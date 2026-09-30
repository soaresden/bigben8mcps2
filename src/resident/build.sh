#!/bin/sh
# Builds the resident driver blob (linked at 0x000C0000) -> build/mmres.bin
set -e
cd "$(dirname "$0")"; . ../env.sh
mkdir -p build
CF="-G0 -O2 -Wall -fno-pic -mno-abicalls -ffreestanding -fno-builtin -nostdlib"
$EE_CC $CF -c res_asm.S -o build/res_asm.o
$EE_CC $CF -c res.c -o build/res.o
mips64r5900el-ps2-elf-ld -T res.ld -o build/res.elf build/res_asm.o build/res.o
mips64r5900el-ps2-elf-objcopy -O binary build/res.elf build/mmres.bin
bin2c build/mmres.bin build/mmres_bin.c mmres_bin
echo "built src/resident/build/mmres.bin"
