/*
 * MMBOOT.ELF v3 - "the CD, without the CD", with a small launcher menu.
 *
 * 1. Installs the MegaMem resident driver (clean-room, no Datel code) in the EE kernel area.
 *    From then on, until power-off, every program started through ExecPS2 gets SECRMAN's
 *    SecrAuthCard patched after each IOP reboot, so a Datel Mega Memory card (no MagicGate)
 *    is accepted like a Sony card.
 * 2. Starts the auto-boot entry of MMBOOT.CNF after a short countdown, or shows a menu listing every
 *    ELF found on the memory cards, USB drives and exFAT HDD. In the menu:
 *       X launch, TRIANGLE add/remove favourite, SQUARE set/clear auto-boot, L1/R1 tabs, SELECT rescan.
 *    Favourites and auto-boot are saved back to MMBOOT.CNF.
 *
 * MMBOOT.CNF is searched next to MMBOOT.ELF, then mc0:/MMBOOT/, mc1:/MMBOOT/, mc0:/PS2BBL/, mc1:/PS2BBL/,
 * mass:/MMBOOT/, mass:/PS2BBL/. See docs/MMBOOT.md.
 */
#define NEWLIB_PORT_AWARE
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <libmc.h>
#include <fileXio_rpc.h>
#include <iox_stat.h>
#include <elf-loader.h>
#include <osd_config.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include "gfx.h"
#include "res_install.h"

#define VERSION "3.0"

int g_fr;
#define T(fr, en) (g_fr ? (fr) : (en))

#define IRX(n) extern unsigned char n[]; extern unsigned int size_##n;
IRX(secrpatch_irx) IRX(iomanX_irx) IRX(fileXio_irx) IRX(usbd_irx) IRX(bdm_irx) IRX(bdmfs_fatfs_irx)
IRX(usbmass_bd_irx) IRX(ps2dev9_irx) IRX(ata_bd_irx)

#define MAX_BOOT 8
#define MAX_FAV  32
#define MAXELF   400
#define PATHLEN  256

static char padbuf[256] __attribute__((aligned(64)));
static int pad_ok, hdd_loaded;
static unsigned int prevpad;
static int rep;

static char cfg_boot[MAX_BOOT][PATHLEN];
static char cfg_fav[MAX_FAV][PATHLEN];
static int cfg_nboot, cfg_nfav, cfg_delay = 2, cfg_hdd = 1, cfg_wait = 5;
static char cfg_path[PATHLEN], self_path[PATHLEN];

typedef struct { char path[PATHLEN]; char name[40]; } Elf;
static Elf *elfs;
static int nelf, scanned;

static unsigned int drv_st[8];
static int drv_active;

static char toast[160];
static u64 toast_col;
static int toast_t;

/* ------------------------------------------------------------------ IOP ---- */

static int exec_buf(void *p, unsigned int sz)
{
    int r = 0;
    return SifExecModuleBuffer(p, sz, 0, NULL, &r) < 0 ? -1 : r;
}

static void pad_open(void)
{
    int st, tries;
    if (padPortOpen(0, 0, padbuf)) {
        pad_ok = 1;
        for (tries = 0; tries < 100; tries++) {
            st = padGetState(0, 0);
            if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) break;
            usleep(10000);
        }
    }
}

