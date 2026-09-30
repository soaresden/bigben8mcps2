# XMEM2 archive format ("XMM2")

XMEM2 stores compressed saves as **ordinary, allocated files** in the save folder `XMEM2-ARCHIVES`
(with its own `icon.sys` and `xmem2.ico`, so the Sony browser shows it as a normal save). One file per save, named
after the save folder (max 31 characters). Datel's X-MEM stored archives as *deleted* files that games could overwrite;
this format fixes that.

## Header (192 bytes, little-endian)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `"XMM2"` (`0x324D4D58`) |
| 4 | 4 | version = 1 |
| 8 | 4 | raw (uncompressed payload) size |
| 12 | 4 | compressed size |
| 16 | 4 | CRC-32 of the raw payload (zlib `crc32`) |
| 20 | 2 | folder attributes (`mode`, e.g. `0x8427`) |
| 22 | 2 | number of files |
| 24 | 8 | folder creation date (`sceMcStDateTime`) |
| 32 | 8 | folder modification date |
| 40 | 32 | folder name |
| 72 | 68 | title, Shift-JIS, copied from `icon.sys` (to list archives without decompressing) |
| 140 | 2 | title line-break offset (from `icon.sys`) |
| 142 | 50 | reserved (0) |

## Payload

zlib stream (`compress2`, level 9) of, for each file:

| Size | Field |
|---:|---|
| 32 | file name |
| 2 | attributes (`mode`, e.g. `0x8497`) |
| 2 | padding |
| 8 | creation date |
| 8 | modification date |
| 4 | size |
| n | data, padded to a multiple of 4 |

On restore, XMEM2 checks the CRC and the file count, recreates the folder and files, and restores attributes
and dates with `mcSetFileInfo`.
