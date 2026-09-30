/*
 * MMDIAG.IRX - trace de l'authentification carte memoire (MagicGate) paquet par paquet.
 * RPC 0x4D4D0002 : fn1 test(port, mode) ; fn2 lire le journal.
 * mode 0 = comme le MCMAN Sony (F3 puis SecrAuthCard), mode 1 = comme le CD Datel (F3, F7 01, SecrAuthCard)
 */
#include <irx.h>
#include <loadcore.h>
#include <sysclib.h>
#include <sio2man.h>
#include <secrman.h>
#include <thbase.h>
#include <sifcmd.h>

IRX_ID("mmdiag", 1, 0);
#define RPC_ID 0x4D4D0002

typedef struct {
    u8 port, kind, txlen, rxlen;   /* kind: 0 = paquet, 1 = resultat SecrAuthCard, 2 = marqueur */
    u32 stat6c;
    u8 tx[24], rx[24];
} logent_t;                        /* 56 octets */
#define MAXLOG 140
static logent_t lg[MAXLOG];
static int nlog;

static void addlog(int port, int kind, u32 stat, const u8 *tx, int txl, const u8 *rx, int rxl)
{
    logent_t *e;
    if (nlog >= MAXLOG) return;
    e = &lg[nlog++];
    memset(e, 0, sizeof(*e));
    e->port = port; e->kind = kind; e->stat6c = stat;
    e->txlen = txl; e->rxlen = rxl;
    if (tx) memcpy(e->tx, tx, txl > 24 ? 24 : txl);
    if (rx) memcpy(e->rx, rx, rxl > 24 ? 24 : rxl);
}

static void log_td(int port, sio2_transfer_data_t *d)
{
    const u8 *tx = d->in, *rx = d->out;
    int tl = d->in_size, rl = d->out_size;
    if (!tx && d->in_dma.addr) { tx = d->in_dma.addr; tl = d->in_dma.size * d->in_dma.count * 4; }
    if (!rx && d->out_dma.addr) { rx = d->out_dma.addr; rl = d->out_dma.size * d->out_dma.count * 4; }
    addlog(port, 0, d->stat6c, tx, tl > 255 ? 255 : tl, rx, rl > 255 ? 255 : rl);
}

static sio2_transfer_data_t td;
static u8 txb[64], rxb[64];

static int mc_cmd(int port, u32 reg, const u8 *cmd, int clen, int size)
{
    memset(&td, 0, sizeof(td));
    memset(txb, 0, sizeof(txb));
    memset(rxb, 0, sizeof(rxb));
    memcpy(txb, cmd, clen);
    td.port_ctrl1[port] = 0x00040505;
    td.port_ctrl2[port] = 0x00020f00;
    td.regdata[0] = (port & 3) | reg;
    td.in_size = size; td.out_size = size;
    td.in = txb; td.out = rxb;
    sio2_mc_transfer_init();
    sio2_transfer(&td);
    log_td(port, &td);
    return (((td.stat6c >> 13) & 1) || ((td.stat6c >> 14) & 3)) ? -1 : 0;
}

static int cmd_handler(int port, int slot, sio2_transfer_data_t *d)
{
    int r;
    (void)slot;
    sio2_mc_transfer_init();
    r = sio2_transfer(d);
    log_td(port, d);
    return r;
}
static int devid_handler(int port, int slot)
{
    (void)slot;
    return (port & 1) << 3;
}

/* resultat : bit0 = probe initial OK, bit1 = F3 OK, bit2 = F7 OK, bit3 = SecrAuthCard OK,
              bit4 = specs OK, bit5 = probe final OK */
static int run_test(int port, int mode)
{
    static const u8 c11[] = {0x81, 0x11}, cF3[] = {0x81, 0xF3, 0x00}, cF7[] = {0x81, 0xF7, 0x01};
    static const u8 c28[] = {0x81, 0x28}, c27[] = {0x81, 0x27, 0x5A}, c26[] = {0x81, 0x26, 0x5A};
    int r = 0, a;
    u8 mk[2] = {(u8)port, (u8)mode};
    addlog(port, 2, 0, mk, 2, 0, 0);
    if (!mc_cmd(port, 0x00100440, c11, 2, 4)) r |= 1;
    if (!mc_cmd(port, 0x00140540, cF3, 3, 5)) r |= 2;
    if (mode == 1 && !mc_cmd(port, 0x00140540, cF7, 3, 5)) r |= 4;
    SecrSetMcCommandHandler(cmd_handler);
    SecrSetMcDevIDHandler(devid_handler);
    a = SecrAuthCard(port, 0, (port & 1) << 3);
    addlog(port, 1, (u32)a, 0, 0, 0, 0);
    if (a == 1) r |= 8;
    if (!mc_cmd(port, 0x00140540, c28, 2, 5) && !mc_cmd(port, 0x00140540, c27, 3, 5) &&
        !mc_cmd(port, 0x00340d40, c26, 3, 13)) r |= 16;
    DelayThread(20000);
    if (!mc_cmd(port, 0x00100440, c11, 2, 4)) r |= 32;
    return r;
}

static u32 rpcbuf[(MAXLOG * sizeof(logent_t)) / 4 + 64] __attribute__((aligned(64)));

static void *rpc_handler(int fn, void *buf, int size)
{
    u32 *b = (u32 *)buf;
    (void)size;
    if (fn == 1) {
        int port = b[0] & 3, mode = b[1];
        b[0] = run_test(port, mode);
    } else if (fn == 2) {
        b[0] = nlog;
        memcpy(&b[1], lg, nlog * sizeof(logent_t));
    } else if (fn == 3) {
        nlog = 0;
    }
    return buf;
}

static SifRpcDataQueue_t qd;
static SifRpcServerData_t sd;
static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&qd, GetThreadId());
    sceSifRegisterRpc(&sd, RPC_ID, rpc_handler, rpcbuf, NULL, NULL, &qd);
    sceSifRpcLoop(&qd);
}

int _start(int argc, char *argv[])
{
    iop_thread_t t;
    int tid;
    (void)argc; (void)argv;
    memset(&t, 0, sizeof(t));
    t.attr = TH_C; t.thread = rpc_thread; t.priority = 40; t.stacksize = 0x1000;
    tid = CreateThread(&t);
    if (tid <= 0) return MODULE_NO_RESIDENT_END;
    StartThread(tid, NULL);
    return MODULE_RESIDENT_END;
}