static void iop_init(void)
{
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {};
    while (!SifIopSync()) {};
    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();        /* needed by LoadELFFromFile */

    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    exec_buf(secrpatch_irx, size_secrpatch_irx);   /* so the Datel card is readable from here too */
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    exec_buf(iomanX_irx, size_iomanX_irx);
    exec_buf(fileXio_irx, size_fileXio_irx);
    exec_buf(usbd_irx, size_usbd_irx);
    exec_buf(bdm_irx, size_bdm_irx);
    exec_buf(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
    exec_buf(usbmass_bd_irx, size_usbmass_bd_irx);
    fileXioInit();
    mcInit(MC_TYPE_MC);
    if (padInit(0) == 1) pad_open();
}

/* internal HDD formatted exFAT/FAT32 -> appears as a mass?: device through BDM */
static void load_hdd(void)
{
    if (hdd_loaded) return;
    hdd_loaded = 1;
    if (exec_buf(ps2dev9_irx, size_ps2dev9_irx) >= 0) exec_buf(ata_bd_irx, size_ata_bd_irx);
}

static unsigned int pad_read(void)
{
    struct padButtonStatus b;
    int st;
    if (!pad_ok) return 0;
    st = padGetState(0, 0);
    if (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1) return 0;
    if (padRead(0, 0, &b) == 0) return 0;
    return 0xffff ^ b.btns;
}

static unsigned int pad_edge(void)
{
    unsigned int p = pad_read(), e = p & ~prevpad;
    if (p & (PAD_UP | PAD_DOWN)) {
        if (++rep > 18 && (rep % 4) == 0) e |= p & (PAD_UP | PAD_DOWN);
    } else rep = 0;
    prevpad = p;
    return e;
}

/* ---------------------------------------------------------------- files ---- */

static int mc_split(const char *path, int *port, const char **rest)
{
    if (strncmp(path, "mc", 2) || (path[2] != '0' && path[2] != '1') || path[3] != ':') return 0;
    *port = path[2] - '0';
    *rest = path + 4;
    return 1;
}

static int mc_sync(void) { int r; mcSync(0, NULL, &r); return r; }

static int mc_is_ps2(int port)
{
    int type = 0, fr, fmt = 0;
    mcGetInfo(port, 0, &type, &fr, &fmt);
    mc_sync();
    return type == MC_TYPE_PS2 && fmt;
}

/* "mc0:/A/B" through libmc (works for the Datel card thanks to the IRX), anything else through fileXio */
static int read_file(const char *path, char *buf, int max)
{
    int port, fd, n = -1;
    const char *rest;
    if (mc_split(path, &port, &rest)) {
        if (!mc_is_ps2(port)) return -1;
        mcOpen(port, 0, rest, O_RDONLY);
        fd = mc_sync();
        if (fd < 0) return -1;
        if (buf) { mcRead(fd, buf, max); n = mc_sync(); } else n = 0;
        mcClose(fd);
        mc_sync();
        return n;
    }
    fd = fileXioOpen(path, O_RDONLY);
    if (fd < 0) return -1;
    n = buf ? fileXioRead(fd, buf, max) : 0;
    fileXioClose(fd);
    return n;
}

static int write_file(const char *path, const char *buf, int len)
{
    int port, fd, n;
    const char *rest;
    if (mc_split(path, &port, &rest)) {
        if (!mc_is_ps2(port)) return -1;
        mcDelete(port, 0, rest);              /* mcman does not truncate */
        mc_sync();
        mcOpen(port, 0, rest, O_WRONLY | O_CREAT);
        fd = mc_sync();
        if (fd < 0) return -1;
        mcWrite(fd, buf, len);
        n = mc_sync();
        mcClose(fd);
        mc_sync();
        return n == len ? 0 : -1;
    }
    fd = fileXioOpen(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    n = fileXioWrite(fd, buf, len);
    fileXioClose(fd);
    return n == len ? 0 : -1;
}

static int file_exists(const char *path) { return read_file(path, NULL, 0) >= 0; }
static int is_mass(const char *p) { return !strncmp(p, "mass", 4); }

/* mass0:/X, mass:/X -> mass?:/X  (USB and HDD can swap unit numbers from one boot to the next) */
static void canonical(const char *in, char *out)
{
    if (!strncmp(in, "mass", 4) && isdigit((unsigned char)in[4]) && in[5] == ':') snprintf(out, PATHLEN, "mass?:%.240s", in + 6);
    else if (!strncmp(in, "mass:", 5)) snprintf(out, PATHLEN, "mass?:%.240s", in + 5);
    else snprintf(out, PATHLEN, "%.250s", in);
}

static int same_path(const char *a, const char *b)
{
    char ca[PATHLEN], cb[PATHLEN];
    canonical(a, ca);
    canonical(b, cb);
    /* "mc?:" in the config matches mc0: and mc1: */
    if (!strncmp(ca, "mc?:", 4) && !strncmp(cb, "mc", 2) && cb[3] == ':') return !strcasecmp(ca + 4, cb + 4);
    if (!strncmp(cb, "mc?:", 4) && !strncmp(ca, "mc", 2) && ca[3] == ':') return !strcasecmp(ca + 4, cb + 4);
    return !strcasecmp(ca, cb);
}

/* "mc?:" -> mc0:, mc1:   "mass?:" -> mass0: .. mass4: */
static int expand(const char *in, char out[][PATHLEN], int max)
{
    int n = 0, i;
    if (!strncmp(in, "mc?:", 4)) {
        for (i = 0; i < 2 && n < max; i++, n++) snprintf(out[n], PATHLEN, "mc%d:%.240s", i, in + 4);
    } else if (!strncmp(in, "mass?:", 6)) {
        for (i = 0; i < 5 && n < max; i++, n++) snprintf(out[n], PATHLEN, "mass%d:%.240s", i, in + 6);
    } else if (max > 0) snprintf(out[n++], PATHLEN, "%.255s", in);
    return n;
}

/* first existing file behind a (possibly wildcard) path; "BROWSER" stays as is */
static int resolve(const char *in, char *out)
{
    char cand[8][PATHLEN];
    int j, n;
    if (!strcasecmp(in, "BROWSER") || !strcasecmp(in, "rom0:OSDSYS")) { strcpy(out, "BROWSER"); return 1; }
    n = expand(in, cand, 8);
    for (j = 0; j < n; j++)
        if (file_exists(cand[j])) { strcpy(out, cand[j]); return 1; }
    return 0;
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    if (!s) s = strrchr(p, ':');
    return s ? s + 1 : p;
}

/* --------------------------------------------------------------- config ---- */

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void parse_cnf(char *text)
{
    char *line = text, *next, *eq, *k, *v;
    while (line && *line) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        line = trim(line);
        if (*line && *line != '#' && *line != ';' && (eq = strchr(line, '='))) {
            *eq = 0;
            k = trim(line);
            v = trim(eq + 1);
            if (!strcasecmp(k, "BOOT") && cfg_nboot < MAX_BOOT && *v) snprintf(cfg_boot[cfg_nboot++], PATHLEN, "%.255s", v);
            else if (!strcasecmp(k, "FAV") && cfg_nfav < MAX_FAV && *v) snprintf(cfg_fav[cfg_nfav++], PATHLEN, "%.255s", v);
            else if (!strcasecmp(k, "DELAY")) cfg_delay = atoi(v);
            else if (!strcasecmp(k, "HDD")) cfg_hdd = atoi(v);
            else if (!strcasecmp(k, "USB_WAIT")) cfg_wait = atoi(v);
        }
        line = next;
    }
}

static int try_cnf(const char *path)
{
    static char text[8192];
    int n = read_file(path, text, sizeof(text) - 1);
    if (n < 0) return 0;
    text[n] = 0;
    snprintf(cfg_path, PATHLEN, "%.255s", path);
    parse_cnf(text);
    return 1;
}

/* MMBOOT.CNF next to the ELF: "mass:/APPS/MMBOOT.ELF" -> "mass:/APPS/MMBOOT.CNF" */
static int cnf_beside_self(char *out)
{
    char *slash;
    if (!self_path[0] || !strchr(self_path, ':')) return 0;
    if (strncmp(self_path, "mc", 2) && !is_mass(self_path)) return 0;
    snprintf(out, PATHLEN, "%s", self_path);
    slash = strrchr(out, '/');
    if (!slash) slash = strchr(out, ':');
    if ((size_t)(slash - out) + 12 >= PATHLEN) return 0;
    strcpy(slash + 1, "MMBOOT.CNF");
    return 1;
}

static int load_cnf(void)
{
    static const char *fixed[] = {"mc0:/MMBOOT/MMBOOT.CNF", "mc1:/MMBOOT/MMBOOT.CNF", "mc0:/PS2BBL/MMBOOT.CNF",
                                  "mc1:/PS2BBL/MMBOOT.CNF", "mass:/MMBOOT/MMBOOT.CNF", "mass:/PS2BBL/MMBOOT.CNF"};
    char here[PATHLEN];
    int i, t, pass;
    if (cnf_beside_self(here)) {
        if (is_mass(here)) {                         /* USB needs a moment to be mounted */
            for (t = 0; t < 20; t++) { if (try_cnf(here)) return 1; usleep(100000); }
        } else if (try_cnf(here)) return 1;
    }
    for (i = 0; i < 4; i++) if (try_cnf(fixed[i])) return 1;
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) load_hdd();
        for (t = 0; t < 10; t++) {
            for (i = 4; i < 6; i++) if (try_cnf(fixed[i])) return 1;
            usleep(200000);
        }
    }
    return 0;
}

