# Disc & executable notes — Twisted Metal 2

Facts established by probing the dumps in `Game (ps1 disc)/`. Everything here
was read off the discs directly; nothing is guessed.

## Retail — `Twisted Metal 2 (USA)` (recomp target)

| | |
|---|---|
| Serial | `SCUS-94306` |
| Boot | `cdrom:\SCUS_943.06;1` (`SYSTEM.CNF`: `TCB = 4`, `EVENT = 16`, `STACK = 801FFFF0`) |
| Disc | 12 tracks — Track 01 `MODE2/2352` data (418,187,952 B), tracks 02–12 CD-DA |
| ISO created | 1996-09-11 17:44:06 |
| Data track sectors | 177,801 |
| Files | 216 entries |
| Boot EXE | LBA 24, 737,280 B on disc |

PS-X EXE header:

```
pc0          0x8013878C
text load    0x800CDC54
text size    0x000B3800   (735,232 B = 183,808 instructions)
text end     0x80181454
sp           0x801FFFF0
gp0          0x00000000
region       "Sony Computer Entertainment Inc. for North America area"
```

### Memory layout consequence

The EXE sits high in RAM. That leaves two large free windows:

```
0x80010000 .. 0x800CDC54   ~760 KB below the text  (level/model/texture staging)
0x80181454 .. 0x801FFFF0   ~518 KB above the text  (heap + stack)
```

### No code overlays

Every disc path referenced by the executable is **data**, not code:

- `TERRAIN\*.RMP` / `.GRP` / `.PTS` — collision/terrain maps
- `TMS\*.TMS`, `DMD\*.DMD` — level and car geometry/models
- `SCREEN\`, `SHELLDB1\`, `SHELLDB2\`, `BIOS\` — `.TIM` images, `.VAB` audio bank
- `MOVIES\*.STR` — MDEC streams
- `*.DA` — XA audio streams

There is no second `SCUS_*`/`.OVL`/`.BIN` executable on the disc and no
`LoadExec`-style path in the string table. This is the best case for a static
recompiler: **the whole game is one ahead-of-time recompilable executable.**

### Linked Sony libraries

The RCS ids are still in the binary, so the PSY-Q library revision is known:

```
$Id: bios.c,v 1.71 1995/12/01 08:36:19 makoto Exp $
$Id: sys.c,v 1.116 1995/12/01 07:03:58 suzu Exp $
$Id: intr.c,v 1.73 1995/11/10 05:29:40 suzu Exp $
```

Instruction mix over the 183,808 text instructions: 722 COP2 (GTE) ops,
472 `syscall`, 913 `break`.

## Prototype — Aug 26 1996 (reference only, not a build target)

| | |
|---|---|
| Serial | `SCUS-94306` (same) |
| ISO created | 1996-08-08 15:18:21 |
| Boot EXE | LBA 170,872, 712,704 B |
| text | `0x800CFA68`, size `0x000AD800`, entry `0x80133BC0` |
| Files | 219 entries |

Audio tracks 02–11 are byte-identical in size to retail; track 12 differs.

### Layout differences

| Retail | Prototype |
|---|---|
| `TMS/`, `DMD/` (flat) | `CARSDB/`, `LEVELDB/`, `MLEVELDB/` (split by role) |
| `SCREEN/*PLATE.TIM`, `SCREEN/SHOWOPS*.TIM` | `PLATES/` |
| `SHELLDB1/`, `SHELLDB2/` | `SHELLDB/` |
| `SCREEN/UACORE.VAB` | `SND/UACORE.VAB` |
| — | `LEVELDB/ROOFEZ.*` ("EZ" easy variant; format string `%s%sEZ.%s`) |

### Code similarity

Byte-for-byte the two executables share little (all absolute addresses shift by
the `0x1E14` load-address delta), but at the **opcode level 75.9%** of retail
32-instruction windows appear verbatim in the prototype. It is the same
codebase, so the prototype is a usable cross-reference for naming functions.

### Strings present only in the prototype

Cheats / modes cut before release:

```
Kali Mode      Kali Mode Off
Thor Mode      Thor Mode Off
```

Text cut or reworded:

```
Amizonia : Fire Walk        -> Amazonia : Fire Walk   (typo fixed)
Denmark : Field of Screams  -> Holland : Field of Screams
Hammer Head                 -> Hammerhead
SAY HELLO TO MINION
HOW ABOUT A / LITTLE ICE CREAM?
older / newer
```

These are the starting list for the cut-content mod work.

## Controller: digital pad only

Twisted Metal 2 shipped in September 1996, a year before the DualShock. Its
libpad predates analog controllers and **silently discards input from a pad
that answers the read command with id `0x73`** — the game looks completely
dead to the controller, while launcher hotkeys keep working because they never
go through SIO.

Confirmed on the SIO trace. With an analog pad:

```
tx 0x01 -> rx 0xFF     address controller
tx 0x42 -> rx 0x73     read command; 0x73 = DualShock in analog mode
tx 0x00 -> rx 0x5A     ready
                       ... game ignores the payload
```

With a digital pad:

```
tx 0x42 -> rx 0x41     0x41 = digital pad — game accepts input
```

`game.toml` therefore sets:

```toml
[controller]
default_mode = "digital"
lock_mode    = true
```

`lock_mode` is the load-bearing half. `default_mode` alone is only a default,
and a launcher-written `settings.toml` carrying `p1_mode = "analog"` overrides
it — which is exactly how this first showed up. `lock_mode` clamps every seat
after all config sources are applied, hides the launcher's pad-mode selector,
and forces `multitap_analog` off. Verified: with `settings.toml` deliberately
set back to `p1_mode = "analog"`, the wire still shows `0x41`.

### Open question

`settings.toml` also carries `multitap = true`. Twisted Metal 2 has no
multitap support, and two-player over two ports has not been tested yet.
Worth re-checking when split-screen is exercised.
