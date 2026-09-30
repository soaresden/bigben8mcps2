/*
 * XMEM2 - reading Datel "X-MEM" archives straight from the card.
 *
 * Datel's MegaMem software "compresses" a save with LZARI (Haruhiko Okumura, 1989) and stores it as a file of
 * the folder "BESLES-00000 X-MEM" whose directory entry is flagged as *deleted* (mode 0x0417). The card sees
 * those clusters as free, but the FAT still holds their chains (only bit 31, "allocated", is cleared).
 * So they can't be opened through MCMAN: we read raw pages and walk the file system ourselves.
 * Same logic as pc-tools/datel_xmem_extract.py (see docs/REVERSE_ENGINEERING.md, section 6).
 *
 * Archive layout (decompressed stream starts at 0x4C):
 *   0x00 char dir[32]   0x20 char title[32]   0x40 u32 raw size   0x44 u32 file count   0x48 u32 size
 *   payload: per file { u32 size; char name[32]; u8 data[size]; } headers 4-aligned, 0..12 junk bytes between.
 */
#include "datel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void rddate(const uint8_t *p, McDate *d) { d->resv = p[0]; d->sec = p[1]; d->min = p[2]; d->hour = p[3]; d->day = p[4]; d->month = p[5]; d->year = rd16(p + 6); }

/* ------------------------------------------------------------------ LZARI (Okumura) decoder ---- */
#define LZ_N 4096
#define LZ_F 60
#define LZ_THRESHOLD 2
#define LZ_M 15
#define LZ_Q1 (1UL << LZ_M)
#define LZ_Q2 (2 * LZ_Q1)
#define LZ_Q3 (3 * LZ_Q1)
#define LZ_Q4 (4 * LZ_Q1)
#define LZ_MAX_CUM (LZ_Q1 - 1)
#define LZ_N_CHAR (256 - LZ_THRESHOLD + LZ_F)

typedef struct {
    const uint8_t *src;
    uint32_t srclen, pos;
    unsigned int buf, mask;
    unsigned long low, high, value;
    int char_to_sym[LZ_N_CHAR], sym_to_char[LZ_N_CHAR + 1];
    unsigned int sym_freq[LZ_N_CHAR + 1], sym_cum[LZ_N_CHAR + 1], position_cum[LZ_N + 1];
    uint8_t text[LZ_N + LZ_F - 1];
} Lzari;

static int lz_getbit(Lzari *z)
{
    if ((z->mask >>= 1) == 0) {
        z->buf = z->pos < z->srclen ? z->src[z->pos] : 0;
        z->pos++;
        z->mask = 128;
    }
    return (z->buf & z->mask) != 0;
}

static void lz_start_model(Lzari *z)
{
    int ch, sym, i;
    z->sym_cum[LZ_N_CHAR] = 0;
    for (sym = LZ_N_CHAR; sym >= 1; sym--) {
        ch = sym - 1;
        z->char_to_sym[ch] = sym;
        z->sym_to_char[sym] = ch;
        z->sym_freq[sym] = 1;
        z->sym_cum[sym - 1] = z->sym_cum[sym] + z->sym_freq[sym];
    }
    z->sym_freq[0] = 0;
    z->position_cum[LZ_N] = 0;
    for (i = LZ_N; i >= 1; i--) z->position_cum[i - 1] = z->position_cum[i] + 10000 / (i + 200);
}

static void lz_update(Lzari *z, int sym)
{
    int i, c, ch_i, ch_sym;
    if (z->sym_cum[0] >= LZ_MAX_CUM) {
        c = 0;
        for (i = LZ_N_CHAR; i > 0; i--) {
            z->sym_cum[i] = c;
            c += (z->sym_freq[i] = (z->sym_freq[i] + 1) >> 1);
        }
        z->sym_cum[0] = c;
    }
    for (i = sym; z->sym_freq[i] == z->sym_freq[i - 1]; i--) ;
    if (i < sym) {
        ch_i = z->sym_to_char[i];
        ch_sym = z->sym_to_char[sym];
        z->sym_to_char[i] = ch_sym;
        z->sym_to_char[sym] = ch_i;
        z->char_to_sym[ch_i] = sym;
        z->char_to_sym[ch_sym] = i;
    }
    z->sym_freq[i]++;
    while (--i >= 0) z->sym_cum[i]++;
}

