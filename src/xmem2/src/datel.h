/* XMEM2 - reading Datel "X-MEM" archives (MegaMem v1.70 "compressed" saves) straight from the card */
#ifndef XM_DATEL_H
#define XM_DATEL_H
#include <stdint.h>
#include "save.h"

#define DATEL_DIR "BESLES-00000 X-MEM"

/* raw page reader: page number -> 512 bytes of data (no ECC). returns 0 if ok */
typedef int (*DatelPageReader)(void *ctx, uint32_t page, uint8_t *buf);

enum { DT_OK = 0, DT_LOST = 1, DT_CORRUPT = 2 };

typedef struct {
    char entry[33];         /* file name inside BESLES-00000 X-MEM */
    char dir[33];           /* save folder stored in the archive */
    char title[33];         /* title as written by the Datel software */
    uint32_t rawsize;       /* decompressed size */
    uint32_t stored;        /* compressed size on the card */
    uint32_t count;         /* number of files */
    uint32_t cluster;       /* first cluster (relative to the allocatable area) */
    McDate modified;        /* date of the archive entry */
    int status;             /* DT_OK / DT_LOST (first cluster overwritten) / DT_CORRUPT (found after a failed extract) */
} DatelItem;

typedef struct DatelCard DatelCard;

/* mounts the card (superblock + FAT). returns NULL on failure, *err = reason (<0) */
DatelCard *datel_open(DatelPageReader rd, void *ctx, int *err);
void datel_close(DatelCard *c);
/* lists the archives of BESLES-00000 X-MEM (including the "deleted" ones). returns count or <0 */
int datel_list(DatelCard *c, DatelItem *out, int max);
/* decompresses one archive into a Save (malloc'd data, free with save_free). returns 0 or <0 */
int datel_extract(DatelCard *c, DatelItem *it, Save *s);

#endif
