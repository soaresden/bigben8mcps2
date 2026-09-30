/*
 * XMEM2 - Gestionnaire de cartes memoire PS2 (remplacant du CD Datel MegaMem)
 * par Soaresden. Cartes Datel sans MagicGate reconnues grace a SECRPATCH.
 */
#define NEWLIB_PORT_AWARE
#include <kernel.h>
#include <libpad.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "gfx.h"
#include "sys.h"
#include "save.h"
#include "icon.h"
#include "mcio.h"
#include "usbio.h"
#include "datel.h"
#include <osd_config.h>

#define VERSION "1.3"
int g_fr = 1;                 /* langue de l'interface : suit la langue de la console */
#define T(fr, en) (g_fr ? (fr) : (en))
enum { T_SLOT1, T_SLOT2, T_ARC1, T_ARC2, T_USB, NTABS };
static const char *tabname[NTABS] = {"Slot 1", "Slot 2", "Archives 1", "Archives 2", "USB"};

#define MAXENT 200
typedef struct { Entry e[MAXENT]; int n, sel, top, loaded, err, nmark; char mark[MAXENT]; } List;
static List L[NTABS];
static int tab;
static int ctype[2], cfreekb[2], cfmt[2], usbok;
static char toast[160];
static int toast_t;
static u64 toast_col;
static Icon icon;
static int icon_ok, icon_for_tab = -1, icon_for_sel = -1, idle;
static float angle, tsec;

/* ------------------------------------------------------------ utilitaires */
static const char *mcerr(int r)
{
    static char b[40];
    switch (r) {
    case -2: return T("carte non formatée", "card not formatted");
    case -3: return T("carte pleine", "card full");
    case -4: return T("introuvable", "not found");
    case -5: return T("accès refusé", "access denied");
    case -11: case -12: case -13: return T("carte non détectée", "card not detected");
    case -90: return T("authentification refusée", "authentication refused");
    }
    snprintf(b, sizeof(b), T("erreur %d", "error %d"), r);
    return b;
}

static void say(u64 col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(toast, sizeof(toast), fmt, ap);
    va_end(ap);
    toast_col = col;
    toast_t = 240;
}

static int port_of(int t) { return (t == T_SLOT2 || t == T_ARC2) ? 1 : 0; }

static void kb_str(unsigned int bytes, char *o) { sprintf(o, T("%u Ko", "%u KB"), (bytes + 1023) / 1024); }

static void draw_frame(void);

static void busy(const char *msg, const char *sub)
{
    gfx_begin();
    draw_frame();
    gfx_rect(120, 180, SW - 240, 90, RGBA(0, 0, 0, 0x60));
    gfx_rect(120, 180, SW - 240, 3, RGB(0xff, 0xc0, 0x40));
    gfx_text(140, 198, msg, RGB(0xff, 0xff, 0xff));
    if (sub) gfx_text_clip(140, 228, sub, RGB(0xa0, 0xb0, 0xd0), SW - 290);
    gfx_end();
}

static void progress(const char *what, int done, int total)
{
    char b[96];
    snprintf(b, sizeof(b), T("Fichier %d / %d : %s", "File %d / %d: %s"), done + 1, total, what);
    busy(T("Écriture sur la carte...", "Writing to card..."), b);
}

/* ------------------------------------------------------------ listes */
static void refresh_info(void)
{
    int p;
    for (p = 0; p < 2; p++) mcx_info(p, &ctype[p], &cfreekb[p], &cfmt[p]);
}

static void load_tab(int t)
{
    List *l = &L[t];
    int p = port_of(t), keep = l->sel;
    l->n = 0; l->err = 0;
    memset(l->mark, 0, sizeof(l->mark));
    l->nmark = 0;
    if (t == T_USB) {
        usbok = usb_ready();
        if (usbok) l->n = usb_list(l->e, MAXENT);
        if (l->n < 0) { l->err = l->n; l->n = 0; }
    } else {
        mcx_info(p, &ctype[p], &cfreekb[p], &cfmt[p]);
        if (ctype[p] == 2 && cfmt[p]) {
            l->n = (t <= T_SLOT2) ? mcx_list(p, l->e, MAXENT) : mcx_list_archives(p, l->e, MAXENT);
            if (l->n < 0) { l->err = l->n; l->n = 0; }
        }
    }
    l->loaded = 1;
    l->sel = keep < l->n ? keep : (l->n ? l->n - 1 : 0);
    if (l->sel < 0) l->sel = 0;
    if (l->top > l->sel) l->top = l->sel;
    icon_for_tab = -1;
}

static void invalidate_port(int p)
{
    L[p ? T_SLOT2 : T_SLOT1].loaded = 0;
    L[p ? T_ARC2 : T_ARC1].loaded = 0;
}

/* ------------------------------------------------------------ icone de la selection */
static int icon_from_iconsys_and_reader(const unsigned char *is, unsigned int islen,
                                        int (*rd)(const char *, unsigned char **, unsigned int *))
{
    char vn[64];
    unsigned char *ico;
    unsigned int ilen;
    if (iconsys_viewicon(is, islen, vn, sizeof(vn))) return -1;
    if (rd(vn, &ico, &ilen)) return -1;
    icon_free(&icon);
    icon_ok = icon_parse(ico, ilen, &icon) == 0;
    free(ico);
    gfx_icon_invalidate();
    return icon_ok ? 0 : -1;
}

static int cur_port; static const char *cur_dir;
static int rd_slot(const char *n, unsigned char **b, unsigned int *l)
{
    char p[80];
    snprintf(p, sizeof(p), "/%s/%s", cur_dir, n);
    return mcx_read_file(cur_port, p, b, l);
}
static const char *cur_file;
static int rd_usb(const char *n, unsigned char **b, unsigned int *l) { return usb_read_psu_file(cur_file, n, b, l); }
static Save cur_arc;
static int rd_arc(const char *n, unsigned char **b, unsigned int *l)
{
    int i = save_find(&cur_arc, n);
    if (i < 0) return -1;
    *b = malloc(cur_arc.f[i].size);
    memcpy(*b, cur_arc.f[i].data, cur_arc.f[i].size);
    *l = cur_arc.f[i].size;
    return 0;
}