static void lz_renorm(Lzari *z)
{
    for (;;) {
        if (z->low >= LZ_Q2) { z->value -= LZ_Q2; z->low -= LZ_Q2; z->high -= LZ_Q2; }
        else if (z->low >= LZ_Q1 && z->high <= LZ_Q3) { z->value -= LZ_Q1; z->low -= LZ_Q1; z->high -= LZ_Q1; }
        else if (z->high > LZ_Q2) break;
        z->low += z->low;
        z->high += z->high;
        z->value = 2 * z->value + lz_getbit(z);
    }
}

static int lz_search(const unsigned int *tbl, unsigned long x, int hi)
{
    int i = 1, j = hi, k;
    while (i < j) {
        k = (i + j) / 2;
        if (tbl[k] > x) i = k + 1; else j = k;
    }
    return i;
}

static int lz_decode_char(Lzari *z)
{
    unsigned long range = z->high - z->low;
    int sym = lz_search(z->sym_cum, ((z->value - z->low + 1) * z->sym_cum[0] - 1) / range, LZ_N_CHAR);
    int ch;
    z->high = z->low + (range * z->sym_cum[sym - 1]) / z->sym_cum[0];
    z->low += (range * z->sym_cum[sym]) / z->sym_cum[0];
    lz_renorm(z);
    ch = z->sym_to_char[sym];
    lz_update(z, sym);
    return ch;
}

static int lz_decode_position(Lzari *z)
{
    unsigned long range = z->high - z->low;
    int p = lz_search(z->position_cum, ((z->value - z->low + 1) * z->position_cum[0] - 1) / range, LZ_N) - 1;
    z->high = z->low + (range * z->position_cum[p]) / z->position_cum[0];
    z->low += (range * z->position_cum[p + 1]) / z->position_cum[0];
    lz_renorm(z);
    return p;
}

/* Datel fills the ring buffer with spaces (0x20). returns 0 if ok */
static int lzari_decode(const uint8_t *src, uint32_t srclen, uint8_t *out, uint32_t outlen)
{
    Lzari *z = malloc(sizeof(Lzari));
    uint32_t n = 0;
    int r, c, i, j, k;
    if (!z) return -1;
    memset(z, 0, sizeof(*z));
    z->src = src; z->srclen = srclen;
    z->high = LZ_Q4;
    for (i = 0; i < LZ_M + 2; i++) z->value = 2 * z->value + lz_getbit(z);
    lz_start_model(z);
    memset(z->text, 0x20, sizeof(z->text));
    r = LZ_N - LZ_F;
    while (n < outlen) {
        if (z->pos > srclen + 8) { free(z); return -2; }     /* ran past the data: corrupted */
        c = lz_decode_char(z);
        if (c < 256) {
            out[n++] = c;
            z->text[r++] = c;
            r &= (LZ_N - 1);
        } else {
            i = (r - lz_decode_position(z) - 1) & (LZ_N - 1);
            j = c - 255 + LZ_THRESHOLD;
            for (k = 0; k < j && n < outlen; k++) {
                c = z->text[(i + k) & (LZ_N - 1)];
                out[n++] = c;
                z->text[r++] = c;
                r &= (LZ_N - 1);
            }
        }
    }
    free(z);
    return 0;
}

/* ------------------------------------------------------------------ card file system ---- */
struct DatelCard {
    DatelPageReader rd;
    void *ctx;
    uint32_t ppc, alloc_off, root;
    uint32_t *fat, nfat;
    uint8_t *clbuf;
};

static int read_cluster(DatelCard *c, uint32_t cl, uint8_t *dst)
{
    uint32_t k;
    for (k = 0; k < c->ppc; k++)
        if (c->rd(c->ctx, cl * c->ppc + k, dst + k * 512)) return -1;
    return 0;
}

