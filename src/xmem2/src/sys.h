#ifndef XM_SYS_H
#define XM_SYS_H
extern int sys_secr_status;     /* bits 8.. de SECRPATCH (voir irx/secrpatch.c) */
void sys_init(void);
unsigned int pad_read(void);    /* boutons appuyes (1 = appuye) */
int sys_exit_browser(void);     /* installe le pilote resident (clean-room) puis ouvre le navigateur ; <0 si echec */
#endif