static int save_cnf(void)
{
    static char text[8192];
    char path[PATHLEN];
    int n = 0, i;
    n += snprintf(text + n, sizeof(text) - n,
                  "# MMBOOT.CNF - written by MMBOOT " VERSION " (docs/MMBOOT.md)\n"
                  "# BOOT = auto-boot entry (fallbacks: more BOOT lines). No BOOT line = menu at every boot.\n"
                  "# FAV = favourites shown in the menu. mc?: = mc0/mc1, mass?: = any USB drive / exFAT HDD, BROWSER = PS2 browser\n");
    for (i = 0; i < cfg_nboot; i++) n += snprintf(text + n, sizeof(text) - n, "BOOT = %s\n", cfg_boot[i]);
    for (i = 0; i < cfg_nfav; i++) n += snprintf(text + n, sizeof(text) - n, "FAV = %s\n", cfg_fav[i]);
    n += snprintf(text + n, sizeof(text) - n, "HDD = %d\nUSB_WAIT = %d\nDELAY = %d\n", cfg_hdd, cfg_wait, cfg_delay);
    if (cfg_path[0] && write_file(cfg_path, text, n) == 0) return 0;
    if (cnf_beside_self(path) && write_file(path, text, n) == 0) goto ok;
    strcpy(path, "mc0:/PS2BBL/MMBOOT.CNF");
    if (write_file(path, text, n) == 0) goto ok;
    if (mc_is_ps2(0)) { mcMkDir(0, 0, "/MMBOOT"); mc_sync(); }
    strcpy(path, "mc0:/MMBOOT/MMBOOT.CNF");
    if (write_file(path, text, n) == 0) goto ok;
    return -1;
ok:
    strcpy(cfg_path, path);
    return 0;
}