static void load_icon(void)
{
    List *l = &L[tab];
    Entry *e;
    unsigned char *is = NULL, *buf;
    unsigned int islen, blen;
    icon_ok = 0;
    icon_for_tab = tab; icon_for_sel = l->sel;
    if (!l->n) return;
    e = &l->e[l->sel];
    if (tab <= T_SLOT2) {
        char p[80];
        cur_port = port_of(tab); cur_dir = e->dir;
        snprintf(p, sizeof(p), "/%s/icon.sys", e->dir);
        if (mcx_read_file(cur_port, p, &is, &islen) == 0) icon_from_iconsys_and_reader(is, islen, rd_slot);
    } else if (tab == T_USB) {
        cur_file = e->file;
        if (usb_read_psu_file(e->file, "icon.sys", &is, &islen) == 0) icon_from_iconsys_and_reader(is, islen, rd_usb);
    } else {
        char p[80];
        snprintf(p, sizeof(p), "/" XM_ARCDIR "/%s", e->dir);
        if (mcx_read_file(port_of(tab), p, &buf, &blen) == 0) {
            save_free(&cur_arc);
            if (arc_extract(buf, blen, &cur_arc) == 0) {
                int i = save_find(&cur_arc, "icon.sys");
                if (i >= 0) icon_from_iconsys_and_reader(cur_arc.f[i].data, cur_arc.f[i].size, rd_arc);
            }
            save_free(&cur_arc);
            free(buf);
        }
    }
    free(is);
}

/* ------------------------------------------------------------ dessin */
static void draw_list(void)
{
    List *l = &L[tab];
    int i, rows = 9, y0 = 96, rh = 36, p = port_of(tab);
    char b[64];
    if (!l->n) {
        const char *m = T("Aucune sauvegarde", "No save");
        if (tab == T_USB && !usbok) m = T("Clé USB non détectée (FAT32/exFAT, port avant)", "USB drive not found (FAT32/exFAT, front port)");
        else if (tab != T_USB && ctype[p] == 0) m = T("Pas de carte dans ce slot", "No card in this slot");
        else if (tab != T_USB && ctype[p] == 1) m = T("Carte PS1 : non gérée", "PS1 card: not supported");
        else if (tab != T_USB && !cfmt[p]) m = T("Carte non formatée", "Unformatted card");
        else if (tab == T_USB) m = T("Aucun .psu dans mass:/PS2SAVES", "No .psu in mass:/PS2SAVES");
        else if (tab >= T_ARC1) m = T("Aucune archive sur cette carte", "No archive on this card");
        gfx_text(40, y0 + 20, m, RGB(0x90, 0x98, 0xb0));
        return;
    }
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + rows) l->top = l->sel - rows + 1;
    for (i = 0; i < rows && l->top + i < l->n; i++) {
        Entry *e = &l->e[l->top + i];
        int y = y0 + i * rh, s = (l->top + i == l->sel);
        const char *line2 = e->t2[0] ? e->t2 : e->dir;
        while (*line2 == ' ') line2++;
        if (s) gfx_rect_grad(24, y - 2, 356, rh - 2, RGBA(0x70, 0x90, 0xff, 0x70), RGBA(0x30, 0x40, 0x90, 0x70));
        if (l->mark[l->top + i]) {
            gfx_rect(24, y - 2, 356, rh - 2, RGBA(0xff, 0xc0, 0x40, 0x28));
            gfx_rect(25, y + 2, 5, rh - 10, RGB(0xff, 0xc0, 0x40));
        }
        gfx_text_clip(34, y, e->t1, s ? RGB(0xff, 0xff, 0xff) : RGB(0xd8, 0xdc, 0xe8), 250);
        gfx_text_clip(34, y + 16, line2, RGB(0x90, 0x9c, 0xc0), 250);
        kb_str(e->size, b);
        gfx_text(370 - gfx_text_w(b), y + 8, b, RGB(0xc0, 0xc8, 0xe0));
    }
    if (l->n > rows) {           /* ascenseur */
        float h = 9.0f * rh * rows / l->n, y = y0 + 9.0f * rh * l->top / l->n;
        gfx_rect(384, y0, 3, rows * rh, RGBA(0xff, 0xff, 0xff, 0x20));
        gfx_rect(384, y, 3, h, RGBA(0xff, 0xff, 0xff, 0x70));
    }
}

static void draw_details(void)
{
    List *l = &L[tab];
    Entry *e;
    char b[96];
    int x = 408, y = 300;
    gfx_rect(400, 90, SW - 420, 330, RGBA(0, 0, 0, 0x28));
    if (!l->n) return;
    e = &l->e[l->sel];
    if (icon_ok && icon_for_tab == tab && icon_for_sel == l->sel) gfx_icon(&icon, 400 + (SW - 420) / 2, 185, 150, angle, tsec);
    gfx_text_clip(x, y, e->t1, RGB(0xff, 0xff, 0xff), SW - 430);
    if (e->t2[0]) gfx_text_clip(x, y + 18, e->t2, RGB(0xff, 0xff, 0xff), SW - 430);
    gfx_text_clip(x, y + 42, e->dir, RGB(0xa0, 0xb0, 0xd0), SW - 430);
    if (tab == T_ARC1 || tab == T_ARC2) {
        snprintf(b, sizeof(b), T("%u Ko -> %u Ko (%d%%)", "%u KB -> %u KB (%d%%)"), (e->rawsize + 1023) / 1024, (e->size + 1023) / 1024,
                 e->rawsize ? (int)(100.0 * e->size / e->rawsize) : 100);
    } else {
        kb_str(e->size, b);
    }
    gfx_text(x, y + 62, b, RGB(0xc0, 0xc8, 0xe0));
    if (tab <= T_SLOT2 && !strcmp(e->dir, DATEL_DIR)) {
        gfx_text_clip(x, y + 98, T("Archive Datel : X pour l'ouvrir", "Datel archive: X to open it"), RGB(0xff, 0xc0, 0x40), SW - 430);
    }
    if (e->modified.year) {
        snprintf(b, sizeof(b), "%02d/%02d/%04d %02d:%02d", e->modified.day, e->modified.month, e->modified.year,
                 e->modified.hour, e->modified.min);
        gfx_text(x, y + 80, b, RGB(0x90, 0x9c, 0xc0));
    }
}

