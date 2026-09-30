#ifndef XM_MCIO_H
#define XM_MCIO_H
#include "save.h"
#define XM_ARCDIR "XMEM2-ARCHIVES"
typedef struct {
    char dir[32];          /* nom du dossier (ou de l'archive) */
    char t1[64], t2[64];   /* titre */
    unsigned int size;     /* octets (sauvegarde) ou taille compressee (archive) */
    unsigned int rawsize;  /* archive : taille decompressee */
    McDate modified;
    unsigned short attr;
    char file[64];         /* USB : nom du fichier .psu */
} Entry;
typedef void (*ProgressCb)(const char *what, int done, int total);

int  mcx_init(void);
/* type : 0 aucune, 1 PS1, 2 PS2 ; retour <0 = changement/erreur */
int  mcx_info(int port, int *type, int *freekb, int *formatted);
int  mcx_list(int port, Entry *out, int max);
int  mcx_list_archives(int port, Entry *out, int max);
int  mcx_exists(int port, const char *dir);
int  mcx_read_file(int port, const char *path, unsigned char **data, unsigned int *len);
int  mcx_load_save(int port, const char *dir, Save *s);
int  mcx_write_save(int port, const Save *s, ProgressCb cb);          /* ecrase si present */
int  mcx_delete_save(int port, const char *dir);
int  mcx_write_archive(int port, const char *name, const unsigned char *buf, unsigned int len);
int  mcx_delete_archive(int port, const char *name);
int  mcx_archive_exists(int port, const char *name);
int  mcx_raw_init(void);                                          /* binds XMRAW.IRX */
void mcx_raw_invalidate(void);
int  mcx_read_page(int port, unsigned int page, void *buf512);   /* raw page, for Datel X-MEM archives */
#endif
