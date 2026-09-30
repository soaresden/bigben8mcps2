/*
 * MMACTIV.IRX - activation de la carte memoire Datel "Mega Memory" (violette)
 * sans le CD "MegaMem Program v1.70".
 *
 * Reproduit a l'identique la sequence envoyee par le module SIOSTUFF.COD du CD
 * (fonctions 0xd84 / 0xb28 / 0x858 du module d'origine) :
 *   81 11                      probe       -> si OK la carte est deja active
 *   81 F3 00                   reset auth
 *   81 F7 01                   (commande absente du MCMAN Sony -> "deverrouillage")
 *   SecrAuthCard(port,0,cnum)  authentification MagicGate (peut echouer sur une Datel)
 *   81 28 / 81 27 5A / 81 26 5A  probe, terminateur, lecture des specs
 * Reste resident (les handlers secrman pointent sur ce module).
 *
 * Code retour de _start : bits 0-1 = MODULE_RESIDENT_END,
 *   bits 8-15 = resultat slot 1 (port SIO2 2), bits 16-23 = slot 2 (port 3).
 */
#include <irx.h>
#include <loadcore.h>
#include <sysclib.h>
#include <sio2man.h>
#include <secrman.h>
#include <thbase.h>

IRX_ID("mmactiv", 1, 0);

/* resultats par slot */
#define R_NOCARD     0   /* pas de reponse                          */
#define R_ALREADY    1   /* 81 11 OK des le depart : deja active     */
#define R_MG_OK      2   /* active, MagicGate OK (carte Sony-like)  */
#define R_NO_MG      3   /* active, specs OK sans MagicGate         */
#define R_F3_FAIL    4
#define R_F7_FAIL    5
#define R_SPEC_FAIL  6
#define R_VERIFIED   0x10 /* 81 11 repond OK apres activation       */

static sio2_transfer_data_t td;
static u8 txb[16], rxb[16];
u8 mmactiv_specs[2][16];

static int xfer_err(void)
{
    u32 s = td.stat6c;
    return ((s >> 13) & 1) || ((s >> 14) & 3);
}

/* un paquet SIO2 memory card, memes registres que SIOSTUFF.COD */
static int mc_cmd(int port, u32 ctrl1, u32 ctrl2, u32 reg, const u8 *cmd, int clen, int size)
{
    memset(&td, 0, sizeof(td));
    memset(txb, 0, sizeof(txb));
    memset(rxb, 0, sizeof(rxb));
    memcpy(txb, cmd, clen);
    td.port_ctrl1[port] = ctrl1;
    td.port_ctrl2[port] = ctrl2;
    td.regdata[0] = (port & 3) | reg;
    td.regdata[1] = 0;
    td.in_size = size;
    td.out_size = size;
    td.in = txb;
    td.out = rxb;
    sio2_mc_transfer_init();
    sio2_transfer(&td);
    return xfer_err() ? -1 : 0;
}

#define CTRL1 0x00040505
#define CTRL2 0x00020f00

static int probe11(int port)
{
    static const u8 c[] = {0x81, 0x11};
    return mc_cmd(port, CTRL1, CTRL2, 0x00100440, c, 2, 4);
}

/* handlers passes a secrman (identiques a SIOSTUFF 0x820 / 0x84c) */
static int cmd_handler(int port, int slot, sio2_transfer_data_t *d)
{
    (void)port; (void)slot;
    sio2_mc_transfer_init();
    return sio2_transfer(d);
}
static int devid_handler(int port, int slot)
{
    (void)slot;
    return (port & 1) << 3;
}

static int get_specs(int port)
{
    static const u8 c28[] = {0x81, 0x28};
    static const u8 c27[] = {0x81, 0x27, 0x5A};
    static const u8 c26[] = {0x81, 0x26, 0x5A};
    if (mc_cmd(port, CTRL1, CTRL2, 0x00140540, c28, 2, 5)) return -1;
    if (mc_cmd(port, CTRL1, CTRL2, 0x00140540, c27, 3, 5)) return -1;
    if (mc_cmd(port, CTRL1, CTRL2, 0x00340d40, c26, 3, 13)) return -1;
    memcpy(mmactiv_specs[port & 1], rxb, 13);
    return 0;
}

static int activate(int port)
{
    static const u8 cF3[] = {0x81, 0xF3, 0x00};
    static const u8 cF7[] = {0x81, 0xF7, 0x01};
    int mg_fail, r;

    if (probe11(port) == 0)
        return R_ALREADY;

    if (mc_cmd(port, CTRL1, CTRL2, 0x00140540, cF3, 3, 5)) return R_F3_FAIL;
    if (mc_cmd(port, CTRL1, CTRL2, 0x00140540, cF7, 3, 5)) return R_F7_FAIL;

    SecrSetMcCommandHandler(cmd_handler);
    SecrSetMcDevIDHandler(devid_handler);
    mg_fail = (SecrAuthCard(port, 0, (port == 3) ? 8 : 0) != 1);

    if (get_specs(port)) return R_SPEC_FAIL;
    r = mg_fail ? R_NO_MG : R_MG_OK;

    DelayThread(20000);
    if (probe11(port) == 0) r |= R_VERIFIED;
    return r;
}

int _start(int argc, char *argv[])
{
    int r2, r3;
    (void)argc; (void)argv;
    int i;
    for (i = 0; i < 3; i++) {           /* quelques essais, comme la boucle du CD */
        r2 = activate(2);
        if (r2 == R_ALREADY || (r2 & 0x0f) == R_MG_OK || (r2 & 0x0f) == R_NO_MG) break;
        DelayThread(100000);
    }
    for (i = 0; i < 3; i++) {
        r3 = activate(3);
        if (r3 == R_ALREADY || (r3 & 0x0f) == R_MG_OK || (r3 & 0x0f) == R_NO_MG) break;
        DelayThread(100000);
    }
    if (r2 == R_F3_FAIL) r2 = R_NOCARD;   /* aucune reponse = pas de carte */
    if (r3 == R_F3_FAIL) r3 = R_NOCARD;
    return MODULE_RESIDENT_END | (r2 << 8) | (r3 << 16);
}
