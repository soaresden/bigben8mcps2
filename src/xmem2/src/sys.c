/* Initialisation IOP, manette, sortie vers le navigateur */
#define NEWLIB_PORT_AWARE
#include "sys.h"
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <libpad.h>
#include <libmc.h>
#include <fileXio_rpc.h>
#include <string.h>
#include "mcio.h"

#define IRX(n) extern unsigned char n[]; extern unsigned int size_##n;
IRX(secrpatch_irx) IRX(iomanX_irx) IRX(fileXio_irx) IRX(usbd_irx) IRX(bdm_irx) IRX(bdmfs_fatfs_irx) IRX(usbmass_bd_irx) IRX(xmraw_irx)
#include "res_install.h"

int sys_secr_status;
static char padbuf[256] __attribute__((aligned(64)));

static int exec_buf(void *p, unsigned int sz)
{
    int r = 0;
    SifExecModuleBuffer(p, sz, 0, NULL, &r);
    return r;
}

void sys_init(void)
{
    int st, tries;
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {};
    while (!SifIopSync()) {};
    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    sys_secr_status = exec_buf(secrpatch_irx, size_secrpatch_irx);   /* avant MCMAN */
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    exec_buf(xmraw_irx, size_xmraw_irx);    /* raw page reads (Datel X-MEM archives), read-only */
    exec_buf(iomanX_irx, size_iomanX_irx);
    exec_buf(fileXio_irx, size_fileXio_irx);
    exec_buf(usbd_irx, size_usbd_irx);
    exec_buf(bdm_irx, size_bdm_irx);
    exec_buf(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
    exec_buf(usbmass_bd_irx, size_usbmass_bd_irx);
    fileXioInit();
    mcx_init();
    mcx_raw_init();

    padInit(0);
    padPortOpen(0, 0, padbuf);
    for (tries = 0; tries < 200; tries++) {
        st = padGetState(0, 0);
        if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) break;
        nopdelay();
    }
}

unsigned int pad_read(void)
{
    struct padButtonStatus b;
    int st = padGetState(0, 0);
    if (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1) return 0;
    if (padRead(0, 0, &b) == 0) return 0;
    return 0xffff ^ b.btns;
}

int sys_exit_browser(void)
{
    static char *args[] = {"SkipHdd", "BootBrowser", "SkipMc"};
    ResInfo ri;
    int r;
    padPortClose(0, 0);
    padEnd();
    fileXioUmount("mass:");
    r = res_install(&ri);
    if (r != 0) {                          /* on rouvre la manette et on reste dans l'appli */
        padPortOpen(0, 0, padbuf);
        return r;
    }
    SifExitRpc();
    LoadExecPS2("rom0:OSDSYS", 3, args);
    return 0;
}
