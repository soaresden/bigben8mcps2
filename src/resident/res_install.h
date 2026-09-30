#ifndef MM_RES_INSTALL_H
#define MM_RES_INSTALL_H
typedef struct {
    unsigned int execps2, hookoff, taillen, clearpatch, ok;
    unsigned int khook;     /* 1 = SifSetReg syscall hooked now, 2 = was already hooked */
} ResInfo;
/* resident driver already active (installed since power-on)? copies its 8 status counters, returns 1 */
int res_read_status(unsigned int st[8]);
/* copie le pilote resident a 0xC0000 et l'accroche au noyau ; retourne 0 si ok */
int res_install(ResInfo *info);
#endif