static void draw_frame(void)
{
    int i, x = 24;
    char b[128];
    gfx_rect_grad(0, 0, SW, 44, RGBA(0x50, 0x30, 0x90, 0x60), RGBA(0x20, 0x10, 0x40, 0x40));
    gfx_text(24, 12, "XMEM2", RGB(0xff, 0xc0, 0x40));
    gfx_text(90, 12, T("Gestionnaire de cartes mémoire", "Memory card manager"), RGB(0xe0, 0xe0, 0xf0));
    for (i = 0; i < NTABS; i++) {
        int w = gfx_text_w(tabname[i]) + 20;
        if (i == tab) gfx_rect_grad(x, 54, w, 26, RGBA(0xff, 0xc0, 0x40, 0x70), RGBA(0x90, 0x60, 0x10, 0x60));
        else gfx_rect(x, 54, w, 26, RGBA(0xff, 0xff, 0xff, 0x14));
        gfx_text(x + 10, 58, tabname[i], i == tab ? RGB(0xff, 0xff, 0xff) : RGB(0xa0, 0xa8, 0xc0));
        x += w + 6;
    }
    draw_list();
    draw_details();
    gfx_rect(0, 428, SW, 20, RGBA(0, 0, 0, 0x40));
    if (L[tab].nmark) {
        snprintf(b, sizeof(b), T("%d cochée(s) : X actions  SELECT aucune", "%d ticked: X actions  SELECT none"), L[tab].nmark);
        gfx_text(24, 429, b, RGB(0xff, 0xc0, 0x40));
    } else gfx_text(24, 429, T("X actions  [] cocher  SELECT tout  START menu", "X actions  [] tick  SELECT all  START menu"), RGB(0xa0, 0xa8, 0xc0));
    for (i = 0; i < 2; i++) {
        if (ctype[i] == 2 && cfmt[i]) snprintf(b, sizeof(b), T("S%d %d Ko", "S%d %d KB"), i + 1, cfreekb[i]);
        else snprintf(b, sizeof(b), "S%d %s", i + 1, ctype[i] == 2 ? T("non formatée", "unformatted") : (ctype[i] == 1 ? "PS1" : T("vide", "empty")));
        gfx_text(SW - 230 + i * 110, 429, b, RGB(0xb0, 0xc0, 0xe0));
    }
    if (toast_t > 0) {
        int w = gfx_text_w(toast) + 30;
        gfx_rect((SW - w) / 2, 388, w, 28, RGBA(0, 0, 0, 0x70));
        gfx_text((SW - w) / 2 + 15, 392, toast, toast_col);
    }
}

/* ------------------------------------------------------------ menus modaux */
static unsigned int prevpad;
static int rep;
static unsigned int pad_edge(void)
{
    unsigned int p = pad_read(), e = p & ~prevpad;
    if (p & (PAD_UP | PAD_DOWN)) {
        if (++rep > 18 && (rep % 4) == 0) e |= p & (PAD_UP | PAD_DOWN);
    } else rep = 0;
    prevpad = p;
    return e;
}

static int menu(const char *title, const char **items, int n)
{
    int sel = 0, i;
    while (1) {
        unsigned int e = pad_edge();
        int h = 50 + n * 26, y0 = 224 - h / 2, w = 380;
        angle += 0.02f; tsec += 1.0f / 50;
        gfx_begin();
        draw_frame();
        gfx_rect(0, 0, SW, 448, RGBA(0, 0, 0, 0x30));
        gfx_rect((SW - w) / 2, y0, w, h, RGBA(0x10, 0x10, 0x28, 0x80));
        gfx_rect((SW - w) / 2, y0, w, 3, RGB(0xff, 0xc0, 0x40));
        gfx_text((SW - w) / 2 + 16, y0 + 10, title, RGB(0xff, 0xc0, 0x40));
        for (i = 0; i < n; i++) {
            int y = y0 + 40 + i * 26;
            if (i == sel) gfx_rect((SW - w) / 2 + 8, y - 2, w - 16, 24, RGBA(0x70, 0x90, 0xff, 0x60));
            gfx_text((SW - w) / 2 + 20, y, items[i], i == sel ? RGB(0xff, 0xff, 0xff) : RGB(0xc0, 0xc8, 0xe0));
        }
        gfx_end();
        if (e & PAD_UP) sel = (sel + n - 1) % n;
        if (e & PAD_DOWN) sel = (sel + 1) % n;
        if (e & PAD_CROSS) return sel;
        if (e & (PAD_CIRCLE | PAD_TRIANGLE)) return -1;
    }
}

static int confirm(const char *q)
{
    const char *it[] = {T("Oui", "Yes"), T("Non", "No")};
    return menu(q, it, 2) == 0;
}

