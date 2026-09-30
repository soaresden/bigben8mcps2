/*
 * SECRPATCH.IRX - reproduit le "pilote" du CD Datel MegaMem v1.70 :
 * cherche le module "secrman_for_cex" en RAM IOP et remplace une instruction
 * (nom - 0x2BA0) par "li v0,1" : SecrAuthCard reussit toujours, la carte Datel
 * (sans MagicGate) est acceptee par MCMAN. Garde-fou : l'adresse doit tomber
 * dans SecrAuthCard (table d'export "secrman", entree 6) -> bit XMP_SANE, informatif.
 * Retour de _start : bits 8.. = etat (voir XMP_*).
 */
#include <irx.h>
#include <loadcore.h>

IRX_ID("secrpatch", 1, 0);

#define XMP_STR     (1 << 8)
#define XMP_EXPORT  (1 << 9)
#define XMP_SANE    (1 << 10)
#define XMP_PATCHED (1 << 11)
#define XMP_ALREADY (1 << 12)
#define LI_V0_1 0x24020001

static int match(const unsigned char *p, const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) if (p[i] != (unsigned char)s[i]) return 0;
    return 1;
}

int _start(int argc, char *argv[])
{
    static const char rev[] = "xec_rof_namrces";   /* a l'envers : ne se trouve pas lui-meme */
    char pat[16];
    unsigned int a, str = 0, exp = 0, auth = 0, target;
    int i, r = 0;

    for (i = 0; i < 15; i++) pat[i] = rev[14 - i];
    pat[15] = 0;

    for (a = 0x40000; a < 0x80000; a += 4)
        if (match((const unsigned char *)a, pat, 16)) { str = a; break; }
    if (!str) return MODULE_NO_RESIDENT_END;
    r |= XMP_STR;

    for (a = 0x1000; a < 0x200000; a += 4) {
        const unsigned int *w = (const unsigned int *)a;
        if (w[0] == 0x41c00000 && match((const unsigned char *)(a + 12), "secrman", 8)) { exp = a; break; }
    }
    if (exp) {
        r |= XMP_EXPORT;
        auth = ((const unsigned int *)exp)[5 + 6];
    }
    target = str - 0x2BA0;
    if (auth && target >= auth && target < auth + 0x800) r |= XMP_SANE;

    if (*(volatile unsigned int *)target == LI_V0_1) r |= XMP_ALREADY;
    else {   /* comme le CD Datel : on patche meme si le garde-fou n'a pas pu confirmer */
        *(volatile unsigned int *)target = LI_V0_1;
        FlushIcache();
        r |= XMP_PATCHED;
    }
    return MODULE_NO_RESIDENT_END | r;
}
