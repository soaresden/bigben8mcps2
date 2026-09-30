#include "save.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wrdate(uint8_t *p, const McDate *d) { p[0] = d->resv; p[1] = d->sec; p[2] = d->min; p[3] = d->hour; p[4] = d->day; p[5] = d->month; wr16(p + 6, d->year); }
static void rddate(const uint8_t *p, McDate *d) { d->resv = p[0]; d->sec = p[1]; d->min = p[2]; d->hour = p[3]; d->day = p[4]; d->month = p[5]; d->year = rd16(p + 6); }

void save_init(Save *s) { memset(s, 0, sizeof(*s)); s->attr = 0x8427; }
void save_free(Save *s)
{
    int i;
    for (i = 0; i < s->nfiles; i++) free(s->f[i].data);
    memset(s, 0, sizeof(*s));
}
uint32_t save_total_size(const Save *s)
{
    uint32_t t = 0; int i;
    for (i = 0; i < s->nfiles; i++) t += s->f[i].size;
    return t;
}
int save_find(const Save *s, const char *name)
{
    int i;
    for (i = 0; i < s->nfiles; i++) if (!strcmp(s->f[i].name, name)) return i;
    return -1;
}
int save_needed_kb(const Save *s)
{
    int i, kb = 1 + (s->nfiles + 2 + 1) / 2;   /* cluster du dossier + entrees (2 par Ko) */
    for (i = 0; i < s->nfiles; i++) kb += (s->f[i].size + 1023) / 1024;
    return kb;
}

/* ------------------------------------------------------------ SJIS */
static const struct { uint16_t c; char a; } sj_punct[] = {
    {0x8140, ' '}, {0x8141, ','}, {0x8142, '.'}, {0x8143, ','}, {0x8144, '.'}, {0x8146, ':'}, {0x8147, ';'},
    {0x8148, '?'}, {0x8149, '!'}, {0x814F, '^'}, {0x8151, '_'}, {0x815B, '-'}, {0x815C, '-'}, {0x815D, '-'},
    {0x815E, '/'}, {0x815F, '\\'}, {0x8160, '~'}, {0x8162, '|'}, {0x8165, '\''}, {0x8166, '\''}, {0x8167, '"'},
    {0x8168, '"'}, {0x8169, '('}, {0x816A, ')'}, {0x816D, '['}, {0x816E, ']'}, {0x816F, '{'}, {0x8170, '}'},
    {0x817B, '+'}, {0x817C, '-'}, {0x8181, '='}, {0x8183, '<'}, {0x8184, '>'}, {0x818F, '\\'}, {0x8190, '$'},
    {0x8193, '%'}, {0x8194, '#'}, {0x8195, '&'}, {0x8196, '*'}, {0x8197, '@'}, {0x8180, '/'}, {0x817E, 'x'},
    {0, 0}};

