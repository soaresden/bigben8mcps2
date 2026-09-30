#!/usr/bin/env python3
"""
datel_xmem_extract.py - recover the saves hidden by Datel's "Mega Memory" software (X-MEM)
from a raw PS2 memory card image, and write them as standard .psu files.

Datel's MegaMem Program (PS2, 2001) compresses saves with LZARI (Haruhiko Okumura, 1989) and stores
them as files of the folder "BESLES-00000 X-MEM" that are flagged as *deleted* on the card.
Their clusters are therefore seen as free by the PS2: anything written later may overwrite them.
The FAT still keeps the cluster chains (only the "allocated" bit is cleared), so they can be followed.

Usage:  python3 datel_xmem_extract.py card_dump.(bin|mcd|ps2) output_dir
Accepts 8 MB images with ECC (528-byte pages, e.g. MMDUMP, PCSX2, OPL VMC) or without (512-byte pages).
"""
import os, struct, sys

# ---------------------------------------------------------------- LZARI decoder (Okumura)
N, F, THRESHOLD = 4096, 60, 2
M = 15
Q1, Q2, Q3, Q4 = 1 << M, 2 << M, 3 << M, 4 << M
MAX_CUM = Q1 - 1
N_CHAR = 256 - THRESHOLD + F


def lzari_decode(src, textsize, fill=0x20):
    pos = [0]; buf = [0]; mask = [0]

    def getbit():
        mask[0] >>= 1
        if mask[0] == 0:
            buf[0] = src[pos[0]] if pos[0] < len(src) else 0
            pos[0] += 1
            mask[0] = 128
        return 1 if buf[0] & mask[0] else 0

    char_to_sym = [0] * N_CHAR
    sym_to_char = [0] * (N_CHAR + 1)
    sym_freq = [0] * (N_CHAR + 1)
    sym_cum = [0] * (N_CHAR + 1)
    position_cum = [0] * (N + 1)
    for sym in range(N_CHAR, 0, -1):
        ch = sym - 1
        char_to_sym[ch] = sym; sym_to_char[sym] = ch; sym_freq[sym] = 1
        sym_cum[sym - 1] = sym_cum[sym] + sym_freq[sym]
    for i in range(N, 0, -1):
        position_cum[i - 1] = position_cum[i] + 10000 // (i + 200)

    def update(sym):
        if sym_cum[0] >= MAX_CUM:
            c = 0
            for i in range(N_CHAR, 0, -1):
                sym_cum[i] = c
                sym_freq[i] = (sym_freq[i] + 1) >> 1
                c += sym_freq[i]
            sym_cum[0] = c
        i = sym
        while sym_freq[i] == sym_freq[i - 1]:
            i -= 1
        if i < sym:
            ch_i, ch_sym = sym_to_char[i], sym_to_char[sym]
            sym_to_char[i], sym_to_char[sym] = ch_sym, ch_i
            char_to_sym[ch_i], char_to_sym[ch_sym] = sym, i
        sym_freq[i] += 1
        for k in range(i - 1, -1, -1):
            sym_cum[k] += 1

    st = {'low': 0, 'high': Q4, 'value': 0}
    for _ in range(M + 2):
        st['value'] = 2 * st['value'] + getbit()

    def renorm():
        low, high, value = st['low'], st['high'], st['value']
        while True:
            if low >= Q2:
                value -= Q2; low -= Q2; high -= Q2
            elif low >= Q1 and high <= Q3:
                value -= Q1; low -= Q1; high -= Q1
            elif high > Q2:
                break
            low += low; high += high; value = 2 * value + getbit()
        st['low'], st['high'], st['value'] = low, high, value

    def bsearch(tbl, x, hi, off):
        i, j = 1, hi
        while i < j:
            k = (i + j) // 2
            if tbl[k] > x: i = k + 1
            else: j = k
        return i - off

    def decode_char():
        rng = st['high'] - st['low']
        sym = bsearch(sym_cum, ((st['value'] - st['low'] + 1) * sym_cum[0] - 1) // rng, N_CHAR, 0)
        st['high'] = st['low'] + (rng * sym_cum[sym - 1]) // sym_cum[0]
        st['low'] += (rng * sym_cum[sym]) // sym_cum[0]
        renorm()
        ch = sym_to_char[sym]
        update(sym)
        return ch

    def decode_position():
        rng = st['high'] - st['low']
        p = bsearch(position_cum, ((st['value'] - st['low'] + 1) * position_cum[0] - 1) // rng, N, 1)
        st['high'] = st['low'] + (rng * position_cum[p]) // position_cum[0]
        st['low'] += (rng * position_cum[p + 1]) // position_cum[0]
        renorm()
        return p

    text = bytearray([fill]) * (N + F - 1)
    r = N - F
    out = bytearray()
    while len(out) < textsize:
        c = decode_char()
        if c < 256:
            out.append(c); text[r] = c; r = (r + 1) & (N - 1)
        else:
            i = (r - decode_position() - 1) & (N - 1)
            for k in range(c - 255 + THRESHOLD):
                if len(out) >= textsize: break
                c2 = text[(i + k) & (N - 1)]
                out.append(c2); text[r] = c2; r = (r + 1) & (N - 1)
    return bytes(out), pos[0]

# ---------------------------------------------------------------- PS2 memory card filesystem
class Card:
    def __init__(self, data):
        if len(data) % 528 == 0 and len(data) // 528 in (16384, 32768, 65536):
            ps = 528
        elif len(data) % 512 == 0:
            ps = 512
        else:
            raise ValueError('unknown image size')
        self.d, self.ps = data, ps
        sb = self.page(0)
        if not sb.startswith(b'Sony PS2 Memory Card Format'):
            raise ValueError('not a formatted PS2 memory card image')
        self.ppc, = struct.unpack_from('<H', sb, 0x2A)
        self.alloc_off, self.alloc_end, self.root = struct.unpack_from('<III', sb, 0x34)
        ifc = struct.unpack_from('<32I', sb, 0x50)
        ind = self.cluster(ifc[0])
        self.fat = []
        for i in range(256):
            fc = struct.unpack_from('<I', ind, i * 4)[0]
            if fc in (0, 0xFFFFFFFF): break
            self.fat += struct.unpack('<256I', self.cluster(fc))

    def page(self, i): return self.d[i * self.ps:i * self.ps + 512]
    def cluster(self, c): return b''.join(self.page(c * self.ppc + k) for k in range(self.ppc))

    def chain(self, c, n):
        out = [c]
        while len(out) < n:                      # follow links even if "free" (bit 31 cleared)
            v = self.fat[c] & 0x7FFFFFFF
            if v == 0x7FFFFFFF or v >= len(self.fat): break
            out.append(v); c = v
        return out

    def read(self, cl, length):
        n = (length + 1023) // 1024
        return b''.join(self.cluster(x + self.alloc_off) for x in self.chain(cl, n))[:length]

    def entries(self, cl, count):
        raw = self.read(cl, count * 512)
        for i in range(count):
            e = raw[i * 512:(i + 1) * 512]
            if len(e) < 512: break
            mode, length = struct.unpack_from('<HxxI', e, 0)
            ecl, = struct.unpack_from('<I', e, 0x10)
            name = e[0x40:0x60].split(b'\0')[0].decode('latin1')
            yield name, mode, length, ecl, e

# ---------------------------------------------------------------- X-MEM archives -> .psu
def psu_entry(mode, size, name, created=b'\0' * 8, modified=b'\0' * 8):
    e = bytearray(512)
    struct.pack_into('<HxxI', e, 0, mode, size)
    e[8:16] = created; e[24:32] = modified
    n = name.encode('latin1')[:31]; e[64:64 + len(n)] = n
    return bytes(e)


def parse_archive(u, count):
    files, p = [], 0
    def plausible(q, loose):
        if q + 36 > len(u): return None
        sz, = struct.unpack_from('<I', u, q)
        f = u[q + 4:q + 36]
        nm = (f.lstrip(b'\0') if loose else f).split(b'\0')[0]
        if 0 < sz <= len(u) - q - 36 and 3 <= len(nm) < 32 and all(32 <= c < 127 for c in nm):
            return sz, nm.decode('latin1')
        return None
    for _ in range(count):
        q0 = (p + 3) & ~3
        hit = None
        for loose in (False, True):          # headers are 4-aligned, followed by 0..12 junk bytes
            for q in range(q0, q0 + 64, 4):
                h = plausible(q, loose)
                if h: hit = (q, h); break
            if hit: break
        if not hit: return None
        q, (sz, nm) = hit
        files.append((nm, u[q + 36:q + 36 + sz])); p = q + 36 + sz
    return files


def main():
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(1)
    card = Card(open(sys.argv[1], 'rb').read())
    os.makedirs(sys.argv[2], exist_ok=True)
    root_len = struct.unpack_from('<I', card.read(card.root, 512), 4)[0]
    xmem = [e for e in card.entries(card.root, root_len) if e[0] == 'BESLES-00000 X-MEM']
    if not xmem:
        print('No "BESLES-00000 X-MEM" folder on this card.'); return
    _, _, xlen, xcl, _ = xmem[0]
    ok = bad = 0
    for name, mode, length, cl, e in card.entries(xcl, xlen):
        if not (mode & 0x10) or name in ('icon.sys', 'slime1.ico'): continue
        d = card.read(cl, length)
        arc = d[0:32].split(b'\0')[0].decode('latin1', 'replace')
        title = d[0x20:0x40].split(b'\0')[0].decode('latin1', 'replace')
        size, count, _ = struct.unpack_from('<III', d, 0x40)
        if not arc.startswith('B') or not (0 < size < 4_000_000 and 0 < count < 64):
            print('  LOST       %-22s (first cluster overwritten)' % name); bad += 1; continue
        u, _ = lzari_decode(d[0x4C:], size)
        files = parse_archive(u, count)
        if not files or not any(f[0] == 'icon.sys' and f[1][:4] == b'PS2D' for f in files):
            print('  CORRUPTED  %-22s %s' % (name, title)); bad += 1; continue
        psu = bytearray(psu_entry(0x8427, len(files) + 2, arc) + psu_entry(0x8427, 0, '.') + psu_entry(0xA426, 0, '..'))
        for nm, data in files:
            psu += psu_entry(0x8497, len(data), nm) + data + b'\0' * ((-len(data)) % 1024)
        safe = ''.join('_' if c in '\\/:*?"<>|#' else c for c in arc)
        open(os.path.join(sys.argv[2], safe + '.psu'), 'wb').write(psu)
        print('  RECOVERED  %-22s %-24s %d files, %d bytes' % (name, title, len(files), len(u))); ok += 1
    print('%d recovered, %d lost' % (ok, bad))


if __name__ == '__main__':
    main()