static int fav_index(const char *p)
{
    int i;
    for (i = 0; i < cfg_nfav; i++) if (same_path(cfg_fav[i], p)) return i;
    return -1;
}

static int is_auto(const char *p) { return cfg_nboot > 0 && same_path(cfg_boot[0], p); }

/* ------------------------------------------------------------------ scan ---- */

static int has_elf_ext(const char *n)
{
    int l = strlen(n);
    return l > 4 && !strcasecmp(n + l - 4, ".ELF");
}

static void add_elf(const char *path)
{
    Elf *e;
    const char *b;
    int l;
    if (nelf >= MAXELF) return;
    e = &elfs[nelf++];
    snprintf(e->path, PATHLEN, "%.255s", path);
    b = base_name(path);
    l = strlen(b) - 4;
    if (l > 39) l = 39;
    if (l < 0) l = 0;
    memcpy(e->name, b, l);
    e->name[l] = 0;
}

static void scan_mc(int port)
{
    static sceMcTblGetDir tbl[64] __attribute__((aligned(64)));
    static char dirs[64][34];
    char pat[64], path[PATHLEN];
    int n, m, i, j, nd = 0;
    if (!mc_is_ps2(port)) return;
    mcGetDir(port, 0, "/*", 0, 64, tbl);
    n = mc_sync();
    for (i = 0; i < n; i++) {
        const char *nm = (const char *)tbl[i].EntryName;
        if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
        if (tbl[i].AttrFile & MC_ATTR_SUBDIR) { if (nd < 64) snprintf(dirs[nd++], 34, "%.32s", nm); }
        else if (has_elf_ext(nm)) { snprintf(path, PATHLEN, "mc%d:/%.32s", port, nm); add_elf(path); }
    }
    for (i = 0; i < nd; i++) {
        snprintf(pat, sizeof(pat), "/%.32s/*", dirs[i]);
        mcGetDir(port, 0, pat, 0, 64, tbl);
        m = mc_sync();
        for (j = 0; j < m; j++) {
            const char *nm = (const char *)tbl[j].EntryName;
            if (!(tbl[j].AttrFile & MC_ATTR_SUBDIR) && has_elf_ext(nm)) {
                snprintf(path, PATHLEN, "mc%d:/%.32s/%.32s", port, dirs[i], nm);
                add_elf(path);
            }
        }
    }
}

static int skip_dir(const char *n)
{
    static const char *skip[] = {"System Volume Information", "DVD", "CD", "ART", "VMC", "CFG", "THM", "CHT", "LNG",
                                 "PS2SAVES", "BAKMEGAMEMORYMC", NULL};
    int i;
    if (n[0] == '.' || n[0] == '$') return 1;
    for (i = 0; skip[i]; i++) if (!strcasecmp(n, skip[i])) return 1;
    return 0;
}

/* root, and 2 levels of folders: mass:/X.ELF, mass:/APPS/X.ELF, mass:/APPS/OPL/X.ELF */
static void scan_dir(const char *base, int depth)
{
    iox_dirent_t de;
    char (*sub)[128];
    char path[PATHLEN];
    int d, ns = 0, i;
    d = fileXioDopen(base);
    if (d < 0) return;
    sub = malloc(32 * 128);
    while (fileXioDread(d, &de) > 0) {
        if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
        if (FIO_S_ISDIR(de.stat.mode)) {
            if (depth < 2 && sub && ns < 32 && !skip_dir(de.name) && strlen(de.name) < 120) strcpy(sub[ns++], de.name);
        } else if (has_elf_ext(de.name)) {
            snprintf(path, PATHLEN, "%s%.200s", base, de.name);
            add_elf(path);
        }
    }
    fileXioDclose(d);
    for (i = 0; i < ns; i++) {
        snprintf(path, PATHLEN, "%s%.120s/", base, sub[i]);
        if (strlen(path) < 200) scan_dir(path, depth + 1);
    }
    free(sub);
}