void sjis_to_latin(const uint8_t *s, int len, char *out, int max)
{
    int i = 0, o = 0;
    while (i < len && s[i] && o < max - 1) {
        uint8_t b = s[i];
        if (b < 0x80) { out[o++] = b; i++; continue; }
        if (i + 1 >= len) break;
        {
            uint16_t c = (b << 8) | s[i + 1];
            char a = '?';
            int k;
            if (c >= 0x824F && c <= 0x8258) a = '0' + (c - 0x824F);
            else if (c >= 0x8260 && c <= 0x8279) a = 'A' + (c - 0x8260);
            else if (c >= 0x8281 && c <= 0x829A) a = 'a' + (c - 0x8281);
            else for (k = 0; sj_punct[k].c; k++) if (sj_punct[k].c == c) { a = sj_punct[k].a; break; }
            out[o++] = a;
            i += 2;
        }
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

int latin_to_sjis(const char *s, uint8_t *out, int max)
{
    int o = 0, k;
    for (; *s && o + 2 <= max; s++) {
        unsigned char a = *s;
        uint16_t c = 0x8148;
        if (a >= '0' && a <= '9') c = 0x824F + (a - '0');
        else if (a >= 'A' && a <= 'Z') c = 0x8260 + (a - 'A');
        else if (a >= 'a' && a <= 'z') c = 0x8281 + (a - 'a');
        else for (k = 0; sj_punct[k].c; k++) if (sj_punct[k].a == a) { c = sj_punct[k].c; break; }
        out[o++] = c >> 8; out[o++] = c & 0xff;
    }
    return o;
}

int iconsys_title(const uint8_t *is, int len, char *l1, char *l2, int max)
{
    int nl;
    l1[0] = l2[0] = 0;
    if (len < 0x104 || memcmp(is, "PS2D", 4)) return -1;
    nl = rd16(is + 6);
    if (nl < 0 || nl > 68) nl = 68;
    sjis_to_latin(is + 0xC0, nl, l1, max);
    if (nl < 68) sjis_to_latin(is + 0xC0 + nl, 68 - nl, l2, max);
    return 0;
}

int iconsys_viewicon(const uint8_t *is, int len, char *out, int max)
{
    int i;
    if (len < 0x144 || memcmp(is, "PS2D", 4)) return -1;
    for (i = 0; i < max - 1 && i < 64 && is[0x104 + i]; i++) out[i] = is[0x104 + i];
    out[i] = 0;
    return i ? 0 : -1;
}

/* ------------------------------------------------------------ PSU */
static void psu_entry(uint8_t *e, uint16_t mode, uint32_t size, const McDate *c, const McDate *m, const char *name)
{
    memset(e, 0, 512);
    wr16(e, mode);
    wr32(e + 4, size);
    wrdate(e + 8, c);
    wrdate(e + 24, m);
    strncpy((char *)e + 64, name, 31);
}

int psu_build(const Save *s, uint8_t **out, uint32_t *outlen)
{
    uint32_t len = 3 * 512, p;
    int i;
    uint8_t *b;
    for (i = 0; i < s->nfiles; i++) len += 512 + ((s->f[i].size + 1023) & ~1023u);
    b = calloc(1, len);
    if (!b) return -1;
    psu_entry(b, s->attr ? s->attr : 0x8427, s->nfiles + 2, &s->created, &s->modified, s->dir);
    psu_entry(b + 512, 0x8427, 0, &s->created, &s->modified, ".");
    psu_entry(b + 1024, 0xA426, 0, &s->created, &s->modified, "..");
    p = 1536;
    for (i = 0; i < s->nfiles; i++) {
        const SaveFile *f = &s->f[i];
        psu_entry(b + p, f->attr ? f->attr : 0x8497, f->size, &f->created, &f->modified, f->name);
        p += 512;
        if (f->size) memcpy(b + p, f->data, f->size);
        p += (f->size + 1023) & ~1023u;
    }
    *out = b; *outlen = len;
    return 0;
}

int psu_parse(const uint8_t *buf, uint32_t len, Save *s)
{
    uint32_t p, n, i;
    save_init(s);
    if (len < 1536) return -1;
    if (!(rd16(buf) & 0x20)) return -2;
    n = rd32(buf + 4);
    memcpy(s->dir, buf + 64, 31);
    s->attr = rd16(buf);
    rddate(buf + 8, &s->created);
    rddate(buf + 24, &s->modified);
    p = 512;
    for (i = 0; i < n && p + 512 <= len; i++) {
        const uint8_t *e = buf + p;
        uint16_t mode = rd16(e);
        uint32_t sz = rd32(e + 4);
        p += 512;
        if (mode & 0x20) continue;                 /* "." et ".." */
        if (s->nfiles >= XM_MAXFILES || p + sz > len) { save_free(s); return -3; }
        {
            SaveFile *f = &s->f[s->nfiles++];
            memcpy(f->name, e + 64, 31);
            f->attr = mode; f->size = sz;
            rddate(e + 8, &f->created);
            rddate(e + 24, &f->modified);
            f->data = malloc(sz ? sz : 1);
            if (!f->data) { save_free(s); return -4; }
            memcpy(f->data, buf + p, sz);
        }
        p += (sz + 1023) & ~1023u;
    }
    return 0;
}

/* ------------------------------------------------------------ Archive XMEM2 */
int arc_build(const Save *s, const uint8_t *is, int islen, uint8_t **out, uint32_t *outlen)
{
    uint32_t raw_len = 0, p = 0;
    uLongf clen;
    uint8_t *raw, *b;
    int i;
    for (i = 0; i < s->nfiles; i++) raw_len += 56 + ((s->f[i].size + 3) & ~3u);
    raw = malloc(raw_len ? raw_len : 1);
    if (!raw) return -1;
    for (i = 0; i < s->nfiles; i++) {
        const SaveFile *f = &s->f[i];
        memset(raw + p, 0, 56);
        memcpy(raw + p, f->name, strlen(f->name) > 31 ? 31 : strlen(f->name));
        wr16(raw + p + 32, f->attr);
        wrdate(raw + p + 36, &f->created);
        wrdate(raw + p + 44, &f->modified);
        wr32(raw + p + 52, f->size);
        p += 56;
        memcpy(raw + p, f->data, f->size);
        memset(raw + p + f->size, 0, ((f->size + 3) & ~3u) - f->size);
        p += (f->size + 3) & ~3u;
    }
    clen = compressBound(raw_len);
    b = calloc(1, XM_ARC_HDR + clen);
    if (!b) { free(raw); return -1; }
    if (compress2(b + XM_ARC_HDR, &clen, raw, raw_len, 9) != Z_OK) { free(raw); free(b); return -2; }
    wr32(b, XM_ARC_MAGIC);
    wr32(b + 4, 1);
    wr32(b + 8, raw_len);
    wr32(b + 12, clen);
    wr32(b + 16, crc32(0, raw, raw_len));
    wr16(b + 20, s->attr);
    wr16(b + 22, s->nfiles);
    wrdate(b + 24, &s->created);
    wrdate(b + 32, &s->modified);
    memcpy(b + 40, s->dir, strlen(s->dir) > 31 ? 31 : strlen(s->dir));
    if (is && islen >= 0x104 && !memcmp(is, "PS2D", 4)) {
        memcpy(b + 72, is + 0xC0, 68);
        wr16(b + 140, rd16(is + 6));
    }
    free(raw);
    *out = b; *outlen = XM_ARC_HDR + clen;
    return 0;
}

int arc_read_header(const uint8_t *b, uint32_t len, ArcHeader *h)
{
    if (len < XM_ARC_HDR || rd32(b) != XM_ARC_MAGIC) return -1;
    h->magic = rd32(b); h->version = rd32(b + 4); h->raw_size = rd32(b + 8);
    h->comp_size = rd32(b + 12); h->crc = rd32(b + 16);
    h->dirattr = rd16(b + 20); h->nfiles = rd16(b + 22);
    rddate(b + 24, &h->created); rddate(b + 32, &h->modified);
    memcpy(h->dir, b + 40, 32); h->dir[31] = 0;
    memcpy(h->title, b + 72, 68); h->nl = rd16(b + 140);
    return 0;
}

int arc_extract(const uint8_t *b, uint32_t len, Save *s)
{
    ArcHeader h;
    uLongf rlen;
    uint8_t *raw;
    uint32_t p = 0;
    int i;
    save_init(s);
    if (arc_read_header(b, len, &h)) return -1;
    if (XM_ARC_HDR + h.comp_size > len) return -2;
    raw = malloc(h.raw_size ? h.raw_size : 1);
    if (!raw) return -3;
    rlen = h.raw_size;
    if (uncompress(raw, &rlen, b + XM_ARC_HDR, h.comp_size) != Z_OK || rlen != h.raw_size ||
        crc32(0, raw, rlen) != h.crc) { free(raw); return -4; }
    strcpy(s->dir, h.dir);
    s->attr = h.dirattr; s->created = h.created; s->modified = h.modified;
    for (i = 0; i < h.nfiles && i < XM_MAXFILES; i++) {
        SaveFile *f = &s->f[s->nfiles];
        if (p + 56 > rlen) break;
        memcpy(f->name, raw + p, 31); f->name[31] = 0;
        f->attr = rd16(raw + p + 32);
        rddate(raw + p + 36, &f->created);
        rddate(raw + p + 44, &f->modified);
        f->size = rd32(raw + p + 52);
        p += 56;
        if (p + f->size > rlen) break;
        f->data = malloc(f->size ? f->size : 1);
        memcpy(f->data, raw + p, f->size);
        p += (f->size + 3) & ~3u;
        s->nfiles++;
    }
    free(raw);
    return s->nfiles == h.nfiles ? 0 : -5;
}
