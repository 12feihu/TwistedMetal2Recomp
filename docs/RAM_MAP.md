# RAM map

What we know about Twisted Metal 2's memory, and how we know it. Everything
here is derived from the GameShark list in `../Cheats.txt` cross-checked
against the real executable — see `tools/gs_import.py`, which regenerates
`mods/tm2-debug/cheats.toml`.

**Nothing in this file has been confirmed at runtime yet.** It is inference
from cheat addresses plus static validation against the EXE. Treat every
label as a hypothesis until a poke through the debug server proves it.

## Regions

The boot EXE loads high, which splits RAM into three very different zones:

| Range | Contents | Notes |
|---|---|---|
| `0x80010000`–`0x800CDC54` | streamed level / shell data | ~760 KB, contents change per level; addresses here are only meaningful once the right level is loaded |
| `0x800CDC54`–`0x80181454` | the EXE image: code **and** initialised data | patchable at any time; `orig_word` is recorded so patches can fail closed |
| `0x80181454`–`0x801FFFF0` | BSS / heap: live gameplay state | where the player and enemy structures live |

Of the 160 imported cheat operations: 92 target BSS, 49 the EXE image, 19 the
streamed low region.

## Player structures — stride `0x814`

Twenty-four independent P1/P2 cheat pairs agree on a **`0x814`-byte stride**
between player one and player two. That is a strong result: it means these are
two instances of one structure, and every P1 offset below implies the matching
P2 field at `+0x814`.

Taking P1 as the base, with the lowest known field at `0x80187BB8`:

| P1 address | Offset from `0x80187BB8` | Field (per the cheat that touches it) |
|---|---|---|
| `0x80187BB8` | `+0x000` | frozen |
| `0x80187BBA` | `+0x002` | shield active |
| `0x80187BC6` | `+0x00E` | exhaust / on-fire effect |
| `0x80187D00` | `+0x148` | **energy / health** (`0x78` = full, `0x00` = dead) |
| `0x80187D08` | `+0x150` | speed |
| `0x801880CC` | `+0x514` | low gravity |
| `0x801882D8` | `+0x720` | god mode |
| `0x801882DA` | `+0x722` | ammo block ("infinite everything") |
| `0x801882E4` | `+0x72C` | weapons-disabled mask |
| `0x801882E6` | `+0x72E` | specials count |
| `0x801882E8` | `+0x730` | fire missiles |
| `0x801882EA` | `+0x732` | homing missiles |
| `0x801882EC` | `+0x734` | remote bombs |
| `0x801882EE` | `+0x736` | power missiles |
| `0x801882F0` | `+0x738` | napalms |
| `0x801882F2` | `+0x73A` | ricochet bombs |
| `0x801882F4` | `+0x73C` | lightning |
| `0x801882FE` | `+0x746` | fire-rate / cooldown |
| `0x80188308` | `+0x750` | advanced-attack energy bar |
| `0x8018830A` | `+0x752` | turbo |
| `0x8018830C` | `+0x754` | invisibility |

The weapon counters form a contiguous `u16` array at `+0x72E`..`+0x73C` —
eight consecutive weapon slots, two bytes each. That is almost certainly a
single `u16 ammo[8]` and is the most useful single find in the list.

Energy full is `0x78` = 120. The "25% / 50% / 75%" cheats write `0x1E` / `0x3C`
/ `0x5A`, which are exactly 30 / 60 / 90 — confirming a 0–120 scale.

## Enemy array — stride `0x854`

The nine "Kill Enemy N" codes are perfectly uniform:

```
enemy 1  0x801890FC
enemy 2  0x80189950   +0x854
...
enemy 9  0x8018D39C   +0x854
```

**The enemy stride (`0x854`) is not the player stride (`0x814`).** Both are
measured across many samples, so this is not noise: players and AI cars are
either two separate arrays or two different structure types. Do not assume a
single unified car array until this is checked at runtime.

> **Resolved — and the base above is not the struct base.** Both arrays have
> since been recovered from the game's own index-to-pointer helpers and read
> at runtime (`docs/DARK_TOOTH.md`):
>
> | Array | Base | Stride | From |
> |---|---|---|---|
> | player vehicles | `0x80187B10` | `0x814` | `func_8011CEE8` |
> | AI vehicles | `0x80188B5C` | `0x854` | `func_8011D128` |
>
> The stride was right in both cases. The bases were not: the addresses in
> this file point at *fields*, not at objects. `0x80187BB8` is player
> vehicle `+0xA8`, and the "Kill Enemy N" addresses are
> `ai_vehicle[n] + 0x5A0` — health. Position is at `+0x10`/`+0x14`/`+0x18`
> and the behaviour mode at `+0x10C`.