static int cmp_elf(const void *a, const void *b) { return strcasecmp(((const Elf *)a)->path, ((const Elf *)b)->path); }

static void busy(const char *msg, const char *sub);

static void scan_all(void)
{
    char base[16];
    int u, t, d;
    nelf = 0;
    busy(T("Recherche des ELF...", "Looking for ELF files..."), T("Cartes mémoire", "Memory cards"));
    scan_mc(0);
    scan_mc(1);
    busy(T("Recherche des ELF...", "Looking for ELF files..."), T("USB / disque dur", "USB / hard disk"));
    for (t = 0; t < cfg_wait * 5; t++) {              /* give USB drives time to be mounted */
        d = fileXioDopen("mass0:/");
        if (d >= 0) { fileXioDclose(d); break; }
        usleep(200000);
    }
    for (u = 0; u < 5; u++) {
        snprintf(base, sizeof(base), "mass%d:/", u);
        scan_dir(base, 0);
    }
    qsort(elfs, nelf, sizeof(Elf), cmp_elf);
    scanned = 1;
}

/* ------------------------------------------------------------------- ui ---- */

static void say(u64 col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(toast, sizeof(toast), fmt, ap);
    va_end(ap);
    toast_col = col;
    toast_t = 150;
}

static void draw_header(const char *right)
{
    gfx_rect_grad(0, 0, SW, 44, RGBA(0x50, 0x30, 0x90, 0x60), RGBA(0x20, 0x10, 0x40, 0x40));
    gfx_text(24, 12, "MMBOOT", RGB(0xff, 0xc0, 0x40));
    gfx_text(110, 12, T("Lanceur + pilote carte Datel", "Launcher + Datel card driver"), RGB(0xe0, 0xe0, 0xf0));
    if (right) gfx_text_clip(SW - 24 - (gfx_text_w(right) < 200 ? gfx_text_w(right) : 200), 12, right, RGB(0x90, 0x98, 0xb0), 200);
}

/* resident driver counters, to check on hardware what happened since power-on */
static void draw_driver_line(float y)
{
    char b[200];
    const char *last;
    if (!drv_active) {
        snprintf(b, sizeof(b), "%s", T("Pilote : pas encore installé depuis l'allumage", "Driver: not installed since power-on"));
    } else {
        if (!drv_st[3] && !drv_st[6] && drv_st[4] != 1) last = T("aucun", "none");
        else if (drv_st[4] == 0) last = "OK";
        else if (drv_st[4] == 1) last = T("SECRMAN introuvable", "SECRMAN not found");
        else last = T("refusé", "refused");
        snprintf(b, sizeof(b), T("Pilote actif : prog %u  stubs %u  reset %u/%u  patchs %u  dernier %s",
                                 "Driver active: prog %u  stubs %u  reset %u/%u  patches %u  last %s"),
                 drv_st[0], drv_st[1], drv_st[2], drv_st[7], drv_st[3], last);
    }
    gfx_text_clip(24, y, b, drv_active ? RGB(0x90, 0xe0, 0x90) : RGB(0x90, 0x98, 0xb0), SW - 48);
}

static void draw_toast(void)
{
    int w;
    if (toast_t <= 0) return;
    toast_t--;
    w = gfx_text_w(toast) + 30;
    gfx_rect((SW - w) / 2, 388, w, 28, RGBA(0, 0, 0, 0x70));
    gfx_text((SW - w) / 2 + 15, 392, toast, toast_col);
}

static void busy(const char *msg, const char *sub)
{
    int i;
    for (i = 0; i < 2; i++) {          /* both buffers */
        gfx_begin();
        draw_header(NULL);
        gfx_rect(120, 180, SW - 240, 90, RGBA(0, 0, 0, 0x60));
        gfx_rect(120, 180, SW - 240, 3, RGB(0xff, 0xc0, 0x40));
        gfx_text(140, 198, msg, RGB(0xff, 0xff, 0xff));
        if (sub) gfx_text_clip(140, 228, sub, RGB(0xa0, 0xb0, 0xd0), SW - 290);
        gfx_end();
    }
}

