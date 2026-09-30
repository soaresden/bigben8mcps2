/*
 * MMACTIV.ELF - active la carte Datel "Mega Memory" violette (test / lancement manuel)
 * puis ouvre le navigateur PS2 (sans relancer le menu de demarrage) pour verifier.
 */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <debug.h>
#include <stdio.h>
#include <unistd.h>

extern unsigned char mmactiv_irx[];
extern unsigned int size_mmactiv_irx;

static const char *label(int r)
{
    switch (r & 0x0f) {
    case 0: return "aucune carte / pas de reponse";
    case 1: return "deja active (repond normalement)";
    case 2: return "ACTIVEE (MagicGate OK)";
    case 3: return "ACTIVEE (sans MagicGate, typique Datel)";
    case 5: return "ECHEC commande F7";
    case 6: return "ECHEC lecture des specs";
    default: return "etat inconnu";
    }
}

static void wait_sec(int s)
{
    int i;
    for (i = s; i > 0; i--) {
        scr_printf("\r   Ouverture du navigateur PS2 dans %d s...  ", i);
        sleep(1);
    }
}

int main(int argc, char *argv[])
{
    int id, ret = 0, r1, r2;

    SifInitRpc(0);
    while (!SifIopReset("", 0)) {};
    while (!SifIopSync()) {};
    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    init_scr();
    scr_printf("\n\n   MMACTIV - activation carte Datel Mega Memory (sans CD)\n");
    scr_printf("   par Soaresden - sequence reprise de SIOSTUFF.COD (MegaMem v1.70)\n\n");

    id = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    scr_printf("   rom0:SIO2MAN ........ %s (%d)\n", id >= 0 ? "OK" : "ERREUR", id);

    id = SifExecModuleBuffer(mmactiv_irx, size_mmactiv_irx, 0, NULL, &ret);
    scr_printf("   MMACTIV.IRX ......... %s (%d)\n\n", id >= 0 ? "OK" : "ERREUR", id);

    if (id >= 0) {
        r1 = (ret >> 8) & 0xff;
        r2 = (ret >> 16) & 0xff;
        scr_printf("   Slot 1 : %s%s\n", label(r1), (r1 & 0x10) ? " + verifiee" : "");
        scr_printf("   Slot 2 : %s%s\n", label(r2), (r2 & 0x10) ? " + verifiee" : "");
        scr_printf("\n   (codes bruts : slot1=0x%02x slot2=0x%02x)\n\n", r1, r2);
    }

    wait_sec(8);

    {
        char *args[] = {"SkipMc", "BootBrowser"};
        SifExitRpc();
        LoadExecPS2("rom0:OSDSYS", 2, args);
    }
    return 0;
}
