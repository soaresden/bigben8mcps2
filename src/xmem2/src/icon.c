/* Parseur d'icones 3D PS2 (.ico) */
#include "icon.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static int16_t rds16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

void icon_free(Icon *ic)
{
    free(ic->pos); free(ic->nrm); free(ic->uv); free(ic->tex);
    memset(ic, 0, sizeof(*ic));
}

int icon_parse(const uint8_t *d, int len, Icon *ic)
{
    uint32_t magic, shapes, tt, nv, vsz, o, frames, i, s;
    memset(ic, 0, sizeof(*ic));
    if (len < 20) return -1;
    magic = rd32(d); shapes = rd32(d + 4); tt = rd32(d + 8); nv = rd32(d + 16);
    if (magic != 0x010000 || shapes < 1 || shapes > 16 || nv < 3 || nv > 20000) return -2;
    vsz = shapes * 8 + 16;
    if (20 + nv * vsz + 20 > (uint32_t)len) return -3;
    ic->nv = nv; ic->nshapes = shapes;
    ic->pos = malloc(sizeof(float) * 3 * nv * shapes);
    ic->nrm = malloc(sizeof(float) * 3 * nv);
    ic->uv = malloc(sizeof(float) * 2 * nv);
    ic->tex = malloc(128 * 128 * 2);
    if (!ic->pos || !ic->nrm || !ic->uv || !ic->tex) { icon_free(ic); return -4; }
    o = 20;
    for (i = 0; i < nv; i++) {
        const uint8_t *v = d + o + i * vsz;
        for (s = 0; s < shapes; s++) {
            float *p = ic->pos + (s * nv + i) * 3;
            p[0] = rds16(v + s * 8) / 4096.0f;
            p[1] = rds16(v + s * 8 + 2) / 4096.0f;
            p[2] = rds16(v + s * 8 + 4) / 4096.0f;
        }
        v += shapes * 8;
        ic->nrm[i * 3] = rds16(v) / 4096.0f;
        ic->nrm[i * 3 + 1] = rds16(v + 2) / 4096.0f;
        ic->nrm[i * 3 + 2] = rds16(v + 4) / 4096.0f;
        ic->uv[i * 2] = rds16(v + 8) / 4096.0f;
        ic->uv[i * 2 + 1] = rds16(v + 10) / 4096.0f;
    }
    o += nv * vsz;
    /* animation : id, frameLength, speed, offset, nframes ; puis par frame : shape, nkeys, nkeys*(t,v) */
    frames = rd32(d + o + 16);
    ic->frames = frames;
    o += 20;
    for (i = 0; i < frames && i < 64; i++) {
        uint32_t nk;
        if (o + 8 > (uint32_t)len) { icon_free(ic); return -5; }
        nk = rd32(d + o + 4);
        o += 8 + nk * 8;
    }
    if (tt & 8) {                                   /* texture compressee RLE */
        uint32_t sz, p = 0, n = 0;
        const uint8_t *src;
        if (o + 4 > (uint32_t)len) { icon_free(ic); return -6; }
        sz = rd32(d + o); src = d + o + 4;
        if (o + 4 + sz > (uint32_t)len) { icon_free(ic); return -7; }
        while (n < 16384 && p + 2 <= sz) {
            uint16_t code = src[p] | (src[p + 1] << 8);
            p += 2;
            if (code & 0x8000) {
                uint32_t k, cnt = 0x10000 - code;
                for (k = 0; k < cnt && n < 16384 && p + 2 <= sz; k++, p += 2) ic->tex[n++] = src[p] | (src[p + 1] << 8);
            } else {
                uint16_t val;
                uint32_t k;
                if (p + 2 > sz) break;
                val = src[p] | (src[p + 1] << 8);
                p += 2;
                for (k = 0; k < code && n < 16384; k++) ic->tex[n++] = val;
            }
        }
        while (n < 16384) ic->tex[n++] = 0;
    } else if (tt & 4) {                            /* texture brute */
        if (o + 32768 > (uint32_t)len) { icon_free(ic); return -8; }
        memcpy(ic->tex, d + o, 32768);
    } else {
        for (i = 0; i < 16384; i++) ic->tex[i] = 0x7fff;
    }
    return 0;
}
