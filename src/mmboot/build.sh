#!/bin/sh
set -e
cd "$(dirname "$0")"; . ../env.sh
mkdir -p build
[ -f ../secrpatch/build/megamem_secrpatch.irx ] || sh ../secrpatch/build.sh
[ -f ../resident/build/mmres_bin.c ] || sh ../resident/build.sh
bin2c ../secrpatch/build/megamem_secrpatch.irx build/secrpatch_irx.c secrpatch_irx
for m in iomanX fileXio usbd bdm bdmfs_fatfs usbmass_bd ps2dev9 ata_bd; do bin2c $IRX/$m.irx build/${m}_irx.c ${m}_irx; done
# the UI reuses XMEM2's renderer and font (src/xmem2/src/gfx.c, font_data.c)
OBJS=""
for f in main.c ../xmem2/src/gfx.c ../xmem2/src/font_data.c ../resident/res_install.c ../resident/build/mmres_bin.c build/*_irx.c; do
    o=build/$(basename $f .c).o
    $EE_CC $EE_CFLAGS $EXTRA_CFLAGS -I../resident -I../xmem2/src -c $f -o $o
    OBJS="$OBJS $o"
done
$EE_CC $EE_LDFLAGS -o build/MMBOOT.ELF $OBJS -lelf-loader -lgskit -ldmakit -lpatches -lpad -lmc -lfileXio -lm -lkernel
mips64r5900el-ps2-elf-strip -s build/MMBOOT.ELF
echo "built src/mmboot/build/MMBOOT.ELF"
