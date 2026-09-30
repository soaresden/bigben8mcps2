# Building

Requirements: the [ps2dev](https://github.com/ps2dev/ps2dev) toolchain (ps2sdk, gsKit, ps2sdk-ports zlib).
The easiest way is the official Docker image:

```sh
./build.sh docker
```

Inside a toolchain shell (`PS2DEV`/`PS2SDK` set, or the defaults `/usr/local/ps2dev`):

```sh
./build.sh
```

Outputs are copied to `bin/`. Each component can also be built alone:

| Script | Output |
|---|---|
| `src/secrpatch/build.sh` | `megamem_secrpatch.irx` |
| `src/resident/build.sh` | `mmres.bin` (resident driver blob, linked at `0x000C0000`) + `mmres_bin.c` |
| `src/mmboot/build.sh` | `MMBOOT.ELF` (embeds the IRX above, the resident blob, ps2sdk's USB/BDM/fileXio/HDD modules, and reuses XMEM2's `gfx.c` + font) |
| `src/xmem2/build.sh` | `XMEM2.ELF` (embeds the IRX above, the resident blob, and ps2sdk's USB/BDM/fileXio modules) |
| `src/tools/mmdump/build.sh` | `MMDUMP.ELF` |
| `src/tools/mmdiag/build.sh` | `MMDIAG.ELF` |

Generated sources are committed (`src/xmem2/src/font_data.c`: DejaVu Sans 15 px atlas, 512×256, Latin-1;
`src/xmem2/src/folder_icon.c`: the archive folder's cube icon and `icon.sys`). To regenerate them you need Python 3
and Pillow: `python3 src/xmem2/gen/mkfont.py && python3 src/xmem2/gen/mkicon.py`.

## Testing without a console

XMEM2 runs in the [Play!](https://purei.org) emulator (`Play --elf XMEM2.ELF`). Play!'s folder-based memory cards
(`Play Data Files/vfs/mc0`, `mc1`) accept save folders copied in directly. USB, SECRMAN and the resident driver can't
be tested there. MMBOOT's menu, config and chain-loading can: Play! has no real kernel to hook, so MMBOOT asks
*"Driver NOT installed: launch anyway?"*, answer `X`. ELFs copied into `vfs/mc0/<folder>/` show up in the menu.
