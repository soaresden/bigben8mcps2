#ifndef XM_ICON_H
#define XM_ICON_H
#include <stdint.h>
typedef struct {
    int nv, nshapes, frames;
    float *pos;        /* nshapes * nv * 3 */
    float *nrm;        /* nv * 3 */
    float *uv;         /* nv * 2 (0..1) */
    uint16_t *tex;     /* 128*128 A1B5G5R5 */
} Icon;
int icon_parse(const uint8_t *d, int len, Icon *ic);
void icon_free(Icon *ic);
#endif