/* "replace?" during a batch: Yes / No / Yes to all / No to all */
static int batch_n, batch_replace = -1;
static int ask_replace(const char *q)
{
    const char *it[4];
    int c;
    if (batch_n <= 1) return confirm(q);
    if (batch_replace >= 0) return batch_replace;
    it[0] = T("Oui", "Yes"); it[1] = T("Non", "No"); it[2] = T("Oui pour tout", "Yes to all"); it[3] = T("Non pour tout", "No to all");
    c = menu(q, it, 4);
    if (c == 2) batch_replace = 1;
    if (c == 3) batch_replace = 0;
    return c == 0 || c == 2;
}

/* ------------------------------------------------------------ operations */
static int load_source(int t, Entry *e, Save *s)
{
    int r;
    unsigned char *b;
    unsigned int n;
    if (t <= T_SLOT2) {
        busy(T("Lecture de la sauvegarde...", "Reading save..."), e->dir);
        r = mcx_load_save(port_of(t), e->dir, s);
    } else if (t == T_USB) {
        busy(T("Lecture sur la clé USB...", "Reading USB drive..."), e->file);
        r = usb_read_all(e->file, &b, &n);
        if (r == 0) { r = psu_parse(b, n, s); free(b); }
    } else {
        char p[80];
        busy(T("Décompression de l'archive...", "Decompressing archive..."), e->dir);
        snprintf(p, sizeof(p), "/" XM_ARCDIR "/%s", e->dir);
        r = mcx_read_file(port_of(t), p, &b, &n);
        if (r == 0) { r = arc_extract(b, n, s); free(b); if (r) r = -300 + r; }
    }
    return r;
}

static int write_to_slot(int p, Save *s)
{
    int r, need = save_needed_kb(s), avail;
    mcx_info(p, &ctype[p], &cfreekb[p], &cfmt[p]);
    if (ctype[p] != 2 || !cfmt[p]) { say(RGB(0xff, 0x70, 0x70), T("Slot %d : pas de carte PS2 formatée", "Slot %d: no formatted PS2 card"), p + 1); return -1; }
    avail = cfreekb[p];
    if (mcx_exists(p, s->dir)) {
        if (!ask_replace(T("Déjà présent sur la carte : remplacer ?", "Already on the card: replace?"))) return -1;
        {
            Save old;
            if (mcx_load_save(p, s->dir, &old) == 0) { avail += save_needed_kb(&old); save_free(&old); }
        }
    }
    if (need > avail) { say(RGB(0xff, 0x70, 0x70), T("Slot %d : place insuffisante (%d Ko requis, %d libres)", "Slot %d: not enough space (%d KB needed, %d free)"), p + 1, need, avail); return -1; }
    r = mcx_write_save(p, s, progress);
    invalidate_port(p);
    if (r < 0) say(RGB(0xff, 0x70, 0x70), T("Échec d'écriture slot %d : %s", "Write to slot %d failed: %s"), p + 1, mcerr(r));
    return r;
}

static int write_to_archive(int p, Save *s)
{
    int r, i;
    unsigned char *b;
    uint32_t n;
    mcx_info(p, &ctype[p], &cfreekb[p], &cfmt[p]);
    if (ctype[p] != 2 || !cfmt[p]) { say(RGB(0xff, 0x70, 0x70), T("Slot %d : pas de carte PS2 formatée", "Slot %d: no formatted PS2 card"), p + 1); return -1; }
    busy(T("Compression...", "Compressing..."), s->dir);
    i = save_find(s, "icon.sys");
    if (arc_build(s, i >= 0 ? s->f[i].data : NULL, i >= 0 ? s->f[i].size : 0, &b, &n)) { say(RGB(0xff, 0x70, 0x70), T("Échec de compression", "Compression failed")); return -1; }
    if (mcx_archive_exists(p, s->dir) && !ask_replace(T("Archive déjà présente : remplacer ?", "Archive already exists: replace?"))) { free(b); return -1; }
    if ((int)((n + 1023) / 1024 + 40) > cfreekb[p]) { free(b); say(RGB(0xff, 0x70, 0x70), T("Slot %d : place insuffisante", "Slot %d: not enough space"), p + 1); return -1; }
    busy(T("Écriture de l'archive...", "Writing archive..."), s->dir);
    r = mcx_write_archive(p, s->dir, b, n);
    free(b);
    invalidate_port(p);
    if (r < 0) say(RGB(0xff, 0x70, 0x70), T("Échec d'écriture de l'archive : %s", "Archive write failed: %s"), mcerr(r));
    return r;
}

static int write_to_usb(Save *s)
{
    unsigned char *b;
    uint32_t n;
    char fn[64];
    int r;
    if (!usb_ready()) { say(RGB(0xff, 0x70, 0x70), T("Clé USB non détectée", "USB drive not found")); return -1; }
    usb_safe_name(s->dir, fn);
    if (usb_exists(fn) && !ask_replace(T("Déjà présent sur la clé : remplacer ?", "Already on the USB drive: replace?"))) return -1;
    if (psu_build(s, &b, &n)) return -1;
    busy(T("Export vers la clé USB...", "Exporting to USB..."), fn);
    r = usb_write(fn, b, n);
    free(b);
    L[T_USB].loaded = 0;
    if (r < 0) say(RGB(0xff, 0x70, 0x70), T("Échec d'écriture sur la clé USB", "USB write failed"));
    return r;
}

/* ------------------------------------------------------------ archives Datel (X-MEM) */
static int dt_pages;
static const char *dt_msg;
static void dt_progress(void)
{
    char b[48];
    if ((++dt_pages & 63) == 0) {
        snprintf(b, sizeof(b), T("%d Ko lus", "%d KB read"), dt_pages / 2);
        busy(dt_msg, b);
    }
}
#ifdef DATEL_EMUTEST            /* emulator test build: pages come from host:DATEL.mcd (528-byte pages), never shipped */
#include <fileio.h>
static int dt_reader(void *ctx, uint32_t page, uint8_t *buf)
{
    static int fd = -1;
    (void)ctx;
    dt_progress();
    if (fd < 0) fd = fioOpen("host:DATEL.mcd", 1);
    if (fd < 0) return -1;
    fioLseek(fd, page * 528, 0);
    return fioRead(fd, buf, 512) == 512 ? 0 : -1;
}
#else
static int dt_reader(void *ctx, uint32_t page, uint8_t *buf) { dt_progress(); return mcx_read_page((int)(intptr_t)ctx, page, buf); }
#endif

