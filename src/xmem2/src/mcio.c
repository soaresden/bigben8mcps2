/* Acces cartes memoire via libmc (rom0:MCMAN/MCSERV, secrman patche) */
#include "mcio.h"
#include <libmc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#define MO_RD 1
#define MO_WR 2
#define MO_CREAT 0x200
#define MAXDIR 256

extern const unsigned char xm_folder_ico[], xm_folder_iconsys[];
extern const unsigned int size_xm_folder_ico, size_xm_folder_iconsys;

static sceMcTblGetDir tbl[MAXDIR] __attribute__((aligned(64)));

static int sync_ret(void)
{
    int r;
    mcSync(0, NULL, &r);
    return r;
}

int mcx_init(void) { return mcInit(MC_TYPE_MC); }

int mcx_info(int port, int *type, int *freekb, int *formatted)
{
    int t = 0, f = 0, fm = 0, r;
    mcGetInfo(port, 0, &t, &f, &fm);
    r = sync_ret();
    if (type) *type = t;
    if (freekb) *freekb = f;
    if (formatted) *formatted = fm;
    return r;
}

static int getdir(int port, const char *pat, int max)
{
    int n;
    if (max > MAXDIR) max = MAXDIR;
    mcGetDir(port, 0, pat, 0, max, tbl);
    n = sync_ret();
    return n;
}

int mcx_read_file(int port, const char *path, unsigned char **data, unsigned int *len)
{
    int fd, sz, r;
    unsigned char *b;
    mcOpen(port, 0, path, MO_RD);
    fd = sync_ret();
    if (fd < 0) return fd;
    mcSeek(fd, 0, SEEK_END);
    sz = sync_ret();
    mcSeek(fd, 0, SEEK_SET);
    sync_ret();
    if (sz < 0) { mcClose(fd); sync_ret(); return sz; }
    b = memalign(64, sz ? sz : 64);
    if (!b) { mcClose(fd); sync_ret(); return -100; }
    r = 0;
    if (sz) { mcRead(fd, b, sz); r = sync_ret(); }
    mcClose(fd);
    sync_ret();
    if (r != sz) { free(b); return -101; }
    *data = b; *len = sz;
    return 0;
}

static int write_file(int port, const char *path, const unsigned char *d, unsigned int len)
{
    int fd, r = 0;
    unsigned int p = 0;
    mcOpen(port, 0, path, MO_WR | MO_CREAT);
    fd = sync_ret();
    if (fd < 0) return fd;
    while (p < len) {
        unsigned int n = len - p > 32768 ? 32768 : len - p;
        mcWrite(fd, d + p, n);
        r = sync_ret();
        if (r != (int)n) { r = r < 0 ? r : -102; break; }
        p += n;
        r = 0;
    }
    mcClose(fd);
    sync_ret();
    return r;
}

static void set_info(int port, const char *path, unsigned short attr, const McDate *c, const McDate *m)
{
    static sceMcTblGetDir info __attribute__((aligned(64)));
    memset(&info, 0, sizeof(info));
    memcpy(&info._Create, c, 8);
    memcpy(&info._Modify, m, 8);
    info.AttrFile = attr;
    mcSetFileInfo(port, 0, path, &info, sceMcFileInfoCreate | sceMcFileInfoModify | sceMcFileInfoAttr);
    sync_ret();
}

int mcx_list(int port, Entry *out, int max)
{
    int n, i, k = 0;
    static sceMcTblGetDir root[MAXDIR] __attribute__((aligned(64)));
    n = getdir(port, "/*", MAXDIR);
    if (n < 0) return n;
    memcpy(root, tbl, sizeof(sceMcTblGetDir) * n);
    for (i = 0; i < n && k < max; i++) {
        sceMcTblGetDir *e = &root[i];
        Entry *o;
        char path[80];
        unsigned char *is;
        unsigned int islen, tot = 0;
        int m, j;
        if (!(e->AttrFile & sceMcFileAttrSubdir)) continue;
        if (!strcmp((char *)e->EntryName, ".") || !strcmp((char *)e->EntryName, "..")) continue;
        if (!strcmp((char *)e->EntryName, XM_ARCDIR)) continue;
        o = &out[k++];
        memset(o, 0, sizeof(*o));
        strncpy(o->dir, (char *)e->EntryName, 31);
        memcpy(&o->modified, &e->_Modify, 8);
        o->attr = e->AttrFile;
        snprintf(path, sizeof(path), "/%s/*", o->dir);
        m = getdir(port, path, MAXDIR);
        for (j = 0; j < m; j++) if (tbl[j].AttrFile & sceMcFileAttrFile) tot += tbl[j].FileSizeByte;
        o->size = tot;
        snprintf(path, sizeof(path), "/%s/icon.sys", o->dir);
        if (mcx_read_file(port, path, &is, &islen) == 0) {
            iconsys_title(is, islen, o->t1, o->t2, 64);
            free(is);
        }
        if (!o->t1[0]) strcpy(o->t1, o->dir);
    }
    return k;
}

