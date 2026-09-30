/*
 * XMRAW.IRX - raw page reads of a PS2 memory card for XMEM2 (Datel X-MEM archives are "deleted" files
 * that MCMAN can't open). Same SIO2 command sequence as MMDUMP (tested on the Datel card):
 *   81 23 <page> , 4 x 81 43 80 (128 bytes each), 81 43 10 (16 bytes ECC), 81 81
 * Read-only: no write or erase command is ever sent. RPC server 0x584D5257 ("XMRW").
 *   fn 1: in u32 slot (0/1), u32 page, u32 count (<= 16)   out: u32 pages_ok, u8 data[count * 512]
 */
#include <irx.h>
#include <loadcore.h>
#include <sysclib.h>
#include <sio2man.h>
#include <thbase.h>
#include <sifcmd.h>

IRX_ID("xmraw", 1, 0);

#define RPC_ID 0x584D5257
#define MAXPAGES 16

static sio2_transfer_data_t td;
static u8 txb[160], rxb[160];
static u32 rpcbuf[(16 + MAXPAGES * 512) / 4] __attribute__((aligned(64)));

#define CTRL1 0x00040505
#define CTRL2 0x00020f00

static int mc_cmd(int port, u32 reg, const u8 *cmd, int clen, int size)
{
    u32 s;
    memset(&td, 0, sizeof(td));
    memset(txb, 0, sizeof(txb));
    memset(rxb, 0, sizeof(rxb));
    memcpy(txb, cmd, clen);
    td.port_ctrl1[port] = CTRL1;
    td.port_ctrl2[port] = CTRL2;
    td.regdata[0] = (port & 3) | reg;
    td.regdata[1] = 0;
    td.in_size = size;
    td.out_size = size;
    td.in = txb;
    td.out = rxb;
    sio2_mc_transfer_init();
    sio2_transfer(&td);
    s = td.stat6c;
    return (((s >> 13) & 1) || ((s >> 14) & 3)) ? -1 : 0;
}

static int read_page(int port, u32 page, u8 *out)
{
    u8 c23[7], x;
    static const u8 c43d[] = {0x81, 0x43, 0x80};
    static const u8 c43e[] = {0x81, 0x43, 0x10};
    static const u8 c81[] = {0x81, 0x81};
    int k, j, bad = 0;

    c23[0] = 0x81; c23[1] = 0x23;
    c23[2] = page & 0xff; c23[3] = (page >> 8) & 0xff; c23[4] = (page >> 16) & 0xff; c23[5] = 0;
    c23[6] = c23[2] ^ c23[3] ^ c23[4];
    if (mc_cmd(port, 0x00240940, c23, 7, 9)) return -1;
    for (k = 0; k < 4; k++) {
        if (mc_cmd(port, 0x02188640, c43d, 3, 134)) return -1;
        memcpy(out + k * 128, rxb + 4, 128);
        for (x = 0, j = 0; j < 128; j++) x ^= rxb[4 + j];
        if (x != rxb[132]) bad = 1;
    }
    if (mc_cmd(port, 0x00581640, c43e, 3, 22)) return -1;       /* ECC, not used */
    if (mc_cmd(port, 0x00100440, c81, 2, 4)) return -1;
    return bad ? -2 : 0;
}

static void *rpc_handler(int fn, void *buf, int size)
{
    u32 *b = (u32 *)buf;
    (void)size;
    if (fn == 1) {
        int port = 2 + (b[0] & 1);
        u32 page = b[1], count = b[2], n, t;
        u8 *out = (u8 *)&b[4];
        if (count > MAXPAGES) count = MAXPAGES;
        for (n = 0; n < count; n++) {
            for (t = 0; t < 3; t++) if (read_page(port, page + n, out + n * 512) == 0) break;
            if (t == 3) break;
        }
        b[0] = n;
    } else b[0] = 0;
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
    t.stacksize = 0x800;
    tid = CreateThread(&t);
    if (tid <= 0) return MODULE_NO_RESIDENT_END;
    StartThread(tid, NULL);
    return MODULE_RESIDENT_END;
}
