/* XMEM2 - gestionnaire de cartes memoire PS2 (Soaresden) - modele de sauvegarde */
#ifndef XM_SAVE_H
#define XM_SAVE_H
#include <stdint.h>

typedef struct { uint8_t resv, sec, min, hour, day, month; uint16_t year; } McDate;  /* = sceMcStDateTime */

typedef struct {
    char name[32];
    uint16_t attr;          /* mode McFsEntry (ex. 0x8497) */
    McDate created, modified;
    uint32_t size;
    uint8_t *data;
} SaveFile;

#define XM_MAXFILES 64
typedef struct {
    char dir[32];
    uint16_t attr;          /* mode du dossier (ex. 0x8427) */
    McDate created, modified;
    int nfiles;
    SaveFile f[XM_MAXFILES];
} Save;

void save_init(Save *s);
void save_free(Save *s);
uint32_t save_total_size(const Save *s);
int save_find(const Save *s, const char *name);          /* index du fichier ou -1 */
/* nombre de clusters (1 Ko) necessaires sur une carte (dossier + entrees + fichiers) */
int save_needed_kb(const Save *s);

/* ---- icon.sys ---- */
/* extrait le titre (2 lignes) depuis un icon.sys (964 o) ; retourne 0 si ok */
int iconsys_title(const uint8_t *iconsys, int len, char *l1, char *l2, int max);
/* nom du fichier icone de la liste (view) */
int iconsys_viewicon(const uint8_t *iconsys, int len, char *out, int max);
void sjis_to_latin(const uint8_t *s, int len, char *out, int max);
int latin_to_sjis(const char *s, uint8_t *out, int max);

/* ---- PSU (format EMS / uLaunchELF) ---- */
int psu_build(const Save *s, uint8_t **out, uint32_t *outlen);   /* malloc */
int psu_parse(const uint8_t *buf, uint32_t len, Save *s);         /* copie les donnees */

/* ---- Archive XMEM2 (zlib, fichier normal = jamais ecrase) ---- */
#define XM_ARC_MAGIC 0x324D4D58u   /* "XMM2" */
#define XM_ARC_HDR 192
typedef struct {
    uint32_t magic, version, raw_size, comp_size, crc;
    uint16_t dirattr, nfiles;
    McDate created, modified;
    char dir[32];
    uint8_t title[68];
    uint16_t nl;
} ArcHeader;
int arc_build(const Save *s, const uint8_t *iconsys, int iconsys_len, uint8_t **out, uint32_t *outlen);
int arc_read_header(const uint8_t *buf, uint32_t len, ArcHeader *h);
int arc_extract(const uint8_t *buf, uint32_t len, Save *s);  /* <0 si corrompu */

#endif