int mcx_list_archives(int port, Entry *out, int max)
{
    int n, i, k = 0;
    static sceMcTblGetDir ad[MAXDIR] __attribute__((aligned(64)));
    n = getdir(port, "/" XM_ARCDIR "/*", MAXDIR);
    if (n < 0) return 0;
    memcpy(ad, tbl, sizeof(sceMcTblGetDir) * n);
    for (i = 0; i < n && k < max; i++) {
        sceMcTblGetDir *e = &ad[i];
        char path[80];
        int fd;
        static unsigned char hdr[XM_ARC_HDR] __attribute__((aligned(64)));
        ArcHeader h;
        Entry *o;
        if (!(e->AttrFile & sceMcFileAttrFile)) continue;
        if (!strcmp((char *)e->EntryName, "icon.sys") || !strcmp((char *)e->EntryName, "xmem2.ico")) continue;
        snprintf(path, sizeof(path), "/" XM_ARCDIR "/%s", e->EntryName);
        mcOpen(port, 0, path, MO_RD);
        fd = sync_ret();
        if (fd < 0) continue;
        mcRead(fd, hdr, XM_ARC_HDR);
        sync_ret();
        mcClose(fd);
        sync_ret();
        if (arc_read_header(hdr, XM_ARC_HDR, &h)) continue;
        o = &out[k++];
        memset(o, 0, sizeof(*o));
        strncpy(o->dir, (char *)e->EntryName, 31);
        o->size = e->FileSizeByte;
        o->rawsize = h.raw_size;
        o->modified = h.modified;
        o->attr = h.dirattr;
        sjis_to_latin(h.title, h.nl > 68 ? 68 : h.nl, o->t1, 64);
        if (h.nl < 68) sjis_to_latin(h.title + h.nl, 68 - h.nl, o->t2, 64);
        if (!o->t1[0]) strcpy(o->t1, h.dir);
    }
    return k;
}

int mcx_exists(int port, const char *dir)
{
    char p[48];
    snprintf(p, sizeof(p), "/%s", dir);
    return getdir(port, p, 1) > 0;
}

int mcx_archive_exists(int port, const char *name)
{
    char p[80];
    snprintf(p, sizeof(p), "/" XM_ARCDIR "/%s", name);
    return getdir(port, p, 1) > 0;
}

int mcx_load_save(int port, const char *dir, Save *s)
{
    char path[80];
    int n, i, r;
    static sceMcTblGetDir d[MAXDIR] __attribute__((aligned(64)));
    save_init(s);
    strncpy(s->dir, dir, 31);
    snprintf(path, sizeof(path), "/%s", dir);
    if (getdir(port, path, 1) == 1) {
        s->attr = tbl[0].AttrFile;
        memcpy(&s->created, &tbl[0]._Create, 8);
        memcpy(&s->modified, &tbl[0]._Modify, 8);
    }
    snprintf(path, sizeof(path), "/%s/*", dir);
    n = getdir(port, path, MAXDIR);
    if (n < 0) return n;
    memcpy(d, tbl, sizeof(sceMcTblGetDir) * n);
    for (i = 0; i < n; i++) {
        SaveFile *f;
        unsigned char *data;
        unsigned int len;
        if (!(d[i].AttrFile & sceMcFileAttrFile)) continue;
        if (s->nfiles >= XM_MAXFILES) { save_free(s); return -200; }
        snprintf(path, sizeof(path), "/%s/%s", dir, d[i].EntryName);
        r = mcx_read_file(port, path, &data, &len);
        if (r < 0) { save_free(s); return r; }
        f = &s->f[s->nfiles++];
        strncpy(f->name, (char *)d[i].EntryName, 31);
        f->attr = d[i].AttrFile;
        memcpy(&f->created, &d[i]._Create, 8);
        memcpy(&f->modified, &d[i]._Modify, 8);
        f->size = len;
        f->data = data;
    }
    return 0;
}

int mcx_delete_save(int port, const char *dir)
{
    char path[80];
    int n, i, r;
    static sceMcTblGetDir d[MAXDIR] __attribute__((aligned(64)));
    snprintf(path, sizeof(path), "/%s/*", dir);
    n = getdir(port, path, MAXDIR);
    if (n < 0) return n;
    memcpy(d, tbl, sizeof(sceMcTblGetDir) * n);
    for (i = 0; i < n; i++) {
        if (!(d[i].AttrFile & sceMcFileAttrFile)) continue;
        snprintf(path, sizeof(path), "/%s/%s", dir, d[i].EntryName);
        mcDelete(port, 0, path);
        r = sync_ret();
        if (r < 0) return r;
    }
    snprintf(path, sizeof(path), "/%s", dir);
    mcDelete(port, 0, path);
    return sync_ret();
}

