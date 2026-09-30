/*
 * MMDUMP.ELF - sauvegarde integrale de la carte Datel "Mega Memory" (violette)
 * vers la cle USB : mass:/BAKMEGAMEMORYMC/
 * Lecture seule : rien n'est jamais ecrit sur la carte.
 * Format produit : image brute 528 octets/page (512 + 16 ECC) = format VMC OPL/PCSX2.
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

#define RPC_ID 0x4D4D0001
#define PAGE_RAW 528
#define CHUNK 16
#define OUTDIR "mass:/BAKMEGAMEMORYMC"

#define IRX(n) extern unsigned char n[]; extern unsigned int size_##n;
IRX(mmdump_irx) IRX(iomanX_irx) IRX(fileXio_irx) IRX(usbd_irx) IRX(bdm_irx) IRX(bdmfs_fatfs_irx) IRX(usbmass_bd_irx)

static SifRpcClientData_t cd __attribute__((aligned(64)));
static u32 sbuf[16] __attribute__((aligned(64)));
static u32 rbuf[(8 + CHUNK * PAGE_RAW) / 4 + 16] __attribute__((aligned(64)));
static u8 wbuf[CHUNK * PAGE_RAW * 4] __attribute__((aligned(64)));
static char logbuf[8192];
static int loglen;

static void LOG(const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    scr_printf("%s", tmp);
    if (loglen + (int)strlen(tmp) < (int)sizeof(logbuf) - 1) {
        strcpy(logbuf + loglen, tmp);
        loglen += strlen(tmp);
    }
}

static int rpc(int fn, int ssize, int rsize)
{
    return SifCallRpc(&cd, fn, 0, sbuf, ssize, rbuf, (rsize + 63) & ~63, NULL, NULL);
}

static const char *label(int r)
{
    switch (r & 0x0f) {
    case 0: return "aucune carte";
    case 1: return "deja active";
    case 2: return "activee (MagicGate OK)";
    case 3: return "activee (sans MagicGate)";
    case 5: return "echec F7";
    case 6: return "echec specs";
    default: return "?";
    }
}

static void load(const char *name, void *p, unsigned int sz)
{
    int ret = 0, id = SifExecModuleBuffer(p, sz, 0, NULL, &ret);
    if (id < 0) LOG("   ERREUR chargement %s (%d)\n", name, id);
}

static void halt(const char *msg)
{
    LOG("\n   %s\n", msg);
    while (1) sleep(1);
}

/* taille en pages : specs 0x26 (octets 7..10), sinon repliement d'adresse (lecture seule) */
static u32 card_pages(int port, u8 *raw)
{
    u32 pages = 0, psize;
    sbuf[0] = port;
    rpc(2, 16, 64);
    memcpy(raw, &rbuf[1], 16);
    if (rbuf[0]) {
        psize = raw[3] | (raw[4] << 8);
        pages = raw[7] | (raw[8] << 8) | (raw[9] << 16) | ((u32)raw[10] << 24);
        if (psize == 512 && pages >= 1024 && pages <= (1u << 20) && !(pages & 1023))
            return pages;
    }
    return 0;
}

static int read_chunk(int port, u32 page, int count, u8 *dst, int *ck)
{
    sbuf[0] = port; sbuf[1] = page; sbuf[2] = count;
    rpc(3, 16, 8 + count * PAGE_RAW);
    memcpy(dst, (u8 *)rbuf + 8, count * PAGE_RAW);
    *ck += rbuf[1];
    return rbuf[0];
}

/* detecte 8/16/32/64 Mo : la page N identique a la page 0 => la carte se replie */
static u32 guess_pages(int port)
{
    static u8 p0[PAGE_RAW], pn[PAGE_RAW];
    u32 n;
    int ck = 0, i, blank = 1;
    if (read_chunk(port, 0, 1, p0, &ck) != 1) return 0;
    for (i = 0; i < 512; i++) if (p0[i] != 0xff && p0[i] != 0) { blank = 0; break; }
    if (blank) return 16384;
    for (n = 16384; n <= 65536; n <<= 1) {
        if (read_chunk(port, n, 1, pn, &ck) != 1) return n;
        if (!memcmp(p0, pn, 512)) return n;
    }
    return 131072;
}

