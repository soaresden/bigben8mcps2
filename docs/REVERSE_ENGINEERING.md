# Reverse engineering notes: Datel MegaMem Program v1.70 / Mega Memory 8 (PS2)

Everything below was obtained by disassembling the disc's files (EE: R5900, IOP: R3000) and by tracing the card on
a real console. Addresses refer to the PAL disc `SLES_500.09`.

* [1. Disc layout](#1-disc-layout)
* [2. The main executable](#2-the-main-executable-sles_50009)
* [3. SIOSTUFF.COD: the card driver used by the manager](#3-siostuffcod-the-card-driver-used-by-the-manager)
* [4. What the card does (and doesn't)](#4-what-the-card-does-and-doesnt)
* [5. The resident hook: how the card becomes visible to the browser and games](#5-the-resident-hook)
* [6. The X-MEM "compressed saves" format](#6-the-x-mem-compressed-saves-format)
* [7. Our clean-room reimplementation](#7-our-clean-room-reimplementation)
* [8. Known limits](#8-known-limits)

---

## 1. Disc layout

`SYSTEM.CNF`:
```
BOOT2 = cdrom0:\SLES_500.09;1
VER = 1.30
VMODE = PAL
```
The main program identifies itself as *"MegaMem Program (c) 2001 Datel Design & Development, Version 1.70"*.

| File | Size | SHA-1 |
|---|---:|---|
| `BIG.DAT` | ≈ 20 MB (19 532 KB) | *(unreadable on our disc; never referenced: padding)* |
| `BUTTONS2.PCX` | 12595 | `1deb327c2f0b06d5c0aca6e75d00e6eab50791fb` |
| `CMN/DOORMMP.PCX` | 9208 | `9d25cdc6c7880cb1964fc603cd869e9fd89cc149` |
| `CMN/LEFTMMP.PCX` | 21525 | `18922e252aa6cb8bfde73cbcdaf843c149990082` |
| `CMN/RIGHTMMP.PCX` | 16384 | `d210e656b61cb830a92f676c176f7facef3521b8` |
| `DEVICES.RAW` | 262144 | `38f808d9324f31ca5083a27917757ff4af094f49` |
| `FLAGS/FLAGS.PCX` | 16384 | `2f75475e6a90a25be355c7532f33316f8dbf8705` |
| `FONT/ARIAL10X.FON` | 32768 | `5cef376c7bf735cb245a76d69a8eccbb63a8f799` |
| `FONT/ARIAL10X.INF` | 4096 | `a2b3878472d283a326e25411607ddf686aacbac2` |
| `FONT/COMIC13X.FON` | 32768 | `00e0b83c1658bbec31a50f6877fdfbdd8558873a` |
| `FONT/COMIC13X.INF` | 4096 | `3a94d5a5cb2f2d26424c2cc875820143dba6f6e7` |
| `FR/FMID.PCX` | 12288 | `61e306983c46807a1b10b59622a65fdcd2cdf77c` |
| `FR/FRBTN2.PCX` | 12288 | `d8c0c8411bfebc6540bc0c1f1f0074288de9d807` |
| `FR/FRBTN3.PCX` | 12288 | `20a0454623b6028cf7eda9c17884100a45e70c9c` |
| `FR/FRPANEL.RAW` | 262144 | `2a18a3a69559d399cd57982b9d1b36aa9cdfaffe` |
| `FR/MMPFBUTT.PCX` | 24576 | `5c149503f034b6f3ffc97b35d58693bcc655f5dc` |
| `FR/MMPFDLG1.RAW` | 262144 | `922328e4d512ecfa9c4a2f96a5c0ce221940ef71` |
| `FR/MMPFDLG2.RAW` | 262144 | `040825d79e5ad2d7ee0982259ed72a341b841131` |
| `FR/MMPFDLG3.RAW` | 262144 | `ecb4c7465f6cc1fc8cbec9c7dea7b35db4c21efc` |
| `FR/MMPFDLG4.RAW` | 262144 | `06ce752221f098b10dacc2ff9ebd33287ca8b2df` |
| `FR/MMPFR.RAW` | 245760 | `26416c6cc736624dbfd139c51f44c820ef1bfb41` |
| `GER/GERBTN2.PCX` | 12288 | `462b091059c04bc70e1a45a31064971ed71dc839` |
| `GER/GERBTN3.PCX` | 12288 | `2b4cbd9fb226796f116419a536eee93e1d9fd1ef` |
| `GER/GERPANEL.RAW` | 262144 | `a94b09b970280e0117375af6018e81bf00a533cf` |
| `GER/GMID.PCX` | 12288 | `de34beb1bdfd48781f318e7538c16952a7d7565a` |
| `GER/MMPGBUTT.PCX` | 24576 | `14d6dda54386cc6e1489803f879ee634805dc542` |
| `GER/MMPGDLG1.RAW` | 262144 | `30c3b45b99d0ba047d164fae52670a19a8bd2b30` |
| `GER/MMPGDLG2.RAW` | 262144 | `05eebab0f27528c532d7b4e32cb50170d5755eb1` |
| `GER/MMPGDLG3.RAW` | 262144 | `7525ea166102cec5fd122d128ac566ebbd734eed` |
| `GER/MMPGDLG4.RAW` | 262144 | `fc5a8f639029608b851933f4a1401cd2e9586ce5` |
| `GER/MMPGER.RAW` | 245760 | `80eec613cde832684f032a55e73330606b475c8b` |
| `PHEW2.COD` | 40984 | `87e60adae92e750316c1b3fa3c54c5b2a80ebf52` |
| `SIOSTUFF.COD` | 16999 | `d1d0d8d0eb235cb86681c53307c3790fe1365055` |
| `SLES_500.09` | 300008 | `24e169d654e96875393d9eeb339a46424af44d30` |
| `SYSTEM.CNF` | 56 | `8b361fddc1d477c8d890a745d86bf68661e447f2` |
| `UK/BUTTMMP.PCX` | 24576 | `98e6ae354668b50d895ea4c2501eb33bc3b1beb4` |
| `UK/BUTTONS3.PCX` | 12243 | `18a68d0541411652441421db51d5593e9db584e2` |
| `UK/DLG1.RAW` | 262144 | `b21b6d73ec9c5d7699fb25f045543b82054ea5ae` |
| `UK/DLG2.RAW` | 262144 | `60b63e56e7b8f7a0a02d654e02b7a76a9f8fb030` |
| `UK/DLG3.RAW` | 262144 | `ae62aae017a7bcd3bc92d65081e8c3703e31d4e6` |
| `UK/DLG4.RAW` | 262144 | `f025ecbdc413fa158b9432591f12fc9eb550d5dd` |
| `UK/MIDMMP.PCX` | 16384 | `5448062cba03f8d5ba5f9732ea1cc60650d89224` |
| `UK/MMPUK.RAW` | 245760 | `a41b663d483c751088df45faf9fa23268c0a4792` |
| `UK/PANELMMP.RAW` | 262144 | `640e048e413787b352d1d48f9a42000e8dd59a40` |
| `ZIPUSB.COD` | 23268 | `acd8cc2dbd85e04d01a7db95350045e9da646407` |

* `BIG.DAT` sits at LBA 25 (right after the file system) and is not referenced anywhere. It is **padding** that pushes
  the real data to the outer, faster part of the disc. An ISO rebuilt without it boots fine.
* `PHEW2.COD`: IOP module exporting `usbd` (USB host driver). `ZIPUSB.COD`: IOP module `zipusb`
  (Iomega Zip / USB mass storage, RPC server). The manager could back saves up to a Zip drive.
* `SIOSTUFF.COD`: IOP module, the memory card / pad driver of the manager (see §3).

## 2. The main executable (`SLES_500.09`)

| Segment | Address | Size | Content |
|---|---|---:|---|
| 1 | `0x000C0000` | `0x2B80` | **resident hook** (code `0xC0000–0xC1218`, data `0xC1280–`) — see §5 |
| 2 | `0x00800000` | `0x29FE4` | manager (UI, LZARI, card logic) |
| 3 | `0x0082A000` | `0x1B114` (+bss) | data |

Startup (`0x815738`): `SifInitRpc`, loads `cdrom0:\PHEW2.COD`, `cdrom0:\ZIPUSB.COD`, `rom0:SIO2MAN`,
`cdrom0:\SIOSTUFF.COD` (no IOP reset; SECRMAN is resident from the boot configuration), then binds RPC server
`0x47AE` of SIOSTUFF.

* LZARI (Okumura 1989) is at `0x800488` onwards: `N_CHAR = 314`, `N − F = 4036`,
  `position_cum` initialised with `10000 / (i + 200)`.
* Card size detection (`0x809BB8`): if the probe returns specs, use them; otherwise read page 0, compare it with pages
  16384 and 32768 (address wrap-around) to guess 8 or 16 MB. If page 0 is not a Sony superblock, the program **writes
  "Guessing MC Size"** into page 0 before comparing, which is destructive on an unformatted card.
* Exit paths: `LoadExecPS2("rom0:OSDSYS", "BootBrowser")` (to browser) and `LoadExecPS2("rom0:PS2LOGO", <game ELF>)`
  (start a game disc). Both first call the resident installer `0xC06E8`.

## 3. SIOSTUFF.COD: the card driver used by the manager

Imports: `secrman` 1.3 (#4 `SecrSetMcCommandHandler`, #5 `SecrSetMcDevIDHandler`, #6 `SecrAuthCard`),
`sio2man` 1.1 (#23 pad init, #24 mc init, #25 transfer), `sifcmd`, `sysclib`, `thbase`, `thsemap`.
A thread (prio 99, 32 KB stack) registers **RPC server `0x47AE`**:

| fn | Action (even = slot 1 / SIO2 port 2, odd = slot 2 / port 3) |
|---:|---|
| 0 | read the whole MECHACON NVM (512 words, S-command `0x0A`) — used for the console language |
| 10/11 | pad read (`01 42`) |
| 20/21 | read page (512 + 16 ECC) |
| 22/23 | probe + authenticate + specs |
| 24/25 | erase block |
| 26/27 | write page |
| 28/29 | specs |
| 30–37 | raw transfers (unused by the EE program) |
| 50 | `81 43 11 11 11 11` (unused) |
| 100 | last result |

SIO2 packets (ps2sdk `sio2_transfer_data_t` layout): `port_ctrl1 = 0x00040505`, `port_ctrl2 = 0x00020F00`,
`regdata[0] = port | X` with X = `0x00100440` (4-byte transfer), `0x00140540` (5), `0x00240940` (9),
`0x00340D40` (13), `0x00581640` (22), `0x02188640` (134).

* **Probe / auth** (`0xD84 → 0xB28 → 0x858`): `81 11`; on error: `81 F3 00`, `81 F7 01`,
  `SecrAuthCard(port, 0, (port & 1) << 3)` with command handler = plain `sio2_mc_transfer_init + sio2_transfer`,
  then `81 28`, `81 27 5A` (terminator), `81 26 5A` (specs: page size `rx[3..4]`, block size `rx[5..6]`,
  card size in pages `rx[7..10]`). Returns 1 = already active, 2 = MagicGate OK, 3 = specs OK but MagicGate failed.
* **Read page**: `81 23 lo hi 00 00 xor` (9 bytes), 4 × `81 43 80` (134 bytes, data `rx[4..131]`,
  XOR checksum `rx[132]`), `81 43 10` (ECC, 22 bytes), `81 81`.
* **Write page**: `81 22 addr`, 4 × `81 42 80 <128 bytes>`, `81 42 10 <ECC>`, `81 81`.
* **Erase block**: `81 21 addr`, `81 82`, then `81 12` until the card answers `0x5A`.

## 4. What the card does (and doesn't)

Traced with `MMDIAG` ([full log](logs/MMDIAG_LOG.TXT)). Sony's SECRMAN, when `SecrAuthCard()` is called, sends:
`81 F3 00` (reset), `81 F7 01`, then `81 F0 00` … `81 F0 14` (21 MagicGate steps, several carrying 8-byte
encrypted payloads).

| | Sony card | Datel Mega Memory 8 |
|---|---|---|
| `81 11` from cold | `ff ff 2b 5a` | no answer (timeout) until it has seen `81 27 5A` |
| `81 F3 00` | `… 2b 5a` | `… 2b ff` (terminator `FF`) |
| `81 F0 00` | answers | **no answer at all** → `SecrAuthCard() = 0` |
| `81 28`, `81 27 5A`, `81 26 5A` | OK | OK: 512-byte pages, 16 pages/block, 16 384 pages (8 MB) |
| page read / write / erase | OK | OK, standard commands, standard Sony file system |

**The card has no MagicGate chip at all.** It is a plain flash memory card that speaks the standard command set.
Sony's MCMAN refuses any PS2 card that fails `SecrAuthCard`, so the browser and games ignore it.

## 5. The resident hook

Segment 1 (`0xC0000`) is self-contained (it even carries its own SIF RPC client). Installer `0xC06E8`:

1. `SetSyscall(254, read_word)`, `SetSyscall(255, write_word)`: kernel memory access from user mode.
2. Finds the syscall table by scanning kernel memory `0x80010000–0x8007FFFF` for its own two handlers, then reads
   entry 7 (`ExecPS2`).
3. Writes `lui k0,0x000C / addiu k0,k0,0x0A08 / jr k0 / nop` at **`ExecPS2 + 44`**, which is the kernel's
   `mfc0 k0,Status … eret` epilogue. The hook (`0xC0A08`) calls `0xC04A0`, then re-executes
   `mfc0 / ori k0,0x13 / mtc0 / sync / eret`.
4. `0xC0660`: finds `lui a0,0x0008 … ori a0,a0,0x2000` in the kernel (the start of the area `LoadExecPS2`
   clears) and changes it to `lui a0,0x0010`. `0x82000–0x102000` survives, and so does the hook.
5. Mode variable at `0xC1328`: 1 = normal; 2 = also hook syscall 121 in the kernel (for boot ELFs `SLES_505.39`
   and `SLUS_202.65`, games that don't use the usual stub); 3 = hook syscall 60.

At every `ExecPS2` (mode 1), `0xC04A0` scans `0x00100000–0x01FFFFFF` (uncached) for the `SifSetReg` stub
`24030079 0000000C 03E00008 00000000` (`li v1,0x79 / syscall / jr ra / nop`) and replaces its first two words with
`j 0xC0A2C / nop`. The wrapper performs the real syscall, then calls `0xC02F0(reg, value)`:

* `(0x80000000, 0)` (SUBADDR cleared: the IOP is being reset) arms a flag;
* then `(0x80000002, 1)` (RPCINIT: the IOP has rebooted and SIF RPC is up) triggers the patch. It binds RPC server
  `0x80000006` (LOADFILE), reads IOP RAM `0x40000–0x7FFFF` word by word (LOADFILE fn 3) looking for
  `"secr" "man_" "for_" "cex\0"` (built from immediates so the code doesn't find itself), and writes
  **`0x24020001` (`li v0,1`) at `name − 0x2BA0`** (LOADFILE fn 2).

That instruction sits inside `SecrAuthCard`: the function now always returns 1 ("authenticated"). MCMAN then
reads the specs and the file system of the Datel card like any other card. The patch is redone after **every** IOP
reboot, so it works in the browser and in games started afterwards, until power-off.

## 6. The X-MEM "compressed saves" format

* Folder `BESLES-00000 X-MEM` with `icon.sys` and `slime1.ico` (the X-MEM cube).
* Each compressed save is a file of that folder named after the save folder, **with mode `0x0417`: the "exists"
  bit `0x8000` is cleared.** For the PS2 the file is deleted and its clusters are free, but the FAT links are kept
  (`next & 0x7FFFFFFF`).
* Archive layout:

| Offset | Size | Content |
|---:|---:|---|
| 0x00 | 32 | save folder name |
| 0x20 | 32 | title (ASCII) |
| 0x40 | 4 | uncompressed size (rounded up to 4 KB) |
| 0x44 | 4 | number of files |
| 0x48 | 4 | uncompressed size (again) |
| 0x4C | … | LZARI stream (Okumura; ring buffer pre-filled with `0x20`) |

* Uncompressed payload: for each file `u32 size`, `char name[32]`, data. Headers are 4-byte aligned and followed by
  0–12 bytes of junk; one name was found with leading NUL bytes. The tail of the buffer is uninitialised memory.

Because the clusters are free, **later saves overwrite archives**. On our card, 9 of 23 archives had lost their first
cluster (unrecoverable: arithmetic coding cannot resynchronise). `pc-tools/datel_xmem_extract.py` recovers the rest.

## 7. Our clean-room reimplementation

* **`megamem_secrpatch.irx`** (`src/secrpatch`): runs on the IOP. It finds `secrman_for_cex` in
  `0x40000–0x7FFFF`, finds the `secrman` export table (magic `0x41C00000`, entry 6 = `SecrAuthCard`),
  writes `li v0,1` at `name − 0x2BA0` and calls `FlushIcache()`. It returns status bits
  (see [IRX.md](IRX.md)).
* **Resident driver** (`src/resident`, ~3.7 KB at `0x000C0000`):
  * installer (`res_install.c`, user mode + `ee_kmode_enter`): `GetSyscallHandler(7)` for `ExecPS2`; looks
    for `mfc0 k0,Status` followed by `eret` within 7 words; **copies those original words** into the trampoline;
    patches the kernel clear range; replaces the epilogue with `j res_tramp / nop`; `SetSyscall(252, res_iop_patch)`.
    If the driver is already installed, it keeps the original words and only refreshes the rest. If anything
    doesn't match, it installs nothing.
  * `res_tramp` (kernel, from `ExecPS2`): saves all GPRs (128-bit), switches to its own stack, redirects every
    `SifSetReg` stub of the new program to `res_setreg`, restores and runs the displaced epilogue.
  * `res_setreg` (user mode): real syscall `0x79`, then on `(0x80000000,0)` → `(0x80000002,1)` calls syscall 252.
  * `res_k_setreg` (kernel, v2): installed in the syscall table in place of `SifSetReg` (0x79); calls the original
    handler, then patches on `(0x80000002,1)`. Needed for packed ELFs, whose stubs don't exist yet when `ExecPS2`
    returns (first hardware test: wLaunchELF launched from MMBOOT did not see the Datel card with the stub method alone).
  * `res_iop_patch` (kernel, syscall 252): patches IOP RAM **directly** through the EE mapping `0xBC000000`
    (no LOADFILE RPC needed), and only if the target lies inside `SecrAuthCard` (export table check). This protects
    games whose IOPRP ships a different SECRMAN.
* **MMBOOT** (`src/mmboot`) reads `MMBOOT.CNF`, installs the resident driver and chain-loads the configured
  launcher with `LoadELFFromFile()` (or opens the browser). See [MMBOOT.md](MMBOOT.md).
* **XMEM2 → Open the Datel archive** (`src/xmem2/src/datel.c`): the same recovery as the PC tool, on the console.
  The archives are *deleted* files, so MCMAN can't open them: XMEM2 reads raw pages through its own read-only
  module `XMRAW.IRX` (`src/xmraw`, same SIO2 sequence as MMDUMP; `mcReadPage` / rom0:MCSERV command `0x7E` hung on
  a real console), mounts the superblock and FAT itself, follows the chains of the deleted entries, and decodes LZARI
  (C port of Okumura's decoder, ring buffer filled with spaces). Checked against the real card dump: the 14
  recoverable saves come out byte-identical to the Python tool.
* **XMEM2** loads `megamem_secrpatch.irx` right after `rom0:SIO2MAN` and before `rom0:MCMAN`, so its own
  session sees the Datel card; *Exit to browser* installs the resident driver.

## 8. Known limits

* **An IRX alone can't make the card work "permanently"**: IOP RAM is wiped at every IOP reset, i.e. every time a
  program or game starts. Only something resident on the EE side (Datel's hook, our MMBOOT) survives. Details in
  [MMBOOT.md](MMBOOT.md#why-an-elf-why-isnt-the-irx-enough).
* The `− 0x2BA0` offset targets the ROM SECRMAN of 2000–2002 consoles (Datel's own assumption). On other BIOS
  versions the export-table check will most likely refuse to patch; please report your console model.
* Programs that reset the IOP without the standard `SifSetReg` stub (Datel special-cased two boot ELFs) are not
  hooked by the resident driver.
* Games started by OPL / Neutrino (which reset the IOP with their own code) were not tested.