DatelCard *datel_open(DatelPageReader rd, void *ctx, int *err)
{
    DatelCard *c = calloc(1, sizeof(DatelCard));
    uint8_t sb[512], *ind;
    uint32_t i, fc;
    *err = -1;
    if (!c) return NULL;
    c->rd = rd; c->ctx = ctx;
    if (rd(ctx, 0, sb)) { *err = -10; free(c); return NULL; }           /* raw page read not possible */
    if (memcmp(sb, "Sony PS2 Memory Card Format", 27)) { *err = -11; free(c); return NULL; }
    c->ppc = rd16(sb + 0x2A);
    c->alloc_off = rd32(sb + 0x34);
    c->root = rd32(sb + 0x3C);
    if (c->ppc < 1 || c->ppc > 16) { *err = -12; free(c); return NULL; }
    c->clbuf = malloc(c->ppc * 512);
    ind = malloc(c->ppc * 512);
    c->fat = malloc(256 * 4 * (c->ppc * 512 / 4));
    if (!c->clbuf || !ind || !c->fat) { *err = -13; free(ind); datel_close(c); return NULL; }
    if (read_cluster(c, rd32(sb + 0x50), ind)) { *err = -14; free(ind); datel_close(c); return NULL; }
    for (i = 0; i < c->ppc * 128; i++) {
        fc = rd32(ind + i * 4);
        if (fc == 0 || fc == 0xFFFFFFFFu) break;
        if (read_cluster(c, fc, c->clbuf)) { *err = -15; free(ind); datel_close(c); return NULL; }
        memcpy(c->fat + c->nfat, c->clbuf, c->ppc * 512);
        c->nfat += c->ppc * 128;
    }
    free(ind);
    if (!c->nfat) { *err = -16; datel_close(c); return NULL; }
    *err = 0;
    return c;
}

void datel_close(DatelCard *c)
{
    if (!c) return;
    free(c->fat);
    free(c->clbuf);
    free(c);
}

/* reads 'len' bytes of the chain starting at cluster 'cl', following links even when marked free */
static uint8_t *read_chain(DatelCard *c, uint32_t cl, uint32_t len)
{
    uint32_t csz = c->ppc * 512, n = (len + csz - 1) / csz, i, v;
    uint8_t *b = malloc(n * csz + 1);
    if (!b) return NULL;
    for (i = 0; i < n; i++) {
        if (cl >= c->nfat || read_cluster(c, cl + c->alloc_off, b + i * csz)) { free(b); return NULL; }
        v = c->fat[cl] & 0x7FFFFFFFu;
        if (i + 1 < n) {
            if (v == 0x7FFFFFFFu || v >= c->nfat) { free(b); return NULL; }
            cl = v;
        }
    }
    return b;
}

/* directory entries of a folder: 512 bytes each */
static uint8_t *read_dir(DatelCard *c, uint32_t cl, uint32_t count)
{
    return read_chain(c, cl, count * 512);
}

static int find_xmem(DatelCard *c, uint32_t *cl, uint32_t *count)
{
    uint8_t *first = read_chain(c, c->root, 512), *d;
    uint32_t n, i;
    if (!first) return -1;
    n = rd32(first + 4);
    free(first);
    if (n < 2 || n > 4096) return -2;
    d = read_dir(c, c->root, n);
    if (!d) return -3;
    for (i = 0; i < n; i++) {
        uint8_t *e = d + i * 512;
        if ((rd16(e) & 0x8020) == 0x8020 && !strncmp((char *)e + 0x40, DATEL_DIR, 32)) {
            *cl = rd32(e + 0x10);
            *count = rd32(e + 4);
            free(d);
            return 0;
        }
    }
    free(d);
    return -4;
}

static void copy_name(char *dst, const uint8_t *src, int max)
{
    int i;
    for (i = 0; i < max - 1 && src[i]; i++) dst[i] = (src[i] >= 32 && src[i] < 127) ? src[i] : '?';
    dst[i] = 0;
}