static int datel_restore(DatelCard *dc, DatelItem *it, int dest)   /* dest 0/1 = slot, 2 = USB */
{
    Save s;
    int r;
    dt_msg = T("Décompression de la sauvegarde Datel...", "Decompressing Datel save...");
    busy(dt_msg, it->title);
    r = datel_extract(dc, it, &s);
    if (r < 0) { say(RGB(0xff, 0x70, 0x70), T("Archive Datel corrompue (%d)", "Corrupted Datel archive (%d)"), r); return r; }
    r = dest == 2 ? write_to_usb(&s) : write_to_slot(dest, &s);
    save_free(&s);
    return r;
}

static char done_mark[64];     /* 1 = restored / exported during this visit */
static char dsel[64];          /* multiple selection in the Datel list */

/* restores / exports every item whose flag is set (all recoverable ones if flags == NULL) */
static void datel_batch(DatelCard *dc, DatelItem *it, int n, const char *flags, int dest)
{
    int i, total = 0, done = 0, fail = 0;
    char b[48];
    for (i = 0; i < n; i++) if (it[i].status == DT_OK && (!flags || flags[i])) total++;
    batch_n = total;
    batch_replace = -1;
    for (i = 0; i < n; i++) {
        if (it[i].status != DT_OK || (flags && !flags[i])) continue;
        snprintf(b, sizeof(b), "%d / %d", done + fail + 1, total);
        busy(it[i].title, b);
        if (datel_restore(dc, &it[i], dest) == 0) { done++; done_mark[i] = 1; } else fail++;
    }
    batch_n = 0;
    say(fail ? RGB(0xff, 0xa0, 0x60) : RGB(0x80, 0xff, 0x90), T("%d restaurée(s), %d échec(s)", "%d restored, %d failed"), done, fail);
}

