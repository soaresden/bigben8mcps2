#!/bin/sh
set -e
cd "$(dirname "$0")"; . ../env.sh
mkdir -p build
[ -f ../secrpatch/build/megamem_secrpatch.irx ] || sh ../secrpatch/build.sh
[ -f ../resident/build/mmres_bin.c ] || sh ../resident/build.sh
# generated sources (font atlas, archive-folder icon) are committed; regenerate with:
#   python3 gen/mkfont.py && python3 gen/mkicon.py   (needs Pillow + DejaVu fonts)
[ -f ../xmraw/build/xmraw.irx ] || sh ../xmraw/build.sh
bin2c ../secrpatch/build/megamem_secrpatch.irx build/secrpatch_irx.c secrpatch_irx
bin2c ../xmraw/build/xmraw.irx build/xmraw_irx.c xmraw_irx
for m in iomanX fileXio usbd bdm bdmfs_fatfs usbmass_bd; do bin2c $IRX/$m.irx build/${m}_irx.c ${m}_irx; done
OBJS=""
for f in src/*.c ../resident/res_install.c ../resident/build/mmres_bin.c build/*_irx.c; do
    o=build/$(basename $f .c).o
    $EE_CC $EE_CFLAGS $EXTRA_CFLAGS -Wno-unused-variable -Isrc -I../resident -c $f -o $o
    OBJS="$OBJS $o"
done
$EE_CC $EE_LDFLAGS -o build/XMEM2.ELF $OBJS -lgskit -ldmakit -lpad -lmc -lfileXio -lpatches -lz -lm -lkernel
mips64r5900el-ps2-elf-strip -s build/XMEM2.ELF
echo "built src/xmem2/build/XMEM2.ELF"