/* modal yes/no; returns 1 on X */
static int ask(const char *msg, const char *sub)
{
    unsigned int e;
    while (1) {
        e = pad_edge();
        if (e & PAD_CROSS) return 1;
        if (e & (PAD_CIRCLE | PAD_TRIANGLE)) return 0;
        gfx_begin();
        draw_header(NULL);
        gfx_rect(80, 160, SW - 160, 130, RGBA(0x10, 0x10, 0x28, 0x80));
        gfx_rect(80, 160, SW - 160, 3, RGB(0xff, 0x70, 0x70));
        gfx_text(100, 176, msg, RGB(0xff, 0xff, 0xff));
        if (sub) gfx_text_clip(100, 206, sub, RGB(0xa0, 0xb0, 0xd0), SW - 200);
        gfx_text(100, 252, T("X oui     O non", "X yes     O no"), RGB(0xff, 0xc0, 0x40));
        gfx_end();
    }
}

static void go_browser(void)
{
    static char *args[] = {"SkipHdd", "BootBrowser", "SkipMc"};
    SifExitRpc();
    LoadExecPS2("rom0:OSDSYS", 3, args);
}

/* installs the driver and starts the program; only returns on failure */
static void launch(const char *entry)
{
    char target[PATHLEN], sub[200];
    ResInfo ri;
    int r;
    if (!resolve(entry, target)) {
        say(RGB(0xff, 0x70, 0x70), T("Introuvable : %s", "Not found: %s"), entry);
        return;
    }
    busy(T("Installation du pilote...", "Installing the driver..."), target);
    r = res_install(&ri);
    if (r != 0) {
        snprintf(sub, sizeof(sub), "err %d  ExecPS2 %08x hook +%d tail %d clear %d", r, ri.execps2, ri.hookoff, ri.taillen, ri.clearpatch);
        if (!ask(T("Pilote NON installé : lancer quand même ?", "Driver NOT installed: launch anyway?"), sub)) return;
    }
    busy(T("Lancement...", "Starting..."), !strcmp(target, "BROWSER") ? T("Navigateur PS2", "PS2 browser") : target);
    if (pad_ok) { padPortClose(0, 0); pad_ok = 0; }
    if (!strcmp(target, "BROWSER")) go_browser();
    LoadELFFromFile(target, 0, NULL);          /* only returns on failure */
    pad_open();
    say(RGB(0xff, 0x70, 0x70), T("Échec du lancement : %s", "Could not start: %s"), target);
}

/* countdown before the auto-boot entry. 0 = launch, 1 = menu, 2 = browser */
static int countdown(const char *target)
{
    char line[PATHLEN + 64];
    unsigned int held, e;
    int f, frames = cfg_delay * 50;
    const char *nm = !strcmp(target, "BROWSER") ? T("Navigateur PS2", "PS2 browser") : base_name(target);
    prevpad = pad_read();          /* a button still held from the boot manager's hotkey is not a press */
    for (f = 0; f < frames || f == 0; f++) {
        held = pad_read();
        e = held & ~prevpad;
        prevpad = held;
        if (held & PAD_R1) return 2;
        if (e) return 1;
        gfx_begin();
        draw_header("v" VERSION);
        gfx_rect(80, 150, SW - 160, 150, RGBA(0, 0, 0, 0x50));
        gfx_rect(80, 150, SW - 160, 3, RGB(0xff, 0xc0, 0x40));
        snprintf(line, sizeof(line), T("Lancement de %s dans %d s", "Starting %s in %d s"), nm, (frames - f + 49) / 50);
        gfx_text_clip(100, 168, line, RGB(0xff, 0xff, 0xff), SW - 200);
        gfx_text_clip(100, 196, target, RGB(0xa0, 0xb0, 0xd0), SW - 200);
        gfx_rect(100, 228, (SW - 200) * (frames - f) / (frames ? frames : 1), 4, RGB(0xff, 0xc0, 0x40));
        gfx_text(100, 250, T("Une touche : menu      R1 : navigateur PS2", "Any button: menu      R1: PS2 browser"), RGB(0xc0, 0xc8, 0xe0));
        draw_driver_line(400);
        gfx_end();
    }
    return 0;
}