static void datel_browser(int p)
{
    DatelCard *dc;
    DatelItem *it = malloc(sizeof(DatelItem) * 64);
    int n, err, i, sel = 0, top = 0, rows = 8, rh = 38, nok = 0, q = 1 - p;
    char b[96], b1[64], b2[64], b3[64];
    if (!it) return;
    dt_msg = T("Lecture de l'archive Datel...", "Reading the Datel archive...");
    busy(dt_msg, T("Lecture brute de la carte (lecture seule)", "Raw card read (read-only)"));
    mcx_raw_invalidate();
    dc = datel_open(dt_reader, (void *)(intptr_t)p, &err);
    if (!dc) {
        free(it);
        if (err == -10 || err == -11)
            say(RGB(0xff, 0x70, 0x70), T("Lecture brute impossible (%d) : utilise MMDUMP + pc-tools", "Raw read not possible (%d): use MMDUMP + pc-tools"), err);
        else say(RGB(0xff, 0x70, 0x70), T("Carte illisible (%d)", "Unreadable card (%d)"), err);
        return;
    }
    n = datel_list(dc, it, 64);
    if (n <= 0) { datel_close(dc); free(it); say(RGB(0xff, 0x70, 0x70), T("Archive Datel vide ou illisible (%d)", "Empty or unreadable Datel archive (%d)"), n); return; }
    {   /* recoverable saves first, overwritten ones at the end (stable) */
        DatelItem *t = malloc(sizeof(DatelItem) * n);
        int k = 0, pass;
        if (t) {
            for (pass = 0; pass < 2; pass++)
                for (i = 0; i < n; i++) if ((it[i].status == DT_OK) == (pass == 0)) t[k++] = it[i];
            memcpy(it, t, sizeof(DatelItem) * n);
            free(t);
        }
    }
    for (i = 0; i < n; i++) if (it[i].status == DT_OK) nok++;
    memset(done_mark, 0, sizeof(done_mark));
    memset(dsel, 0, sizeof(dsel));
    while (1) {
        unsigned int e = pad_edge();
        if (e & PAD_UP) sel = (sel + n - 1) % n;
        if (e & PAD_DOWN) sel = (sel + 1) % n;
        if (e & PAD_LEFT) { sel -= rows; if (sel < 0) sel = 0; }
        if (e & PAD_RIGHT) { sel += rows; if (sel >= n) sel = n - 1; }
        if (e & (PAD_CIRCLE | PAD_TRIANGLE)) break;
        if (e & PAD_SQUARE && it[sel].status == DT_OK) { dsel[sel] ^= 1; if (sel + 1 < n) sel++; }
        if (e & PAD_SELECT) {
            int any = 0;
            for (i = 0; i < n; i++) any |= dsel[i];
            for (i = 0; i < n; i++) dsel[i] = !any && it[i].status == DT_OK;
        }
        if (e & PAD_CROSS) {
            int nsel = 0;
            for (i = 0; i < n; i++) nsel += dsel[i];
            if (nsel) {
                const char *m[3];
                char t[48];
                int c;
                sprintf(b1, T("Restaurer la sélection sur le slot %d", "Restore selection to slot %d"), q + 1);
                sprintf(b2, T("Restaurer la sélection sur le slot %d (cette carte)", "Restore selection to slot %d (this card)"), p + 1);
                m[0] = b1;
                m[1] = T("Exporter la sélection vers USB (.psu)", "Export selection to USB (.psu)");
                m[2] = b2;
                snprintf(t, sizeof(t), T("%d sélectionnée(s)", "%d selected"), nsel);
                c = menu(t, m, 3);
                if (c == 2 && !confirm(T("Écrire sur cette carte peut écraser d'autres sauvegardes Datel cachées. Continuer ?",
                                         "Writing to this card may overwrite other hidden Datel saves. Continue?"))) c = -1;
                if (c >= 0) { datel_batch(dc, it, n, dsel, c == 0 ? q : c == 1 ? 2 : p); memset(dsel, 0, sizeof(dsel)); }
                e &= ~PAD_CROSS;
            }
        }
        if (e & PAD_CROSS) {
            DatelItem *d = &it[sel];
            const char *m[6];
            int c, k = 0;
            if (d->status == DT_LOST) {
                say(RGB(0xff, 0xa0, 0x60), T("Écrasée par une autre sauvegarde : irrécupérable", "Overwritten by another save: unrecoverable"));
            } else {
                sprintf(b1, T("Restaurer sur le slot %d", "Restore to slot %d"), q + 1);
                sprintf(b2, T("Restaurer sur le slot %d (cette carte)", "Restore to slot %d (this card)"), p + 1);
                sprintf(b3, T("Tout restaurer sur le slot %d", "Restore all to slot %d"), q + 1);
                m[k++] = b1; m[k++] = b2;
                m[k++] = T("Exporter vers USB (.psu)", "Export to USB (.psu)");
                m[k++] = b3;
                m[k++] = T("Tout exporter vers USB (.psu)", "Export all to USB (.psu)");
                c = menu(d->title[0] ? d->title : d->dir, m, k);
                if (c == 1 && !confirm(T("Écrire sur cette carte peut écraser d'autres sauvegardes Datel cachées. Continuer ?",
                                         "Writing to this card may overwrite other hidden Datel saves. Continue?"))) c = -1;
                if (c >= 0 && c <= 2) {
                    if (datel_restore(dc, d, c == 0 ? q : c == 1 ? p : 2) == 0 && (done_mark[sel] = 1))
                        say(RGB(0x80, 0xff, 0x90), c == 2 ? T("Exporté dans mass:/PS2SAVES", "Exported to mass:/PS2SAVES") : T("Sauvegarde restaurée", "Save restored"));
                } else if (c == 3 || c == 4) {
                    datel_batch(dc, it, n, NULL, c == 3 ? q : 2);
                }
            }
        }
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        angle += 0.02f; tsec += 1.0f / 50;
        gfx_begin();
        draw_frame();
        gfx_rect(16, 50, SW - 32, 372, RGBA(0x08, 0x08, 0x18, 0x7c));
        gfx_rect(16, 50, SW - 32, 3, RGB(0xff, 0xc0, 0x40));
        snprintf(b, sizeof(b), T("Archive Datel X-MEM (slot %d) : %d récupérable(s) sur %d", "Datel X-MEM archive (slot %d): %d recoverable of %d"), p + 1, nok, n);
        gfx_text(32, 60, b, RGB(0xff, 0xc0, 0x40));
        for (i = 0; i < rows && top + i < n; i++) {
            DatelItem *d = &it[top + i];
            int y = 92 + i * rh, s = (top + i == sel), lost = d->status != DT_OK;
            if (s) gfx_rect_grad(24, y - 2, SW - 48, rh - 2, RGBA(0x70, 0x90, 0xff, 0x70), RGBA(0x30, 0x40, 0x90, 0x70));
            if (dsel[top + i]) {
                gfx_rect(24, y - 2, SW - 48, rh - 2, RGBA(0xff, 0xc0, 0x40, 0x28));
                gfx_rect(25, y + 2, 5, rh - 10, RGB(0xff, 0xc0, 0x40));
            }
            gfx_text_clip(34, y, lost ? d->entry : d->title, lost ? RGB(0x80, 0x80, 0x90) : s ? RGB(0xff, 0xff, 0xff) : RGB(0xd8, 0xdc, 0xe8), SW - 200);
            if (lost) snprintf(b, sizeof(b), "%s", d->status == DT_LOST ? T("écrasée - irrécupérable", "overwritten - unrecoverable") : T("corrompue", "corrupted"));
            else snprintf(b, sizeof(b), "%s   %02d/%02d/%04d", d->dir, d->modified.day, d->modified.month, d->modified.year);
            gfx_text_clip(34, y + 16, b, lost ? RGB(0x90, 0x60, 0x60) : RGB(0x90, 0x9c, 0xc0), SW - 200);
            if (!lost) {
                if (done_mark[top + i]) gfx_text(SW - 150, y + 8, "OK", RGB(0x80, 0xff, 0x90));
                kb_str(d->rawsize, b1);
                gfx_text(SW - 40 - gfx_text_w(b1), y + 8, b1, RGB(0xc0, 0xc8, 0xe0));
            }
        }
        gfx_text(32, 398, T("X actions   [] cocher   SELECT tout   O retour   (carte Datel en lecture seule)",
                            "X actions   [] tick   SELECT all   O back   (Datel card is read-only)"), RGB(0xa0, 0xa8, 0xc0));
        if (toast_t > 0) {
            int w = gfx_text_w(toast) + 30;
            toast_t--;
            gfx_rect((SW - w) / 2, 350, w, 28, RGBA(0, 0, 0, 0x70));
            gfx_text((SW - w) / 2 + 15, 354, toast, toast_col);
        }
        gfx_end();
    }
    datel_close(dc);
    free(it);
    refresh_info();
}

static Entry batch_e[MAXENT];

