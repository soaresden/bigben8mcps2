/* Cle USB (BDM + fileXio) : fichiers .psu dans mass:/PS2SAVES */
#define NEWLIB_PORT_AWARE
#include "usbio.h"
#include <fileXio_rpc.h>
#include <io_common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ctype.h>

int usb_ready(void)
{
    int d = fileXioDopen("mass:/");
    if (d < 0) return 0;
    fileXioDclose(d);
    fileXioMkdir(XM_USBDIR, 0777);
    return 1;
}

static int ends_psu(const char *n)
{
    int l = strlen(n);
    return l > 4 && tolower((unsigned char)n[l - 1]) == 'u' && tolower((unsigned char)n[l - 2]) == 's' &&
           tolower((unsigned char)n[l - 3]) == 'p' && n[l - 4] == '.';
}

static unsigned int rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* parcourt les entrees d'un psu en sautant les donnees ; lit le fichier 'inner' si demande */
static int psu_walk(int fd, Entry *e, const char *inner, unsigned char **buf, unsigned int *len)
{
    static unsigned char h[512] __attribute__((aligned(64)));
    unsigned int n, i, pos = 512;
    if (fileXioRead(fd, h, 512) != 512 || !(h[0] & 0x20)) return -1;
    n = rd32(h + 4);
    if (e) {
        memset(e, 0, sizeof(*e));
        memcpy(e->dir, h + 64, 31);
        memcpy(&e->modified, h + 24, 8);
        e->attr = h[0] | (h[1] << 8);
    }
    for (i = 0; i < n && i < 70; i++) {
        unsigned int sz;
        char nm[33];
        fileXioLseek(fd, pos, SEEK_SET);
        if (fileXioRead(fd, h, 512) != 512) break;
        pos += 512;
        sz = rd32(h + 4);
        memcpy(nm, h + 64, 32); nm[32] = 0;
        if (h[0] & 0x20) continue;
        if (e) e->size += sz;
        if ((e && !strcmp(nm, "icon.sys")) || (inner && !strcmp(nm, inner))) {
            unsigned char *b = memalign(64, sz ? sz : 64);
            if (b && (sz == 0 || fileXioRead(fd, b, sz) == (int)sz)) {
                if (e && !strcmp(nm, "icon.sys")) iconsys_title(b, sz, e->t1, e->t2, 64);
                if (inner && !strcmp(nm, inner) && buf) { *buf = b; *len = sz; return 0; }
            }
            free(b);
        }
        pos += (sz + 1023) & ~1023u;
    }
    return inner ? -2 : 0;
}

int usb_list(Entry *out, int max)
{
    iox_dirent_t de;
    int d, k = 0;
    d = fileXioDopen(XM_USBDIR);
    if (d < 0) return d;
    while (k < max && fileXioDread(d, &de) > 0) {
        char path[320];
        int fd;
        if (!ends_psu(de.name)) continue;
        snprintf(path, sizeof(path), XM_USBDIR "/%s", de.name);
        fd = fileXioOpen(path, FIO_O_RDONLY);
        if (fd < 0) continue;
        if (psu_walk(fd, &out[k], NULL, NULL, NULL) == 0) {
            if (!out[k].t1[0]) strcpy(out[k].t1, out[k].dir);
            snprintf(out[k].file, sizeof(out[k].file), "%s", de.name);
            k++;
        }
        fileXioClose(fd);
    }
    fileXioDclose(d);
    return k;
}

int usb_read_psu_file(const char *name, const char *inner, unsigned char **buf, unsigned int *len)
{
    char path[320];
    int fd, r;
    snprintf(path, sizeof(path), XM_USBDIR "/%s", name);
    fd = fileXioOpen(path, FIO_O_RDONLY);
    if (fd < 0) return fd;
    r = psu_walk(fd, NULL, inner, buf, len);
    fileXioClose(fd);
    return r;
}

int usb_read_all(const char *name, unsigned char **buf, unsigned int *len)
{
    char path[320];
    int fd, sz;
    unsigned char *b;
    snprintf(path, sizeof(path), XM_USBDIR "/%s", name);
    fd = fileXioOpen(path, FIO_O_RDONLY);
    if (fd < 0) return fd;
    sz = fileXioLseek(fd, 0, SEEK_END);
    fileXioLseek(fd, 0, SEEK_SET);
    b = memalign(64, sz > 0 ? sz : 64);
    if (!b || fileXioRead(fd, b, sz) != sz) { free(b); fileXioClose(fd); return -1; }
    fileXioClose(fd);
    *buf = b; *len = sz;
    return 0;
}

void usb_safe_name(const char *dir, char *out)
{
    int i;
    for (i = 0; dir[i] && i < 40; i++) out[i] = strchr("\\/:*?\"<>|", dir[i]) ? '_' : dir[i];
    strcpy(out + i, ".psu");
}

int usb_exists(const char *name)
{
    char path[320];
    iox_stat_t st;
    snprintf(path, sizeof(path), XM_USBDIR "/%s", name);
    return fileXioGetStat(path, &st) >= 0;
}

int usb_write(const char *name, const unsigned char *buf, unsigned int len)
{
    char path[320];
    int fd, r;
    snprintf(path, sizeof(path), XM_USBDIR "/%s", name);
    fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) return fd;
    r = fileXioWrite(fd, buf, len);
    fileXioClose(fd);
    return r == (int)len ? 0 : -1;
}