/* menu: tab 0 = favourites (+ PS2 browser), tab 1 = every ELF found */
static void menu(void)
{
    static int tab = -1, sel[2], top[2];
    static char favok[MAX_FAV];
    int favchk = 0, rows = 7, rh = 42, i, n, s;
    unsigned int e;
    char buf[PATHLEN + 32], cp[PATHLEN];
    const char *path, *name;

    if (!scanned) scan_all();
    if (tab < 0) tab = cfg_nfav ? 0 : 1;
    while (1) {
        if (!favchk) {                                /* which favourites exist right now */
            for (i = 0; i < cfg_nfav; i++) favok[i] = !strcasecmp(cfg_fav[i], "BROWSER") || resolve(cfg_fav[i], cp);
            favchk = 1;
        }
        n = tab == 0 ? cfg_nfav + 1 : nelf;
        if (sel[tab] >= n) sel[tab] = n ? n - 1 : 0;
        s = sel[tab];
        path = NULL;
        if (tab == 0) path = s == 0 ? "BROWSER" : cfg_fav[s - 1];
        else if (nelf) path = elfs[s].path;

        e = pad_edge();
        if (e & PAD_UP && n) sel[tab] = (s + n - 1) % n;
        if (e & PAD_DOWN && n) sel[tab] = (s + 1) % n;
        if (e & PAD_LEFT && n) { sel[tab] -= rows; if (sel[tab] < 0) sel[tab] = 0; }
        if (e & PAD_RIGHT && n) { sel[tab] += rows; if (sel[tab] >= n) sel[tab] = n - 1; }
        if (e & (PAD_L1 | PAD_R1)) tab ^= 1;
        if (e & PAD_SELECT) { scan_all(); favchk = 0; say(RGB(0xc0, 0xd0, 0xff), T("%d ELF trouvés", "%d ELF files found"), nelf); }
        if (e & (PAD_CROSS | PAD_START) && path) launch(path);
        if (e & PAD_TRIANGLE && path && strcmp(path, "BROWSER")) {
            int k = fav_index(path);
            canonical(path, cp);
            if (k >= 0) {
                memmove(cfg_fav[k], cfg_fav[k + 1], (cfg_nfav - k - 1) * PATHLEN);
                cfg_nfav--;
                if (tab == 0 && sel[0] > cfg_nfav) sel[0] = cfg_nfav;
            } else if (cfg_nfav < MAX_FAV) strcpy(cfg_fav[cfg_nfav++], cp);
            favchk = 0;
            if (save_cnf() == 0) say(RGB(0xa0, 0xff, 0xa0), k >= 0 ? T("Retiré des favoris", "Removed from favourites") : T("Ajouté aux favoris", "Added to favourites"));
            else say(RGB(0xff, 0x70, 0x70), T("Impossible d'écrire MMBOOT.CNF", "Could not write MMBOOT.CNF"));
        }
        if (e & PAD_SQUARE && path) {
            int was = is_auto(path);
            canonical(path, cp);
            if (was) cfg_nboot = 0;
            else { strcpy(cfg_boot[0], cp); cfg_nboot = 1; }
            if (save_cnf() == 0) say(RGB(0xa0, 0xff, 0xa0), was ? T("Démarrage auto désactivé : menu à chaque démarrage", "Auto-boot off: menu at every boot")
                                                              : T("Démarrage auto : %s", "Auto-boot: %s"), base_name(cp));
            else say(RGB(0xff, 0x70, 0x70), T("Impossible d'écrire MMBOOT.CNF", "Could not write MMBOOT.CNF"));
        }

        /* ---- draw */
        n = tab == 0 ? cfg_nfav + 1 : nelf;
        if (sel[tab] >= n) sel[tab] = n ? n - 1 : 0;
        s = sel[tab];
        if (s < top[tab]) top[tab] = s;
        if (s >= top[tab] + rows) top[tab] = s - rows + 1;
        if (top[tab] > 0 && top[tab] + rows > n) top[tab] = n > rows ? n - rows : 0;
        gfx_begin();
        draw_header(cfg_nboot ? base_name(cfg_boot[0]) : T("pas d'auto", "no auto-boot"));
        {
            const char *tn[2] = {T("Favoris", "Favourites"), NULL};
            char all[48];
            int x = 24, w, k;
            snprintf(all, sizeof(all), T("Tous les ELF (%d)", "All ELF files (%d)"), nelf);
            tn[1] = all;
            for (k = 0; k < 2; k++) {
                w = gfx_text_w(tn[k]) + 20;
                if (k == tab) gfx_rect_grad(x, 54, w, 26, RGBA(0xff, 0xc0, 0x40, 0x70), RGBA(0x90, 0x60, 0x10, 0x60));
                else gfx_rect(x, 54, w, 26, RGBA(0xff, 0xff, 0xff, 0x14));
                gfx_text(x + 10, 58, tn[k], k == tab ? RGB(0xff, 0xff, 0xff) : RGB(0xa0, 0xa8, 0xc0));
                x += w + 8;
            }
        }
        if (!n) gfx_text(40, 110, T("Aucun ELF trouvé (SELECT pour relire)", "No ELF found (SELECT to rescan)"), RGB(0x90, 0x98, 0xb0));
        for (i = 0; i < rows && top[tab] + i < n; i++) {
            int idx = top[tab] + i, y = 92 + i * rh, ok = 1, badge_x = SW - 30;
            if (tab == 0) {
                path = idx == 0 ? "BROWSER" : cfg_fav[idx - 1];
                name = idx == 0 ? T("Navigateur PS2", "PS2 browser") : base_name(path);
                if (idx > 0) ok = favok[idx - 1];
            } else { path = elfs[idx].path; name = elfs[idx].name; }
            if (idx == s) gfx_rect_grad(24, y - 2, SW - 48, rh - 2, RGBA(0x70, 0x90, 0xff, 0x70), RGBA(0x30, 0x40, 0x90, 0x70));
            if (tab == 0 && idx > 0) {                       /* "OPNPS2LD.ELF" -> "OPNPS2LD" */
                snprintf(buf, sizeof(buf), "%s", name);
                if (has_elf_ext(buf)) buf[strlen(buf) - 4] = 0;
                name = buf;
            }
            gfx_text_clip(34, y, name, !ok ? RGB(0x80, 0x80, 0x90) : idx == s ? RGB(0xff, 0xff, 0xff) : RGB(0xd8, 0xdc, 0xe8), SW - 220);
            if (strcmp(path, "BROWSER")) {
                snprintf(cp, sizeof(cp), "%.200s%s", path, ok ? "" : T("   (absent)", "   (missing)"));
                gfx_text_clip(34, y + 17, cp, RGB(0x90, 0x9c, 0xc0), SW - 200);
            }
            if (is_auto(path)) { badge_x -= gfx_text_w("AUTO"); gfx_text(badge_x, y + 8, "AUTO", RGB(0xff, 0xc0, 0x40)); badge_x -= 12; }
            if (tab == 1 && fav_index(path) >= 0) { badge_x -= gfx_text_w("FAV"); gfx_text(badge_x, y + 8, "FAV", RGB(0x90, 0xe0, 0xff)); }
        }
        if (n > rows) {
            int h = rows * rh * rows / n, y = 92 + (rows * rh - h) * top[tab] / (n - rows);
            gfx_rect(SW - 20, 92, 3, rows * rh, RGBA(0xff, 0xff, 0xff, 0x20));
            gfx_rect(SW - 20, y, 3, h, RGBA(0xff, 0xff, 0xff, 0x70));
        }
        draw_driver_line(400);
        gfx_rect(0, 424, SW, 24, RGBA(0, 0, 0, 0x40));
        gfx_text(24, 426, T("X lancer   /\\ favori   [] auto   L1/R1 onglet   SELECT relire",
                            "X launch   /\\ favourite   [] auto-boot   L1/R1 tab   SELECT rescan"), RGB(0xa0, 0xa8, 0xc0));
        draw_toast();
        gfx_end();
    }
}

