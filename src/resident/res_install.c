/*
 * Installer for the MegaMem resident driver (clean-room).
 * 1. copies mmres.bin to 0x000C0000 (and zeroes its bss),
 * 2. patches the kernel so LoadExecPS2 no longer clears 0x82000-0x102000
 *    ("lui a0,0x0008 ... ori a0,a0,0x2000" -> "lui a0,0x0010"),
 * 3. finds the end of the kernel ExecPS2 handler ("mfc0 k0,Status ... eret"), copies it into the
 *    driver trampoline and replaces it with "j res_tramp",
 * 4. registers the kernel helper as syscall 252.
 */
#include <kernel.h>
#include <string.h>
#include "res_install.h"

extern unsigned char mmres_bin[];
extern unsigned int size_mmres_bin;

typedef struct { u32 magic, ver, tramp, tail, setreg, iop_patch, status, end, ksetreg, origsetreg; } ResHdr;
#define RES_BASE 0x000C0000u
#define RES_SYSCALL 252
#define MFC0_K0_SR 0x401A6000u
#define ERET 0x42000018u

#define SYS_SIFSETREG 0x79

static u32 hook_jmp(void)
{
    const ResHdr *nh = (const ResHdr *)mmres_bin;
    return 0x08000000u | (((nh->tramp | 0x80000000u) >> 2) & 0x03FFFFFFu);
}

static int execps2_hooked(void)
{
    volatile u32 *exe = (volatile u32 *)GetSyscallHandler(7);
    u32 i, jmp = hook_jmp(), found = 0;
    if (!exe) return 0;
    ee_kmode_enter();
    for (i = 0; i < 256; i++) if (exe[i] == jmp) { found = 1; break; }
    ee_kmode_exit();
    return found;
}

int res_read_status(unsigned int st[8])
{
    const ResHdr *h = (const ResHdr *)RES_BASE;
    int i;
    for (i = 0; i < 8; i++) st[i] = 0;
    if (h->magic != 0x524D4D58u || !execps2_hooked()) return 0;
    memcpy(st, (void *)h->status, 32);
    return 1;
}

/* v2: put res_k_setreg in the syscall table (after the blob is in place) */
static u32 hook_sifsetreg(u32 keep_orig)
{
    ResHdr *h = (ResHdr *)RES_BASE;
    u32 want, cur;
    if (h->ver < 2) return 0;
    want = h->ksetreg | 0x80000000u;
    cur = (u32)GetSyscallHandler(SYS_SIFSETREG);
    if (!cur) return 0;
    if (cur == want) {                           /* already ours: the original was saved by the first install */
        *(volatile u32 *)h->origsetreg = keep_orig;
        FlushCache(0);
        return keep_orig ? 2 : 0;
    }
    *(volatile u32 *)h->origsetreg = cur;
    FlushCache(0);
    FlushCache(2);
    SetSyscall(SYS_SIFSETREG, (void *)want);
    return 1;
}

int res_install(ResInfo *info)
{
    ResHdr *h = (ResHdr *)RES_BASE;
    volatile u32 *exe, *k;
    u32 i, mfc = 0, er = 0, n;
    memset(info, 0, sizeof(*info));

    exe = (volatile u32 *)GetSyscallHandler(7);          /* ExecPS2 */
    info->execps2 = (u32)exe;
    if (!exe) return -2;

    {   /* deja installe dans cette session (sans extinction) ? on garde la queue d'origine */
        const ResHdr *nh = (const ResHdr *)mmres_bin;
        u32 jmp = 0x08000000u | (((nh->tramp | 0x80000000u) >> 2) & 0x03FFFFFFu), saved[8], found = 0;
        ee_kmode_enter();
        for (i = 0; i < 256; i++) if (exe[i] == jmp) { found = 1; info->hookoff = i * 4; break; }
        ee_kmode_exit();
        if (found && h->magic == 0x524D4D58u) {
            int intr = DI();
            u32 st[8], orig = h->ver >= 2 ? *(volatile u32 *)h->origsetreg : 0;
            memcpy(saved, (void *)h->tail, 32);
            memcpy(st, (void *)h->status, 32);          /* counters survive a refresh (diagnostics) */
            memcpy((void *)RES_BASE, mmres_bin, size_mmres_bin);
            memset((void *)(RES_BASE + size_mmres_bin), 0, h->end - RES_BASE - size_mmres_bin);
            memcpy((void *)h->tail, saved, 32);
            memcpy((void *)h->status, st, 32);
            info->khook = hook_sifsetreg(orig);
            if (intr) EI();
            FlushCache(0);
            FlushCache(2);
            SetSyscall(RES_SYSCALL, (void *)h->iop_patch);
            info->ok = 2;
            return 0;
        }
    }

    memcpy((void *)RES_BASE, mmres_bin, size_mmres_bin);
    if (h->magic != 0x524D4D58u) return -1;
    memset((void *)(RES_BASE + size_mmres_bin), 0, h->end - RES_BASE - size_mmres_bin);
    FlushCache(0);

    ee_kmode_enter();
    for (i = 0; i < 256 && !mfc; i++) {
        if (exe[i] == MFC0_K0_SR) {
            for (n = 1; n <= 7; n++) if (exe[i + n] == ERET) { mfc = i + 1; er = i + n; break; }
        }
    }
    if (mfc) {
        volatile u32 *tail = (volatile u32 *)(h->tail | 0x80000000u);
        mfc--;
        info->hookoff = mfc * 4;
        info->taillen = er - mfc + 1;
        for (i = 0; i < 8; i++) tail[i] = 0;                       /* nop */
        for (i = 0; i < info->taillen; i++) tail[8 - info->taillen + i] = exe[mfc + i];
        /* protection de la zone 0xC0000 avant d'accrocher le noyau */
        for (k = (volatile u32 *)0x80000000u; k < (volatile u32 *)0x80080000u; k++) {
            if ((k[0] == 0x3C040008u || k[0] == 0x3C040010u) && k[2] == 0x34842000u) { k[0] = 0x3C040010u; info->clearpatch++; }
        }
        if (info->clearpatch) {
            exe[mfc] = 0x08000000u | (((h->tramp | 0x80000000u) >> 2) & 0x03FFFFFFu);   /* j res_tramp */
            exe[mfc + 1] = 0;                                                             /* nop */
            info->ok = 1;
        }
    }
    ee_kmode_exit();
    FlushCache(0);
    FlushCache(2);
    if (!info->ok) return mfc ? -4 : -3;
    SetSyscall(RES_SYSCALL, (void *)h->iop_patch);
    info->khook = hook_sifsetreg(0);
    FlushCache(0);
    FlushCache(2);
    return 0;
}