/* one save, one action. post: -1 = ask after success, 0/1 = decided beforehand (batch). returns 1 if done */
static int do_one(int t, int c, Entry *e, int post)
{
    Save s;
    int p = port_of(t), q = 1 - p, r, ok = 0, del;
    if ((t <= T_SLOT2 && c == 5) || (t >= T_ARC1 && t <= T_ARC2 && c == 3)) {
        busy(T("Suppression...", "Deleting..."), e->dir);
        r = (t <= T_SLOT2) ? mcx_delete_save(p, e->dir) : mcx_delete_archive(p, e->dir);
        invalidate_port(p);
        if (r < 0) say(RGB(0xff, 0x70, 0x70), T("Échec de suppression : %s", "Delete failed: %s"), mcerr(r));
        return r >= 0;
    }
    r = load_source(t, e, &s);
    if (r < 0) { say(RGB(0xff, 0x70, 0x70), T("Lecture impossible : %s", "Cannot read: %s"), r <= -300 ? T("archive corrompue", "corrupted archive") : mcerr(r)); return 0; }
    if (t <= T_SLOT2) {
        if (c <= 1) {
            if (write_to_slot(q, &s) == 0) {
                ok = 1;
                if (c == 1) { busy(T("Suppression de l'original...", "Deleting original..."), e->dir); mcx_delete_save(p, e->dir); invalidate_port(p); }
            }
        } else if (c <= 3) {
            if (write_to_archive(c == 2 ? p : q, &s) == 0) {
                ok = 1;
                del = post < 0 ? confirm(T("Archive créée. Supprimer l'original ?", "Archive created. Delete the original?")) : post;
                if (del) { busy(T("Suppression de l'original...", "Deleting original..."), e->dir); mcx_delete_save(p, e->dir); invalidate_port(p); }
            }
        } else ok = write_to_usb(&s) == 0;
    } else if (t == T_USB) {
        ok = (c <= 1 ? write_to_slot(c, &s) : write_to_archive(c - 2, &s)) == 0;
    } else {
        if (c <= 1) {
            if (write_to_slot(c == 0 ? p : q, &s) == 0) {
                ok = 1;
                del = post < 0 ? confirm(T("Restaurée. Supprimer l'archive ?", "Restored. Delete the archive?")) : post;
                if (del) { mcx_delete_archive(p, e->dir); invalidate_port(p); }
            }
        } else ok = write_to_usb(&s) == 0;
    }
    save_free(&s);
    return ok;
}

static const char *done_msg(int t, int c)
{
    if (t <= T_SLOT2) {
        static const char *fr[] = {"Copié", "Déplacé", "Compressé", "Compressé", "Exporté dans mass:/PS2SAVES", "Supprimé"};
        static const char *en[] = {"Copied", "Moved", "Compressed", "Compressed", "Exported to mass:/PS2SAVES", "Deleted"};
        return g_fr ? fr[c] : en[c];
    }
    if (t == T_USB) return T("Importé", "Imported");
    {
        static const char *fr[] = {"Restauré", "Restauré", "Exporté dans mass:/PS2SAVES", "Supprimé"};
        static const char *en[] = {"Restored", "Restored", "Exported to mass:/PS2SAVES", "Deleted"};
        return g_fr ? fr[c] : en[c];
    }
}

static void do_actions(void)
{
    List *l = &L[tab];
    Entry e;
    const char *it[8];
    int n = 0, c, p = port_of(tab), q = 1 - p, i, count = 0, ok = 0, post = -1, isdel;
    static char b1[48], b2[48], b3[48], b4[48], title[64], prog[48];
    if (!l->n) return;
    e = l->e[l->sel];
    if (!l->nmark && tab <= T_SLOT2 && !strcmp(e.dir, DATEL_DIR)) {
        const char *d[3] = {T("Ouvrir l'archive Datel (voir / restaurer)", "Open the Datel archive (view / restore)"),
                            T("Actions sur le dossier X-MEM", "Actions on the X-MEM folder"), NULL};
        c = menu(e.t1, d, 2);
        if (c == 0) { datel_browser(p); return; }
        if (c < 0) return;
    }
    if (tab <= T_SLOT2) {
        sprintf(b1, T("Copier vers le slot %d", "Copy to slot %d"), q + 1);
        sprintf(b2, T("Déplacer vers le slot %d", "Move to slot %d"), q + 1);
        sprintf(b3, T("Compresser (archives slot %d)", "Compress (archives, slot %d)"), p + 1);
        sprintf(b4, T("Compresser (archives slot %d)", "Compress (archives, slot %d)"), q + 1);
        it[n++] = b1; it[n++] = b2; it[n++] = b3; it[n++] = b4;
        it[n++] = T("Exporter vers USB (.psu)", "Export to USB (.psu)");
        it[n++] = T("Supprimer", "Delete");
    } else if (tab == T_USB) {
        it[n++] = T("Importer vers le slot 1", "Import to slot 1");
        it[n++] = T("Importer vers le slot 2", "Import to slot 2");
        it[n++] = T("Importer compressé (archives slot 1)", "Import compressed (archives, slot 1)");
        it[n++] = T("Importer compressé (archives slot 2)", "Import compressed (archives, slot 2)");
    } else {
        sprintf(b1, T("Restaurer sur le slot %d", "Restore to slot %d"), p + 1);
        sprintf(b2, T("Restaurer sur le slot %d", "Restore to slot %d"), q + 1);
        it[n++] = b1; it[n++] = b2;
        it[n++] = T("Exporter vers USB (.psu)", "Export to USB (.psu)");
        it[n++] = T("Supprimer l'archive", "Delete archive");
    }
    if (l->nmark) snprintf(title, sizeof(title), T("%d sélectionnée(s)", "%d selected"), l->nmark);
    else snprintf(title, sizeof(title), "%s", e.t1);
    c = menu(title, it, n);
    if (c < 0) return;

    if (l->nmark) { for (i = 0; i < l->n; i++) if (l->mark[i]) batch_e[count++] = l->e[i]; }
    else batch_e[count++] = e;
    isdel = (tab <= T_SLOT2 && c == 5) || (tab >= T_ARC1 && tab <= T_ARC2 && c == 3);
    if (isdel) {
        char qd[80];
        if (count > 1) snprintf(qd, sizeof(qd), T("Supprimer définitivement %d éléments ?", "Delete %d items permanently?"), count);
        else snprintf(qd, sizeof(qd), "%s", T("Supprimer définitivement ?", "Delete permanently?"));
        if (!confirm(qd)) return;
    }
    if (count > 1) {
        if (tab <= T_SLOT2 && (c == 2 || c == 3)) post = confirm(T("Supprimer les originaux après compression ?", "Delete the originals after compressing?"));
        if (tab >= T_ARC1 && tab <= T_ARC2 && c <= 1) post = confirm(T("Supprimer les archives après restauration ?", "Delete the archives after restoring?"));
    }
    batch_n = count;
    batch_replace = -1;
    for (i = 0; i < count; i++) {
        if (count > 1) {
            snprintf(prog, sizeof(prog), "%d / %d", i + 1, count);
            busy(batch_e[i].t1, prog);
        }
        ok += do_one(tab, c, &batch_e[i], post);
    }
    batch_n = 0;
    if (count > 1) {
        say(ok == count ? RGB(0x80, 0xff, 0x90) : RGB(0xff, 0xa0, 0x60), T("%s : %d / %d", "%s: %d / %d"), done_msg(tab, c), ok, count);
        memset(l->mark, 0, sizeof(l->mark));
        l->nmark = 0;
    } else if (ok) say(RGB(0x80, 0xff, 0x90), "%s", done_msg(tab, c));
    refresh_info();
}

