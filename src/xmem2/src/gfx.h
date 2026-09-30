#ifndef XM_GFX_H
#define XM_GFX_H
#include <tamtypes.h>
#include "icon.h"
#define RGB(r, g, b) ((u64)(r) | ((u64)(g) << 8) | ((u64)(b) << 16) | ((u64)0x80 << 24))
#define RGBA(r, g, b, a) ((u64)(r) | ((u64)(g) << 8) | ((u64)(b) << 16) | ((u64)(a) << 24))
extern int SW, SH;     /* taille ecran logique (640 x 448 ou 512) */
void gfx_init(void);
void gfx_begin(void);                  /* fond + debut de frame */
void gfx_end(void);                    /* envoi + vsync */
void gfx_rect(float x, float y, float w, float h, u64 col);
void gfx_rect_grad(float x, float y, float w, float h, u64 top, u64 bot);
int  gfx_text(float x, float y, const char *utf8, u64 col);          /* retourne la largeur */
int  gfx_text_w(const char *utf8);
void gfx_text_clip(float x, float y, const char *utf8, u64 col, int maxw);
/* icone 3D : ic peut etre NULL ; angle en radians ; t = temps (animation) */
void gfx_icon(const Icon *ic, float cx, float cy, float size, float angle, float t);
void gfx_icon_invalidate(void);        /* a appeler quand l'icone change */
#endif
