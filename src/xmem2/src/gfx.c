/* Affichage gsKit : fond, texte (atlas DejaVu), icones 3D PS2 */
#include "gfx.h"
#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <malloc.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

extern uint32_t font_atlas[];
extern const uint8_t font_w[];
extern const int font_cw, font_ch, font_cols, font_aw, font_ah;

static GSGLOBAL *gs;
static GSTEXTURE tfont, ticon;
static u16 *iconbuf;
static const Icon *lasticon;
int SW = 640, SH = 448;
static float ys = 1.0f;          /* echelle verticale (PAL 512 / 448) */

void gfx_init(void)
{
    gs = gsKit_init_global();
    gs->PSM = GS_PSM_CT24;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_TexManager_init(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    SW = gs->Width;
    SH = gs->Height;
    ys = SH / 448.0f;

    tfont.Width = font_aw; tfont.Height = font_ah; tfont.PSM = GS_PSM_CT32;
    tfont.Mem = (u32 *)font_atlas; tfont.Filter = GS_FILTER_NEAREST; tfont.Delayed = 1;
    tfont.Vram = 0;

    iconbuf = memalign(128, 128 * 128 * 2);
    memset(iconbuf, 0, 128 * 128 * 2);
    ticon.Width = 128; ticon.Height = 128; ticon.PSM = GS_PSM_CT16;
    ticon.Mem = (u32 *)iconbuf; ticon.Filter = GS_FILTER_LINEAR; ticon.Delayed = 1;
    ticon.Vram = 0;

    FlushCache(0);
    gsKit_set_test(gs, GS_ZTEST_OFF);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void gfx_begin(void)
{
    gsKit_clear(gs, RGB(0, 0, 0));
    /* fond facon navigateur PS2 : degrade gris-bleu */
    gsKit_prim_quad_gouraud_3d(gs, 0, 0, 1, SW, 0, 1, 0, SH, 1, SW, SH, 1,
                               RGB(0x3a, 0x3f, 0x5a), RGB(0x22, 0x26, 0x3a), RGB(0x14, 0x16, 0x22), RGB(0x08, 0x09, 0x10));
}

void gfx_end(void)
{
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
    gsKit_TexManager_nextFrame(gs);
}

void gfx_rect(float x, float y, float w, float h, u64 col)
{
    gsKit_prim_sprite(gs, x, y * ys, x + w, (y + h) * ys, 2, col);
}

void gfx_rect_grad(float x, float y, float w, float h, u64 top, u64 bot)
{
    gsKit_prim_quad_gouraud_3d(gs, x, y * ys, 2, x + w, y * ys, 2, x, (y + h) * ys, 2, x + w, (y + h) * ys, 2, top, top, bot, bot);
}

static int next_cp(const unsigned char **s)
{
    const unsigned char *p = *s;
    int c = *p;
    if (c < 0x80) { *s = p + 1; return c; }
    if ((c & 0xE0) == 0xC0 && p[1]) { *s = p + 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && p[1] && p[2]) {
        int cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *s = p + 3;
        if (cp == 0x2019) return '\'';
        if (cp == 0x2192) return '>';
        return '?';
    }
    *s = p + 1;
    return c;   /* octet latin-1 isole */
}

int gfx_text_w(const char *u)
{
    const unsigned char *s = (const unsigned char *)u;
    int w = 0, c;
    while (*s) {
        c = next_cp(&s);
        if (c < 32 || c > 255) c = '?';
        w += font_w[c - 32] + 1;
    }
    return w;
}

static int draw_text(float x, float y, const char *u, u64 col, int maxw)
{
    const unsigned char *s = (const unsigned char *)u;
    float x0 = x;
    int c;
    gsKit_TexManager_bind(gs, &tfont);
    while (*s) {
        int i, cw, tx, ty;
        c = next_cp(&s);
        if (c < 32 || c > 255) c = '?';
        i = c - 32;
        cw = font_w[i];
        if (maxw > 0 && x + cw - x0 > maxw) break;
        tx = (i % font_cols) * font_cw;
        ty = (i / font_cols) * font_ch;
        if (c != ' ')
            gsKit_prim_sprite_texture(gs, &tfont, x, y * ys, tx, ty, x + cw + 1, (y + font_ch) * ys, tx + cw + 1, ty + font_ch, 3, col);
        x += cw + 1;
    }
    return (int)(x - x0);
}

int gfx_text(float x, float y, const char *u, u64 col) { return draw_text(x, y, u, col, 0); }
void gfx_text_clip(float x, float y, const char *u, u64 col, int maxw) { draw_text(x, y, u, col, maxw); }

void gfx_icon_invalidate(void) { lasticon = NULL; }

/* ---- icone 3D ---- */
typedef struct { float x, y, z, u, v, l; } PV;
static PV *pv;
static int pvcap;
static int *order;
static float *tz;

static int cmp_depth(const void *a, const void *b)
{
    float za = tz[*(const int *)a], zb = tz[*(const int *)b];
    return za < zb ? 1 : (za > zb ? -1 : 0);
}

void gfx_icon(const Icon *ic, float cx, float cy, float size, float ang, float t)
{
    int i, ntri;
    float ca = cosf(ang), sa = sinf(ang), minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9, sc, ox, oy;
    const float *pos;
    if (!ic || !ic->nv) return;
    if (ic != lasticon) {
        memcpy(iconbuf, ic->tex, 128 * 128 * 2);
        FlushCache(0);
        gsKit_TexManager_invalidate(gs, &ticon);
        lasticon = ic;
    }
    if (pvcap < ic->nv) {
        free(pv); free(order); free(tz);
        pvcap = ic->nv;
        pv = malloc(sizeof(PV) * pvcap);
        order = malloc(sizeof(int) * pvcap / 3 + 4);
        tz = malloc(sizeof(float) * pvcap / 3 + 4);
    }
    /* animation simple : interpolation entre les formes au cours du temps */
    pos = ic->pos;
    {
        static float *mix;
        static int mixcap;
        if (ic->nshapes > 1) {
            float ph = fmodf(t * 0.8f, (float)ic->nshapes);
            int s0 = (int)ph, s1 = (s0 + 1) % ic->nshapes;
            float f = ph - s0;
            const float *a = ic->pos + s0 * ic->nv * 3, *b = ic->pos + s1 * ic->nv * 3;
            if (mixcap < ic->nv) { free(mix); mixcap = ic->nv; mix = malloc(sizeof(float) * 3 * mixcap); }
            for (i = 0; i < ic->nv * 3; i++) mix[i] = a[i] + (b[i] - a[i]) * f;
            pos = mix;
        }
    }
    for (i = 0; i < ic->nv; i++) {       /* boite englobante (forme 0) pour centrer */
        const float *p = ic->pos + i * 3;
        if (p[0] < minx) minx = p[0];
        if (p[0] > maxx) maxx = p[0];
        if (p[1] < miny) miny = p[1];
        if (p[1] > maxy) maxy = p[1];
    }
    sc = size / fmaxf(fmaxf(maxx - minx, maxy - miny), 0.01f);
    ox = (minx + maxx) * 0.5f;
    oy = (miny + maxy) * 0.5f;
    for (i = 0; i < ic->nv; i++) {
        const float *p = pos + i * 3, *n = ic->nrm + i * 3;
        float x = p[0] - ox, y = p[1] - oy, z = p[2];
        float rx = x * ca + z * sa, rz = -x * sa + z * ca;
        float nz = -n[0] * sa + n[2] * ca, nx = n[0] * ca + n[2] * sa;
        float persp = 1.0f / (1.0f + rz * 0.06f);
        float l = 0.55f + 0.45f * fmaxf(0.0f, -nz * 0.8f - n[1] * 0.3f + nx * 0.2f);
        pv[i].x = cx + rx * sc * persp;
        pv[i].y = (cy + y * sc * persp) * ys;
        pv[i].z = rz;
        pv[i].u = ic->uv[i * 2] * 128.0f;
        pv[i].v = ic->uv[i * 2 + 1] * 128.0f;
        pv[i].l = l > 1.0f ? 1.0f : l;
    }
    ntri = ic->nv / 3;
    for (i = 0; i < ntri; i++) {
        order[i] = i;
        tz[i] = pv[i * 3].z + pv[i * 3 + 1].z + pv[i * 3 + 2].z;
    }
    qsort(order, ntri, sizeof(int), cmp_depth);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0x80), 0);
    gs->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_TexManager_bind(gs, &ticon);
    for (i = 0; i < ntri; i++) {
        PV *a = &pv[order[i] * 3], *b = a + 1, *c = a + 2;
        u8 la = (u8)(a->l * 0x80), lb = (u8)(b->l * 0x80), lc = (u8)(c->l * 0x80);
        gsKit_prim_triangle_goraud_texture_3d(gs, &ticon,
            a->x, a->y, 4, a->u, a->v, b->x, b->y, 4, b->u, b->v, c->x, c->y, 4, c->u, c->v,
            RGB(la, la, la), RGB(lb, lb, lb), RGB(lc, lc, lc));
    }
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}
