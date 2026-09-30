/*
 * MMDIAG.ELF - diagnostic de l'authentification MagicGate des cartes memoire.
 * Test 1 : slot 1 (carte Sony de reference), sequence MCMAN Sony
 * Test 2 : slot 2 (Datel), sequence MCMAN Sony
 * Test 3 : slot 2 (Datel), sequence du CD Datel (avec F7 01)
 * Journal complet : mass:/BAKMEGAMEMORYMC/MMDIAG_LOG.TXT
 */
#define NEWLIB_PORT_AWARE
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <io_common.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#define RPC_ID 0x4D4D0002
#define OUTDIR "mass:/BAKMEGAMEMORYMC"

#define IRX(n) extern unsigned char n[]; extern unsigned int size_##n;
IRX(mmdiag_irx) IRX(iomanX_irx) IRX(fileXio_irx) IRX(usbd_irx) IRX(bdm_irx) IRX(bdmfs_fatfs_irx) IRX(usbmass_bd_irx)

typedef struct { u8 port, kind, txlen, rxlen; u32 stat6c; u8 tx[24], rx[24]; } logent_t;

static SifRpcClientData_t cd __attribute__((aligned(64)));
static u32 sbuf[16] __attribute__((aligned(64)));
static u32 rbuf[140 * 56 / 4 + 64] __attribute__((aligned(64)));
static char fbuf[65536];
static int flen;

static void F(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    flen += vsnprintf(fbuf + flen, sizeof(fbuf) - flen, fmt, ap);
    va_end(ap);
    if (flen > (int)sizeof(fbuf) - 1) flen = sizeof(fbuf) - 1;
}

static void load(void *p, unsigned int sz) { int r; SifExecModuleBuffer(p, sz, 0, NULL, &r); }

static const char *bitname[] = {"probe initial", "F3 reset auth", "F7 01", "MagicGate", "specs", "probe final"};

int main(int argc, char *argv[])
{
    static const struct { int port, mode; const char *nom; } T[3] = {
        {2, 0, "Slot 1 (Sony)  - sequence Sony "},
        {3, 0, "Slot 2 (Datel) - sequence Sony "},
        {3, 1, "Slot 2 (Datel) - sequence Datel"},
    };
    int i, j, k, r[3], n, fd, usb = 0;
    logent_t *lg;

    SifInitRpc(0);
    while (!SifIopReset("", 0)) {};
    while (!SifIopSync()) {};
    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    init_scr();
    scr_printf("\n\n   MMDIAG - diagnostic MagicGate des cartes memoire (Soaresden)\n\n");

    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    load(mmdiag_irx, size_mmdiag_irx);
    while (SifBindRpc(&cd, RPC_ID, 0) < 0 || cd.server == NULL) nopdelay();

    for (i = 0; i < 3; i++) {
        sbuf[0] = T[i].port; sbuf[1] = T[i].mode;
        SifCallRpc(&cd, 1, 0, sbuf, 16, rbuf, 64, NULL, NULL);
        r[i] = rbuf[0];
        scr_printf("   %s : ", T[i].nom);
        for (k = 0; k < 6; k++) {
            if (k == 2 && T[i].mode == 0) continue;
            scr_printf("%s%s ", (r[i] >> k) & 1 ? "" : "!", k == 3 ? "MG" : (k == 4 ? "specs" : (k == 0 ? "p1" : (k == 1 ? "F3" : (k == 2 ? "F7" : "p2")))));
        }
        scr_printf("\n");
    }
    scr_printf("   (! = echec ; MG = authentification MagicGate)\n\n");

    SifCallRpc(&cd, 2, 0, sbuf, 16, rbuf, sizeof(rbuf), NULL, NULL);
    n = rbuf[0];
    lg = (logent_t *)&rbuf[1];

    F("MMDIAG - journal d'authentification\r\n\r\n");
    for (i = 0; i < 3; i++) {
        F("%s : resultat 0x%02x\r\n", T[i].nom, r[i]);
        for (k = 0; k < 6; k++) F("   %-14s %s\r\n", bitname[k], (k == 2 && T[i].mode == 0) ? "-" : ((r[i] >> k) & 1 ? "OK" : "ECHEC"));
    }
    F("\r\nPaquets (err = bits d'erreur SIO2 du stat6c)\r\n");
    for (j = 0; j < n; j++) {
        logent_t *e = &lg[j];
        if (e->kind == 2) { F("\r\n=== port %d mode %s ===\r\n", e->tx[0], e->tx[1] ? "Datel" : "Sony"); continue; }
        if (e->kind == 1) { F("   -> SecrAuthCard = %d\r\n", (int)e->stat6c); continue; }
        {
            int err = ((e->stat6c >> 13) & 1) | (((e->stat6c >> 14) & 3) << 1);
            F("%3d p%d stat=%08x err=%d tx(%d):", j, e->port, (unsigned)e->stat6c, err, e->txlen);
            for (k = 0; k < e->txlen && k < 24; k++) F(" %02x", e->tx[k]);
            F("\r\n                         rx(%d):", e->rxlen);
            for (k = 0; k < e->rxlen && k < 24; k++) F(" %02x", e->rx[k]);
            F("\r\n");
        }
    }

    /* resume a l'ecran : premier paquet en erreur pendant l'auth de la Datel */
    for (j = 0; j < n; j++) {
        logent_t *e = &lg[j];
        if (e->kind == 0 && e->port == 3 && (((e->stat6c >> 13) & 1) || ((e->stat6c >> 14) & 3))) {
            scr_printf("   1er paquet en erreur (Datel) : n.%d cmd %02x %02x\n", j, e->tx[0], e->tx[1]);
            break;
        }
    }
    scr_printf("   %d paquets enregistres.\n\n   Ecriture sur la cle USB", n);

    load(iomanX_irx, size_iomanX_irx);
    load(fileXio_irx, size_fileXio_irx);
    load(usbd_irx, size_usbd_irx);
    load(bdm_irx, size_bdm_irx);
    load(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
    load(usbmass_bd_irx, size_usbmass_bd_irx);
    fileXioInit();
    for (i = 0; i < 20; i++) {
        int d = fileXioDopen("mass:/");
        if (d >= 0) { fileXioDclose(d); usb = 1; break; }
        scr_printf(".");
        sleep(1);
    }
    if (usb) {
        fileXioMkdir(OUTDIR, 0777);
        fd = fileXioOpen(OUTDIR "/MMDIAG_LOG.TXT", FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
        if (fd >= 0) { fileXioWrite(fd, fbuf, flen); fileXioClose(fd); scr_printf(" OK\n   -> BAKMEGAMEMORYMC/MMDIAG_LOG.TXT\n"); }
        else scr_printf(" ERREUR (%d)\n", fd);
        fileXioUmount("mass:");
    } else
        scr_printf(" : cle introuvable, fais une photo de l'ecran.\n");

    scr_printf("\n   FINI. Tu peux eteindre la console.\n");
    while (1) sleep(1);
    return 0;
}