static void do_start_menu(void)
{
    const char *it[] = {T("Actualiser", "Refresh"), T("Quitter vers le navigateur PS2", "Exit to the PS2 browser"), T("À propos / état du pilote", "About / driver status")};
    int c = menu("Menu", it, 3), i;
    if (c == 0) { for (i = 0; i < NTABS; i++) L[i].loaded = 0; refresh_info(); }
    else if (c == 1) {
        int r;
        busy(T("Installation du pilote résident, ouverture du navigateur...", "Installing resident driver, opening the browser..."), NULL);
        r = sys_exit_browser();
        say(RGB(0xff, 0x70, 0x70), T("Pilote résident non installé (%d) : éteins la console et réessaie", "Resident driver not installed (%d): power off and retry"), r);
    }
    else if (c == 2) {
        char b[160];
        int s = sys_secr_status;
        snprintf(b, sizeof(b), g_fr ? "XMEM2 v" VERSION " - Soaresden - pilote : %s%s%s (0x%x)" : "XMEM2 v" VERSION " - Soaresden - driver: %s%s%s (0x%x)",
                 (s & (1 << 8)) ? T("secrman trouvé", "secrman found") : T("secrman INTROUVABLE", "secrman NOT FOUND"),
                 (s & (3 << 11)) ? T(", patch actif", ", patch active") : "", (s & (1 << 10)) ? T(", vérifié", ", verified") : "", s >> 8);
        say(RGB(0xc0, 0xd0, 0xff), "%s", b);
    }
}

/* ------------------------------------------------------------ boucle */
int main(int argc, char *argv[])
{
    int frame = 0;
    {
        g_fr = (configGetLanguage() == LANGUAGE_FRENCH);
    }
    gfx_init();
    busy(T("Démarrage...", "Starting..."), T("Chargement des modules, du pilote carte et de l'USB", "Loading modules, card driver and USB"));
    sys_init();
    refresh_info();
    if (!(sys_secr_status & (1 << 8))) say(RGB(0xff, 0xa0, 0x60), T("Pilote Datel non appliqué (secrman introuvable)", "Datel driver not applied (secrman not found)"));
    while (1) {
        unsigned int e;
        List *l = &L[tab];
        if (!l->loaded) { busy(T("Lecture...", "Reading..."), tabname[tab]); load_tab(tab); l = &L[tab]; }
        e = pad_edge();
        if (e) idle = 0; else idle++;
        if (e & PAD_UP && l->n) l->sel = (l->sel + l->n - 1) % l->n;
        if (e & PAD_DOWN && l->n) l->sel = (l->sel + 1) % l->n;
        if (e & PAD_LEFT && l->n) { l->sel -= 9; if (l->sel < 0) l->sel = 0; }
        if (e & PAD_RIGHT && l->n) { l->sel += 9; if (l->sel >= l->n) l->sel = l->n - 1; }
        if (e & PAD_R1) { tab = (tab + 1) % NTABS; icon_for_tab = -1; }
        if (e & PAD_L1) { tab = (tab + NTABS - 1) % NTABS; icon_for_tab = -1; }
        if (e & PAD_SQUARE && l->n) {                  /* multiple selection */
            l->mark[l->sel] ^= 1;
            l->nmark += l->mark[l->sel] ? 1 : -1;
            if (l->sel + 1 < l->n) l->sel++;
        }
        if (e & PAD_SELECT && l->n) {                  /* all / none */
            int k, all = l->nmark == 0;
            for (k = 0; k < l->n; k++) l->mark[k] = all;
            l->nmark = all ? l->n : 0;
        }
        if (e & (PAD_CROSS | PAD_TRIANGLE)) do_actions();
        if (e & PAD_START) do_start_menu();
        l = &L[tab];
        if (l->loaded && (icon_for_tab != tab || icon_for_sel != l->sel) && idle > 8) load_icon();
        if (++frame % 150 == 0) {          /* detection de changement de carte */
            int p, t, f, fm;
            for (p = 0; p < 2; p++) {
                int r = mcx_info(p, &t, &f, &fm);
                if (r != 0 || t != ctype[p] || fm != cfmt[p]) { invalidate_port(p); }
                ctype[p] = t; cfreekb[p] = f; cfmt[p] = fm;
            }
        }
        angle += 0.02f;
        tsec += 1.0f / 50;
        if (toast_t > 0) toast_t--;
        gfx_begin();
        draw_frame();
        gfx_end();
    }
    return 0;
}