## Discovered functions

Three cheats work by NOP-ing a `jal`, which names the callee outright. These
are the first real function symbols for this project:

| Address | Evidence |
|---|---|
| `0x800E3800` | `jal` at `0x8011E4F4`; NOP-ing it gives "Drive Through Walls" → **collision / wall response** |
| `0x8012B898` | `jal` at both `0x8012CB50` and `0x8012DDA4`; NOP-ing either skips a movie → **FMV playback**, called once per movie |

`0x8014F214` holds `blez $s4, 0x8014F698`, which "Drive Anywhere" rewrites to
an unconditional branch — so that is an out-of-bounds check guarding a
position update.

> **Corrected.** Disassembled: the surrounding code is COP2/GTE
> (`0x8014F1F0`–`0x8014F204` are GTE ops, and it uses `$gp`/`$sp`/`$fp` as
> scratch), so this is a transform-and-clip routine and the `blez` chain is a
> rejection test on transformed coordinates — not a position update. The
> cheat works by refusing to reject, which is not the same thing. Position
> updates live in `func_80102CC4` and friends (`docs/DARK_TOOTH.md`).

## Game-setup block at 0x80164760

Named by the "Modifier" cheats. Byte-sized fields:

| Address | Field |
|---|---|
| `0x80164760` | number of human players (0-2) |
| `0x80164764` | P1 car index |
| `0x80164765` | P2 car index |
| `0x80164766`-`0x8016476E` | computer cars 1-9 |
| `0x80164770` | P1 lives (a `u16` in the cheat) |
| `0x80164774` | level index |

Car indices resolve through `docs/CARS.md` (0 = Hammerhead ... 14 = Dark
Tooth). All zero in the EXE image; filled at runtime.

## Player 1 button word at 0x80180D34

The "Joker Command" and "Select & X to Kill Enemy N" cheats test a halfword at
`0x80180D34`, which is the game's own copy of the pad state. Decoding the
combinations those cheats use gives its bit layout, which is **not** the
hardware order and is **not** inverted:

| Bit | Button | | Bit | Button |
|---|---|---|---|---|
| 0 | L2 | | 5 | Circle |
| 1 | R2 | | 6 | Cross |
| 2 | L1 | | 7 | Square |
| 3 | R1 | | 8 | Select |
| 4 | Triangle | | | |

Derived from: Select alone `0x0100`, +L2 `0x0101`, +R2 `0x0102`, +L1 `0x0104`,
+R1 `0x0108`, +Triangle `0x0110`, +Circle `0x0120`, +Cross `0x0140`,
+Square `0x0180`.

## Errors found in the source cheat list

Recorded so they are not mistaken for real findings later:

- **"Never Frozen P1" is mislabelled.** It writes `0` to `0x80187BBA`, but
  `0x80187BBA` is the *shield* field — "Player 1's Shield is Always Active"
  writes `1` to the same address, and `0x801883CE` (`+0x814`) is the P2 shield.
  The frozen field is `0x80187BB8`. So that cheat disables the shield; it does
  not prevent freezing.
- **"Never Frozen P2"** points at `0x801883CC`, the same address as "Always
  Frozen P2", which is at least self-consistent (write `0` versus `1`).
- **"Fast Jumping Mode"** is malformed (`80000001 801880B8` is not a valid
  code pair) and is dropped by the importer.
- Duplicate names: "Axel Has No Wheels" (two variants, the second has one
  extra line), "Turbo Mode" (writes `0000` and `FFFF` — an off/on pair sharing
  one name), "No Turbo P1" (the second is actually the P2 address). The
  importer disambiguates these with a `-2` suffix.
- One entry has an empty name; it duplicates "No Radar/Map/Names".

## Next

The cheat list is a starting map, not ground truth. The way to firm it up is
to get into a match and poke these addresses through the debug server
(`read_ram` / `write_ram`), confirming each field does what its cheat name
claims — and, where it does, promoting it into `symbols.toml`.
