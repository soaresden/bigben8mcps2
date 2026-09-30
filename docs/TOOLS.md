# Research tools

## MMDUMP.ELF: full card dump

Read-only. It activates the card(s) with Datel's sequence, reads every page (512 bytes + 16 ECC) with Datel's read
commands, and writes `mass:/BAKMEGAMEMORYMC/MEGAMEM_SLOTn_XX.mcd` (the 528-byte-page format used by PCSX2 / OPL VMCs
and mymcplus) and `MMDUMP_LOG.TXT`. If a card was just activated (the Datel one), only that card is dumped; otherwise
every card present is dumped. The card size comes from the `0x26` specs, cross-checked by address wrap-around.
Example log: [logs/MMDUMP_LOG.TXT](logs/MMDUMP_LOG.TXT).

## MMDIAG.ELF: authentication trace

Runs the authentication three times (slot 1 with Sony's sequence, slot 2 with Sony's sequence, slot 2 with
Datel's) and logs every SIO2 packet exchanged by SECRMAN (TX/RX bytes and SIO2 error bits) to
`mass:/BAKMEGAMEMORYMC/MMDIAG_LOG.TXT`. Example: [logs/MMDIAG_LOG.TXT](logs/MMDIAG_LOG.TXT).

The on-screen texts of these two tools are in French.

## pc-tools/

* `datel_xmem_extract.py card.mcd outdir/`: recovers Datel X-MEM archives (pure Python, no dependency).