int mcx_write_save(int port, const Save *s, ProgressCb cb)
{
    char path[80];
    int i, r;
    if (mcx_exists(port, s->dir)) {
        r = mcx_delete_save(port, s->dir);
        if (r < 0) return r;
    }
    snprintf(path, sizeof(path), "/%s", s->dir);
    mcMkDir(port, 0, path);
    r = sync_ret();
    if (r < 0) return r;
    for (i = 0; i < s->nfiles; i++) {
        const SaveFile *f = &s->f[i];
        if (cb) cb(f->name, i, s->nfiles);
        snprintf(path, sizeof(path), "/%s/%s", s->dir, f->name);
        r = write_file(port, path, f->data, f->size);
        if (r < 0) return r;
        set_info(port, path, f->attr, &f->created, &f->modified);
    }
    snprintf(path, sizeof(path), "/%s", s->dir);
    set_info(port, path, s->attr ? s->attr : 0x8427, &s->created, &s->modified);
    return 0;
}

static int ensure_arcdir(int port)
{
    int r;
    if (mcx_exists(port, XM_ARCDIR)) return 0;
    mcMkDir(port, 0, "/" XM_ARCDIR);
    r = sync_ret();
    if (r < 0) return r;
    r = write_file(port, "/" XM_ARCDIR "/icon.sys", xm_folder_iconsys, size_xm_folder_iconsys);
    if (r < 0) return r;
    return write_file(port, "/" XM_ARCDIR "/xmem2.ico", xm_folder_ico, size_xm_folder_ico);
}

int mcx_write_archive(int port, const char *name, const unsigned char *buf, unsigned int len)
{
    char path[80];
    int r = ensure_arcdir(port);
    if (r < 0) return r;
    snprintf(path, sizeof(path), "/" XM_ARCDIR "/%s", name);
    if (mcx_archive_exists(port, name)) { mcDelete(port, 0, path); sync_ret(); }
    return write_file(port, path, buf, len);
}

int mcx_delete_archive(int port, const char *name)
{
    char path[80];
    snprintf(path, sizeof(path), "/" XM_ARCDIR "/%s", name);
    mcDelete(port, 0, path);
    return sync_ret();
}

/* raw page read (512 bytes, no ECC) through our read-only XMRAW.IRX (same SIO2 sequence as MMDUMP).
 * Used to read Datel X-MEM archives, which are "deleted" files MCMAN can't open.
 * (rom0:MCSERV's own ReadPage command hung on a real console, so it is not used.) */
#include <sifrpc.h>
#define XMRAW_RPC 0x584D5257
#define CHUNK 16
static SifRpcClientData_t raw_cd __attribute__((aligned(64)));
static unsigned int raw_sbuf[16] __attribute__((aligned(64)));
static unsigned char raw_rbuf[16 + CHUNK * 512] __attribute__((aligned(64)));
static int raw_ok = -1, raw_port = -1;
static unsigned int raw_first = 0xFFFFFFFFu, raw_count;

int mcx_raw_init(void)
{
    int t;
    memset(&raw_cd, 0, sizeof(raw_cd));
    for (t = 0; t < 100; t++) {                         /* ~1 s, never forever */
        if (SifBindRpc(&raw_cd, XMRAW_RPC, 0) >= 0 && raw_cd.server) { raw_ok = 1; return 0; }
        usleep(10000);
    }
    raw_ok = 0;
    return -1;
}

void mcx_raw_invalidate(void) { raw_first = 0xFFFFFFFFu; }

int mcx_read_page(int port, unsigned int page, void *buf512)
{
    if (raw_ok != 1) return -1;
    if (port != raw_port || page < raw_first || page >= raw_first + raw_count) {
        unsigned int start = page & ~(CHUNK - 1u);
        raw_sbuf[0] = port; raw_sbuf[1] = start; raw_sbuf[2] = CHUNK;
        if (SifCallRpc(&raw_cd, 1, 0, raw_sbuf, 16, raw_rbuf, sizeof(raw_rbuf), NULL, NULL) < 0) return -2;
        raw_port = port;
        raw_first = start;
        raw_count = *(unsigned int *)raw_rbuf;
        if (page >= raw_first + raw_count) { raw_first = 0xFFFFFFFFu; return -3; }
    }
    memcpy(buf512, raw_rbuf + 16 + (page - raw_first) * 512, 512);
    return 0;
}
