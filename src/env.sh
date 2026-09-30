# Toolchain environment (works inside the official ps2dev/ps2dev Docker image)
: "${PS2DEV:=/usr/local/ps2dev}"
: "${PS2SDK:=$PS2DEV/ps2sdk}"
: "${GSKIT:=$PS2DEV/gsKit}"
export PS2DEV PS2SDK GSKIT
export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin"
EE_CC=mips64r5900el-ps2-elf-gcc
IOP_CC=mipsel-none-elf-gcc
EE_CFLAGS="-D_EE -G0 -O2 -Wall -I$PS2SDK/ee/include -I$PS2SDK/common/include -I$PS2SDK/ports/include -I$GSKIT/include"
EE_LDFLAGS="-T$PS2SDK/ee/startup/linkfile -O2 -L$PS2SDK/ee/lib -L$PS2SDK/ports/lib -L$GSKIT/lib -Wl,-zmax-page-size=128"
IOP_CFLAGS="-D_IOP -fno-builtin -G0 -Os -Wall -I$PS2SDK/iop/include -I$PS2SDK/common/include -I. -msoft-float -mno-explicit-relocs"
IRX="$PS2SDK/iop/irx"
# build_irx <out.irx> <sources...> : builds an IOP module (IRX)
build_irx() {
    out=$1; shift
    objs=""
    for s in "$@"; do
        o=build/$(basename "$s" .c).o
        extra=""; case $s in *imports*) extra="-fno-toplevel-reorder";; esac
        $IOP_CC $IOP_CFLAGS $extra -c "$s" -o "$o"; objs="$objs $o"
    done
    $IOP_CC $IOP_CFLAGS -T$PS2SDK/iop/startup/linkfile -Os -o build/irx.elf $objs -nostdlib -dc -r
    mipsel-none-elf-strip --strip-unneeded --remove-section=.pdr --remove-section=.comment \
        --remove-section=.mdebug.abi32 --remove-section=.gnu.attributes -o build/irx.stripped.elf build/irx.elf
    iopfixup --rb --irx1 --allow-zero-text -o "$out" build/irx.stripped.elf
}
