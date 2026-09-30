# megamem_secrpatch.irx

A tiny IOP module (≈1.2 KB) that makes Sony's `SecrAuthCard()` succeed, so that MCMAN accepts memory cards
without MagicGate, such as the Datel / Big Ben **Mega Memory 8**. It is the same patch Datel's CD applied, done from
the IOP side.

## What it does

1. Looks for the module name `secrman_for_cex` in IOP RAM `0x40000–0x7FFFF`.
2. Looks for the export table of library `secrman` (magic `0x41C00000`) and reads entry #6 (`SecrAuthCard`).
3. Writes `li v0,1` (`0x24020001`) at `name − 0x2BA0` and flushes the IOP instruction cache.
4. Returns `MODULE_NO_RESIDENT_END` (it doesn't stay in memory) with status bits:

| Bit | Meaning |
|---:|---|
| 8 (`0x100`) | `secrman_for_cex` found |
| 9 (`0x200`) | `secrman` export table found |
| 10 (`0x400`) | **sane**: the target lies inside `SecrAuthCard` |
| 11 (`0x800`) | patch written |
| 12 (`0x1000`) | already patched |

Like Datel's disc, it patches even when it cannot confirm bit 10. If you integrate it in something that runs with a
game's own IOP modules, check bit 10 first.

## Using it in your homebrew

Load it **after `SIO2MAN` and before the first card detection**, ideally before `MCMAN`:

```c
int ret;
SifLoadModule("rom0:SIO2MAN", 0, NULL);
SifExecModuleBuffer(megamem_secrpatch_irx, size_megamem_secrpatch_irx, 0, NULL, &ret);
/* ret & 0xC00 == 0xC00 -> verified and patched */
SifLoadModule("rom0:MCMAN", 0, NULL);
SifLoadModule("rom0:MCSERV", 0, NULL);
```

Loading it later also works: MCMAN retries the authentication the next time it detects the card.

## ⚠️ Important limitation: the IRX does not survive a program change

The patch lives in **IOP RAM**, and IOP RAM is wiped at every **IOP reset**. Every ELF, every game, every loader
resets the IOP when it starts and then loads its own modules. So:

```
PS2BBL + LOAD_IRX_E1 = megamem_secrpatch.irx   ->  Datel card OK inside PS2BBL
PS2BBL launches OPL / a game / any ELF         ->  IOP reset: patch gone, Datel card invisible again
```

POPSTARTER-style IRX files "work every time" only because POPSTARTER reloads them itself at each launch. A game or
OPL has no reason to load ours.

What the IRX is good for:

* homebrew that loads its own modules and wants to read a Datel card (XMEM2 and MMBOOT embed it);
* `LOAD_IRX_E1` in PS2BBL, so that PS2BBL itself can read its config or ELFs from a Datel card.

To keep the card working **in the browser, in launchers and in games**, use **`MMBOOT.ELF`** as your auto-boot entry
(it plays the role of the CD, then starts your launcher), or XMEM2 → *Exit to the PS2 browser*. Both install an
EE-side resident driver that re-applies the patch after every IOP reboot until power-off.
Full explanation: [MMBOOT.md](MMBOOT.md#why-an-elf-why-isnt-the-irx-enough).
