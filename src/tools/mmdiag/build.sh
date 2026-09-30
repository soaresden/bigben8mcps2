#!/bin/sh
set -e
cd "$(dirname "$0")"; . ../../env.sh
mkdir -p build
build_irx build/MMDIAG.IRX mmdiag.c imports.c
bin2c build/MMDIAG.IRX build/mmdiag_irx.c mmdiag_irx
for m in iomanX fileXio usbd bdm bdmfs_fatfs usbmass_bd; do bin2c $IRX/$m.irx build/${m}_irx.c ${m}_irx; done
OBJS=""
for f in main.c build/*_irx.c; do o=build/$(basename $f .c).o; $EE_CC $EE_CFLAGS -c $f -o $o; OBJS="$OBJS $o"; done
$EE_CC $EE_LDFLAGS -o build/MMDIAG.ELF $OBJS -lfileXio -lpatches -ldebug -lkernel
mips64r5900el-ps2-elf-strip -s build/MMDIAG.ELF
echo "built src/tools/mmdiag/build/MMDIAG.ELF"