/* ----------------------------------------------------------------- main ---- */

int main(int argc, char *argv[])
{
    char target[PATHLEN];
    int i, t, found = 0, r;

    g_fr = (configGetLanguage() == LANGUAGE_FRENCH);
    if (argc > 0 && argv[0]) snprintf(self_path, PATHLEN, "%.255s", argv[0]);
    elfs = malloc(sizeof(Elf) * MAXELF);
    gfx_init();
    busy(T("Démarrage...", "Starting..."), T("Modules, pilote carte, USB", "Modules, card driver, USB"));
    iop_init();
    drv_active = res_read_status(drv_st);
    load_cnf();
    if (cfg_hdd) load_hdd();

    if (cfg_nboot) {
        busy(T("Démarrage...", "Starting..."), T("Recherche de l'entrée auto", "Looking for the auto-boot entry"));
        for (t = 0; t < cfg_wait * 5 + 1 && !found; t++) {      /* USB/HDD mount asynchronously */
            for (i = 0; i < cfg_nboot && !found; i++) found = resolve(cfg_boot[i], target);
            if (!found) usleep(200000);
        }
        if (found) {
            r = countdown(target);
            if (r == 0) launch(target);
            else if (r == 2) launch("BROWSER");
        } else say(RGB(0xff, 0xa0, 0x60), T("Entrée auto introuvable : %s", "Auto-boot entry not found: %s"), cfg_boot[0]);
    }
    menu();
    return 0;
}
