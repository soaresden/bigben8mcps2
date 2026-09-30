/*
 * MegaMem resident driver (C part). Clean-room: behaviour documented in docs/, code written from scratch.
 *  - res_scan_program : kernel mode, at the end of ExecPS2. Redirects every SifSetReg stub of the new
 *                       program (li v1,0x79 / syscall / jr ra / nop) to res_setreg.
 *  - res_on_setreg    : user mode. SifSetReg(0x80000000,0) = IOP reset starts, then
 *                       SifSetReg(0x80000002,1) = IOP rebooted and RPC ready -> syscall 252.
 *  - res_iop_patch    : kernel mode (syscall 252). Looks at IOP RAM through the EE mapping 0xBC000000,
 *                       finds SECRMAN ("secrman_for_cex"), checks the target lies inside SecrAuthCard
 *                       (export #6 of library "secrman") and writes "li v0,1" at name - 0x2BA0.
 */
typedef unsigned int u32;
typedef unsigned char u8;

#define IOP_RAM 0xBC000000u
#define LI_V0_1 0x24020001u

u32 res_status[8];             /* 0: programs hooked, 1: stubs patched, 2: IOP resets seen (stub path), 3: patches applied,
                                  4: last result, 5: last target, 6: skipped (not sane), 7: RPC inits seen (kernel hook) */
u32 res_orig_setreg;           /* original kernel handler of syscall 0x79, set by the installer */
static u32 reset_pending;
u8 res_regs[0x1D0] __attribute__((aligned(16)));
u8 res_stack[2048] __attribute__((aligned(16)));
u32 res_stack_top_dummy;
extern void res_setreg(void);
extern int res_call_patch(void);

void res_scan_program(void)
{
    volatile u32 *p = (volatile u32 *)0xA0100000u, *end = (volatile u32 *)0xA2000000u;
    u32 j = 0x08000000u | ((((u32)&res_setreg) >> 2) & 0x03FFFFFFu);
    res_status[0]++;
    reset_pending = 0;
    for (; p + 3 < end; p++) {
        if (p[0] == 0x24030079u && p[1] == 0x0000000Cu && p[2] == 0x03E00008u && p[3] == 0u) {
            p[0] = j;
            p[1] = 0;
            res_status[1]++;
        }
    }
}

void res_on_setreg(u32 reg, u32 val)
{
    if (reg == 0x80000000u && val == 0) reset_pending = 1;
    else if (reg == 0x80000002u && val == 1 && reset_pending) {
        reset_pending = 0;
        res_status[2]++;
        res_call_patch();
    }
}

static int iop_match(u32 a, const char *s, int n)
{
    volatile const u8 *m = (volatile const u8 *)(IOP_RAM + a);
    int i;
    for (i = 0; i < n; i++) if (m[i] != (u8)s[i]) return 0;
    return 1;
}

int res_iop_patch(void)
{
    static const char name[] = "secrman_for_cex";
    u32 a, str = 0, exp = 0, auth = 0, target;
    volatile u32 *w;
    for (a = 0x40000; a < 0x80000; a += 4)
        if (iop_match(a, name, 16)) { str = a; break; }
    if (!str) { res_status[4] = 1; return 1; }
    for (a = 0x1000; a < 0x200000; a += 4) {
        w = (volatile u32 *)(IOP_RAM + a);
        if (w[0] == 0x41C00000u && iop_match(a + 12, "secrman", 8)) { exp = a; break; }
    }
    if (exp) auth = ((volatile u32 *)(IOP_RAM + exp))[11];
    target = str - 0x2BA0;
    res_status[5] = target;
    if (!auth || target < auth || target >= auth + 0x2000) { res_status[6]++; res_status[4] = 2; return 2; }
    w = (volatile u32 *)(IOP_RAM + target);
    if (*w != LI_V0_1) *w = LI_V0_1;
    res_status[3]++;
    res_status[4] = 0;
    return 0;
}

/*
 * Kernel-level hook (v2): the installer puts this function in the syscall table instead of SifSetReg (0x79).
 * It catches every program's SifSetReg, including packed ELFs (ps2-packer: the real code, and its SifSetReg stubs,
 * only appear after the ExecPS2 scan) and loaders that reset the IOP with their own code.
 * Runs in kernel mode, so the IOP patch is called directly.
 */
int res_k_setreg(u32 reg, u32 val)
{
    int r = ((int (*)(u32, u32))res_orig_setreg)(reg, val);
    if (reg == 0x80000002u && val == 1) {
        res_status[7]++;
        res_iop_patch();
    }
    return r;
}
