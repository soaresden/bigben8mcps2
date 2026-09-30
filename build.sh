#!/bin/sh
# Build everything and copy the binaries to bin/.
#   inside a ps2dev toolchain :  ./build.sh
#   with Docker               :  ./build.sh docker
set -e
cd "$(dirname "$0")"
if [ "$1" = "docker" ]; then
    exec docker run --rm -v "$PWD":/src -w /src ps2dev/ps2dev sh ./build.sh
fi
sh src/secrpatch/build.sh
sh src/xmraw/build.sh
sh src/resident/build.sh
sh src/mmboot/build.sh
sh src/xmem2/build.sh
sh src/tools/mmdump/build.sh
sh src/tools/mmdiag/build.sh
mkdir -p bin/tools
cp src/xmem2/build/XMEM2.ELF src/mmboot/build/MMBOOT.ELF src/secrpatch/build/megamem_secrpatch.irx bin/
cp src/tools/mmdump/build/MMDUMP.ELF src/tools/mmdiag/build/MMDIAG.ELF bin/tools/
echo "OK -> bin/"
