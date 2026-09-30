/*
 * MMDUMP.IRX - acces bas niveau a la carte Datel "Mega Memory" (sequences reprises
 * de SIOSTUFF.COD, MegaMem v1.70). Serveur RPC 0x4D4D0001.
 *   fn 1 : activer   in: u32 port(2/3)            out: u32 code
 *   fn 2 : specs     in: u32 port                 out: u32 ok, u8 raw[16]
 *   fn 3 : lire      in: u32 port, page, count     out: u32 ok, u32 cksum_warn,
 *                                                        u8 data[count*528]
 */
#include <irx.h>
#include <loadcore.h>
#include <sysclib.h>
#include <sio2man.h>
#include <secrman.h>
#include <thbase.h>
#include <sifcmd.h>

IRX_ID("mmdump", 1, 0);

#define RPC_ID 0x4D4D0001
#define MAXPAGES 16
#define PAGE_RAW 528

#define R_NOCARD 0
#define R_ALREADY 1
#define R_MG_OK 2
#define R_NO_MG 3
#define R_F3_FAIL 4
#define R_F7_FAIL 5
#define R_SPEC_FAIL 6
#define R_VERIFIED 0x10

static sio2_transfer_data_t td;
static u8 txb[160], rxb[160];
static u32 rpcbuf[(8 + MAXPAGES * PAGE_RAW) / 4 + 16] __attribute__((aligned(64)));

static int xfer_err(void)
{
    u32 s = td.stat6c;
    return ((s >> 13) & 1) || ((s >> 14) & 3);
}

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

static u8 specs[16];
static int get_specs(int port)
{
    static const u8 c28[] = {0x81, 0x28};
    static const u8 c27[] = {0x81, 0x27, 0x5A};
    static const u8 c26[] = {0x81, 0x26, 0x5A};
    if (mc_cmd(port, CTRL1, CTRL2, 0x00140540, c28, 2, 5)) return -1;
    if (mc_cmd(port, CTRL1, CTRL2, 0x00140540, c27, 3, 5)) return -1;
    if (mc_cmd(port, CTRL1, CTRL2, 0x00340d40, c26, 3, 13)) return -1;
    memset(specs, 0, sizeof(specs));
    memcpy(specs, rxb, 13);
    return 0;
}

static int activate(int port)
{
    static const u8 cF3[] = {0x81, 0xF3, 0x00};
    static const u8 cF7[] = {0x81, 0xF7, 0x01};
    int mg_fail, r;

    if (probe11(port) == 0) return R_ALREADY;
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

static int activate_retry(int port)
{
    int i, r = 0;
    for (i = 0; i < 3; i++) {
        r = activate(port);
        if (r == R_ALREADY || (r & 0x0f) == R_MG_OK || (r & 0x0f) == R_NO_MG) break;
        DelayThread(100000);
    }
    if (r == R_F3_FAIL) r = R_NOCARD;
    return r;
}

/* lecture d'une page = sequence de SIOSTUFF 0x388 : 81 23 adr, 4x 81 43 80, 81 43 10, 81 81 */
static int cksum_warn;
static int read_page(int port, u32 page, u8 *out)
{
    u8 c23[7], i, x;
    static const u8 c43d[] = {0x81, 0x43, 0x80};
    static const u8 c43e[] = {0x81, 0x43, 0x10};
    static const u8 c81[] = {0x81, 0x81};
    int k, j;

    c23[0] = 0x81; c23[1] = 0x23;
    c23[2] = page & 0xff; c23[3] = (page >> 8) & 0xff; c23[4] = (page >> 16) & 0xff; c23[5] = 0;
    c23[6] = c23[2] ^ c23[3] ^ c23[4];
    if (mc_cmd(port, CTRL1, CTRL2, 0x00240940, c23, 7, 9)) return -1;

    for (k = 0; k < 4; k++) {
        if (mc_cmd(port, CTRL1, CTRL2, 0x02188640, c43d, 3, 134)) return -1;
        memcpy(out + k * 128, rxb + 4, 128);
        for (x = 0, j = 0; j < 128; j++) x ^= rxb[4 + j];
        if (x != rxb[132]) cksum_warn++;
    }
    if (mc_cmd(port, CTRL1, CTRL2, 0x00581640, c43e, 3, 22)) return -1;
    memcpy(out + 512, rxb + 4, 16);
    for (x = 0, i = 0; i < 16; i++) x ^= rxb[4 + i];
    if (x != rxb[20]) cksum_warn++;
    if (mc_cmd(port, CTRL1, CTRL2, 0x00100440, c81, 2, 4)) return -1;
    return 0;
}

static void *rpc_handler(int fn, void *buf, int size)
{
    u32 *b = (u32 *)buf;
    int port = b[0] & 3;
    (void)size;
    switch (fn) {
    case 1:
        b[0] = activate_retry(port);
        break;
    case 2:
        b[0] = (get_specs(port) == 0);
        memcpy(&b[1], specs, 16);
        break;
    case 3: {
        u32 page = b[1], count = b[2], n, t;
        int ok = 1;
        u8 *out = (u8 *)&b[2];
        if (count > MAXPAGES) count = MAXPAGES;
        cksum_warn = 0;
        for (n = 0; n < count && ok; n++) {
            for (t = 0; t < 3; t++)
                if (read_page(port, page + n, out + n * PAGE_RAW) == 0) break;
            if (t == 3) ok = 0;
        }
        b[0] = ok ? count : n - 1;   /* nb de pages lues correctement */
        b[1] = cksum_warn;
        break;
    }
    default:
        b[0] = 0;
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
    t.attr = TH_C;
    t.thread = rpc_thread;
    t.priority = 40;
    t.stacksize = 0x1000;
    tid = CreateThread(&t);
    if (tid <= 0) return MODULE_NO_RESIDENT_END;
    StartThread(tid, NULL);
    return MODULE_RESIDENT_END;
}