int datel_list(DatelCard *c, DatelItem *out, int max)
{
    uint32_t xcl, xn, i, n = 0;
    uint8_t *d, *hdr;
    if (find_xmem(c, &xcl, &xn)) return -20;
    d = read_dir(c, xcl, xn);
    if (!d) return -21;
    for (i = 0; i < xn && (int)n < max; i++) {
        uint8_t *e = d + i * 512;
        uint16_t mode = rd16(e);
        DatelItem *it = &out[n];
        char nm[33];
        copy_name(nm, e + 0x40, 33);
        if (!(mode & 0x10) || !strcmp(nm, "icon.sys") || !strcmp(nm, "slime1.ico") || nm[0] == '.') continue;
        memset(it, 0, sizeof(*it));
        strcpy(it->entry, nm);
        it->stored = rd32(e + 4);
        it->cluster = rd32(e + 0x10);
        rddate(e + 0x18, &it->modified);
        hdr = read_chain(c, it->cluster, 0x4C);
        if (!hdr) { it->status = DT_LOST; n++; continue; }
        copy_name(it->dir, hdr, 33);
        copy_name(it->title, hdr + 0x20, 33);
        it->rawsize = rd32(hdr + 0x40);
        it->count = rd32(hdr + 0x44);
        if (it->dir[0] != 'B' || !it->rawsize || it->rawsize >= 4000000 || !it->count || it->count >= 64) {
            it->status = DT_LOST;                             /* first cluster reused by something else */
            it->dir[0] = it->title[0] = 0;
            it->rawsize = it->count = 0;
        }
        free(hdr);
        n++;
    }
    free(d);
    return n;
}

/* per file: u32 size, char name[32], data. headers 4-aligned, may be preceded by 0..12 junk bytes */
static int plausible(const uint8_t *u, uint32_t len, uint32_t q, int loose, uint32_t *sz, char *name)
{
    const uint8_t *f;
    int i, l;
    if (q + 36 > len) return 0;
    *sz = rd32(u + q);
    f = u + q + 4;
    if (loose) while (f < u + q + 36 && !*f) f++;
    for (l = 0; f + l < u + q + 36 && f[l]; l++) ;
    if (!(*sz > 0 && *sz <= len - q - 36 && l >= 3 && l < 32)) return 0;
    for (i = 0; i < l; i++) if (f[i] < 32 || f[i] >= 127) return 0;
    memcpy(name, f, l);
    name[l] = 0;
    return 1;
}

int datel_extract(DatelCard *c, DatelItem *it, Save *s)
{
    uint8_t *raw, *u;
    uint32_t p = 0, i, q, q0, sz = 0;
    int r, loose, hit, has_iconsys = 0;
    char name[33];
    save_init(s);
    if (it->status == DT_LOST) return -30;
    raw = read_chain(c, it->cluster, it->stored);
    if (!raw) return -31;
    u = malloc(it->rawsize + 64);
    if (!u) { free(raw); return -32; }
    r = lzari_decode(raw + 0x4C, it->stored > 0x4C ? it->stored - 0x4C : 0, u, it->rawsize);
    free(raw);
    if (r) { free(u); it->status = DT_CORRUPT; return -33; }

    snprintf(s->dir, sizeof(s->dir), "%.31s", it->dir);
    s->attr = 0x8427;
    s->created = s->modified = it->modified;
    for (i = 0; i < it->count; i++) {
        q0 = (p + 3) & ~3u;
        hit = 0;
        for (loose = 0; loose < 2 && !hit; loose++)
            for (q = q0; q < q0 + 64 && !hit; q += 4)
                if (plausible(u, it->rawsize, q, loose, &sz, name)) hit = 1, q0 = q;
        if (!hit || s->nfiles >= XM_MAXFILES) { save_free(s); free(u); it->status = DT_CORRUPT; return -34; }
        {
            SaveFile *f = &s->f[s->nfiles++];
            snprintf(f->name, sizeof(f->name), "%.31s", name);
            f->attr = 0x8497;
            f->created = f->modified = it->modified;
            f->size = sz;
            f->data = malloc(sz);
            if (!f->data) { save_free(s); free(u); return -35; }
            memcpy(f->data, u + q0 + 36, sz);
            if (!strcmp(name, "icon.sys") && sz >= 4 && !memcmp(f->data, "PS2D", 4)) has_iconsys = 1;
        }
        p = q0 + 36 + sz;
    }
    free(u);
    if (!has_iconsys) { save_free(s); it->status = DT_CORRUPT; return -36; }
    return 0;
}