static int dump_slot(int slot, int port)
{
    u8 raw[16];
    char path[128];
    u32 pages, p, bad = 0;
    int fd, i, n, ck = 0, wpos = 0, k;

    pages = card_pages(port, raw);
    LOG("   specs:");
    for (i = 0; i < 13; i++) LOG(" %02x", raw[i]);
    LOG("\n");
    {
        u32 g = guess_pages(port);
        LOG("   taille annoncee : %u pages / par repliement : %u pages\n", pages, g);
        if (g > pages) pages = g;
        LOG("   -> sauvegarde de %u pages (%u Mo)\n", pages, pages / 2048);
    }
    if (!pages) { LOG("   lecture impossible, slot ignore\n"); return -1; }

    for (k = 0; k < 100; k++) {
        iox_stat_t st;
        sprintf(path, OUTDIR "/MEGAMEM_SLOT%d_%02d.mcd", slot, k);
        if (fileXioGetStat(path, &st) < 0) break;
    }
    fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) { LOG("   impossible de creer %s (%d)\n", path, fd); return -1; }
    LOG("   -> %s\n", path);

    for (p = 0; p < pages; p += CHUNK) {
        int cnt = (pages - p) < CHUNK ? (pages - p) : CHUNK;
        n = read_chunk(port, p, cnt, wbuf + wpos, &ck);
        if (n != cnt) {          /* pages illisibles : on les remplit de 0xFF et on continue */
            for (i = n; i < cnt; i++) {
                u8 *d = wbuf + wpos + i * PAGE_RAW;
                if (read_chunk(port, p + i, 1, d, &ck) != 1) { memset(d, 0xff, PAGE_RAW); bad++; }
            }
        }
        wpos += cnt * PAGE_RAW;
        if (wpos >= (int)sizeof(wbuf) || p + cnt >= pages) {
            if (fileXioWrite(fd, wbuf, wpos) != wpos) {
                fileXioClose(fd);
                LOG("\n   ERREUR d'ecriture sur la cle USB\n");
                return -1;
            }
            wpos = 0;
        }
        if ((p & 511) == 0)
            scr_printf("\r   %u / %u pages (%u%%)  erreurs:%u  ", p, pages, (unsigned)((u64)p * 100 / pages), bad);
    }
    fileXioClose(fd);
    scr_printf("\r   %u / %u pages (100%%)                 \n", pages, pages);
    LOG("   termine : %u page(s) illisible(s), %d avertissement(s) de checksum\n", bad, ck);
    return 0;
}

int main(int argc, char *argv[])
{
    int r[2], i, fd, todo[2], any;

    SifInitRpc(0);
    while (!SifIopReset("", 0)) {};
    while (!SifIopSync()) {};
    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    init_scr();
    scr_printf("\n\n");
    LOG("   MMDUMP - sauvegarde de la carte Datel Mega Memory vers USB\n");
    LOG("   par Soaresden - lecture seule, rien n'est ecrit sur la carte\n\n");

    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0) halt("ERREUR rom0:SIO2MAN");
    load("MMDUMP", mmdump_irx, size_mmdump_irx);
    load("IOMANX", iomanX_irx, size_iomanX_irx);
    load("FILEXIO", fileXio_irx, size_fileXio_irx);
    load("USBD", usbd_irx, size_usbd_irx);
    load("BDM", bdm_irx, size_bdm_irx);
    load("BDMFS_FATFS", bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
    load("USBMASS_BD", usbmass_bd_irx, size_usbmass_bd_irx);
    fileXioInit();

    while (SifBindRpc(&cd, RPC_ID, 0) < 0 || cd.server == NULL) nopdelay();

    LOG("   Attente de la cle USB");
    for (i = 0; i < 20; i++) {
        int d = fileXioDopen("mass:/");
        if (d >= 0) { fileXioDclose(d); break; }
        scr_printf(".");
        sleep(1);
    }
    if (i == 20) halt("Cle USB introuvable (FAT32 ou exFAT, port USB avant). Eteins et reessaie.");
    LOG(" OK\n");
    fileXioMkdir(OUTDIR, 0777);

    for (i = 0; i < 2; i++) {
        sbuf[0] = 2 + i;
        rpc(1, 16, 64);
        r[i] = rbuf[0];
        LOG("   Slot %d : %s%s (0x%02x)\n", i + 1, label(r[i]), (r[i] & 0x10) ? " + verifiee" : "", r[i]);
    }
    /* on sauvegarde la ou les cartes qu'on vient d'activer (= la Datel) ;
       si aucune (deja activee avant sans eteindre), on sauvegarde toutes les cartes presentes */
    any = 0;
    for (i = 0; i < 2; i++) { todo[i] = ((r[i] & 0x0f) == 2 || (r[i] & 0x0f) == 3); any |= todo[i]; }
    if (!any) for (i = 0; i < 2; i++) todo[i] = ((r[i] & 0x0f) == 1);

    for (i = 0; i < 2; i++) {
        if (!todo[i]) continue;
        LOG("\n   === Sauvegarde slot %d ===\n", i + 1);
        dump_slot(i + 1, 2 + i);
    }

    fd = fileXioOpen(OUTDIR "/MMDUMP_LOG.TXT", FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd >= 0) { fileXioWrite(fd, logbuf, loglen); fileXioClose(fd); }
    fileXioUmount("mass:");

    halt("FINI. Tu peux eteindre la console et recuperer la cle USB.");
    return 0;
}
