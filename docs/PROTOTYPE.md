# The Aug 26 1996 prototype

Reference material, not a build target. Same serial (`SCUS-94306`), but the
executable loads at `0x800CFA68` with entry `0x80133BC0`, and 75.9% of retail's
code matches it at the opcode level — close enough to cross-reference, far
enough that addresses do not transfer.

## Kali Mode and Thor Mode

The prototype has **four** cheat toggles where retail has three.

Both builds keep a table of HUD message pointers. In the prototype it is 33
entries at `0x8015EC38`; in retail, 38 entries at `0x80162868`. Diffing them:

| Removed before release | Added for release |
|---|---|
| `Kali Mode` / `Kali Mode Off` | `Mega Guns` / `Mega Guns Off` |
| `Thor Mode` / `Thor Mode Off` | `Infinite Weapons`, `Shoot 'em Lose 'em`, `Homing Napalm`, `Minion Special`, `Sell Your Soul`, `Demented Bonus`, `Train Ride Bonus` |

So Kali and Thor are the *only* two things removed. Both builds share
`God Mode`, `Eternal Life` and `Secret Combo`, which is what these are: cheats
entered as button combos during play.

### They were never finished

This is the interesting part, and it explains the removal.

The renderer reads a message id from `0x8017FFE4` and indexes the table:

```
lui   $v1, 0x8018
lw    $v1, -28($v1)        ; current message id
lui   $v0, 0x8016
addiu $v0, $v0, -5060      ; table base
sll   $v1, $v1, 2
addu  $v1, $v1, $v0
lw    $v1, 0($v1)          ; -> string pointer
```

The cheat handler at `0x800E1130`–`0x800E12B4` tests each combo and posts the
matching message. **Thor Mode** additionally sets a per-vehicle flag:

```
ori   $v0, $zero, 1
sb    $v0, 1924($s0)       ; vehicle struct + 0x784
ori   $a0, $zero, 24       ; message "Thor Mode"
jal   0x800FB258           ; post it
```

That flag is never used. Counting every load and store with each offset in the
per-vehicle flag block:

| Offset | Reads | Writes | |
|---|---|---|---|
| `+0x770` | 10 | 5 | |
| `+0x778` | 10 | 5 | |
| `+0x77C` | 3 | 4 | God Mode / Eternal Life |
| `+0x780` | 22 | 6 | |
| **`+0x784`** | **0** | **1** | **Thor Mode** |
| `+0x788` | 11 | 5 | |
| `+0x78A` | 11 | 4 | |
| `+0x790` | 12 | 5 | |

Every neighbouring flag is read 10–22 times. Thor's is written once and read
**never**. Compare the God Mode path, which actually branches on its flag:

```
lw    $v0, 1916($s0)       ; +0x77C
beq   $v0, $zero, ...      ; on or off?
ori   $a0, $zero, 23       ; "God Mode" (or 27, "God Mode Off")
```

**Kali Mode** does not even get that far — in the handler it only posts the
HUD message, with no flag write of its own.

So both modes are named, have on/off messages, and are wired into the combo
handler — but neither changes anything. Entering them in the prototype would
print a message and do nothing else. That is almost certainly why they did not
ship: not cut features, but stubs that were never filled in.

**Precision about the claim.** What is measured is that no instruction in this
executable loads offset `0x784` from any base. An indirect read through a
different base/offset pairing cannot be ruled out by this method, but there is
no sign of one, and the direct-read counts on every adjacent flag show the
comparison is meaningful.

## Other prototype-only content

From the string tables (see `docs/DISC_NOTES.md` for the full list):

- `ROOFEZ` — an "easy" layout variant, with the format string `%s%sEZ.%s`
- `Amizonia : Fire Walk` — the typo, fixed to "Amazonia" for release
- `Denmark : Field of Screams` — became Holland
- `Hammer Head` — became Hammerhead
- `SAY HELLO TO MINION`, `HOW ABOUT A / LITTLE ICE CREAM?` — taunts

## Layout differences

| Retail | Prototype |
|---|---|
| `TMS/`, `DMD/` (flat) | `CARSDB/`, `LEVELDB/`, `MLEVELDB/` split by role |
| `SCREEN/*PLATE.TIM`, `SHOWOPS*.TIM` | `PLATES/` |
| `SHELLDB1/`, `SHELLDB2/` | `SHELLDB/` |
| `SCREEN/UACORE.VAB` | `SND/UACORE.VAB` |

## Method

The tables were recovered the same way as the retail roster: find the string
pool, walk backwards from a known pointer to find the table extent, then scan
for `lui`/`addiu` pairs whose computed effective address lands in it. The
scripts were throwaway; the technique is in this file and generalises.

## Levels: prototype versus retail

Same technique as retail — the lookup functions are the cluster at
`0x801269E8`–`0x80126E30`, with jump tables at `0x800D185C` (asset code) and
`0x800D188C` (display name). Both one-based, both bounded at 12.

**The asset codes are byte-for-byte the same list**, including the gap:

```
01 FREEWAY   02 DISH    03 PARIS   04 ISLANDS   05 ROOF      06 GLACIER
07 DEN       08 HKONG   09 (none)  0A SWAMP     0B BURB      0C SROOF
```

So **slot 09 was already empty in August 1996** — its entry points at the same
default handler returning `UNKNOWN LEVEL`. It was never filled in.

The display names are where the two builds diverge:

| # | Prototype | Retail |
|---|---|---|
| `04` | **Amizonia** : Fire Walk | **Amazonia** : Fire Walk |
| `07` | **Denmark** : Field of Screams | **Holland** : Field of Screams |
| `09` | **UNKNOWN LEVEL** | **It's a Secret** |
| `0A` | **It's a Secret** | **Florida : Suicide Swamp** |
| `0B` | **It's a Secret** | Los Angeles : Cyburbia |
| `0C` | **It's a Secret** | Los Angeles : Roof Tops |

Two things worth drawing out.

First, `04` and `07` are plain fixes: a misspelling, and a country change
(Field of Screams is windmills and tulips, so Holland was presumably always
the intent).

Second, and more interesting: in the prototype the three hidden slots
`0A`/`0B`/`0C` were *all* just "It's a Secret", and slot `09` had no name at
all. By release, `0B` and `0C` got real names, `09` inherited the "It's a
Secret" label without ever getting content, and **`0A` was named
"Florida : Suicide Swamp"**.

That last one matters. We established elsewhere (`docs/LEVELS.md`) that slot
`0A` actually loads a Jet Moto course. Now we know the *name* arrived after
August 1996, applied to a slot that was an unnamed secret at the time and
never received the swamp level it was named for. The name is aspirational, not
a leftover.

### A per-level count changed

A third switch (`0x80126DE0`, table `0x800D193C`) returns a small integer per
level — almost certainly the opponent count, given the range:

| Level | Prototype | Retail |
|---|---|---|
| 01 Los Angeles | 7 | 7 |
| **02 Moscow** | **5** | **6** |
| 03 Paris | 7 | 7 |
| 04 Amazonia | 7 | 7 |
| 05 New York | 7 | 7 |
| **06 Antarctica** | **6** | **8** |
| **07 Holland** | **9** | **10** |
| 08 Hong Kong | 9 | 9 |

Three levels got more opponents for release; Antarctica gained the most.

### Level assets were reorganised

The prototype splits single-player and multiplayer level data into separate
directories; retail merges them:

```
proto  LEVELDB/   DEN DISH FREEWAY GLACIER HKONG ISLANDS PARIS ROOFEZ
proto  MLEVELDB/  BURB2 DEN2 DISH2 FREEWAY2 GLACIER2 HKONG2 ISLANDS2
                  PARIS2 ROOF2 SROOF2 SWAMP2
retail TMS/,DMD/  BURB DEN DISH FREEWAY GLACIER GLACIER2 HKONG ISLANDS
                  PARIS PARIS2 ROOF ROOF2 SROOF SWAMP
```

Notable: the prototype has **`ROOFEZ`** rather than `ROOF` in the
single-player set — the "easy" layout variant, matching the format string
`%s%sEZ.%s`. And `SWAMP` and `BURB` exist **only** as multiplayer variants
(`SWAMP2`, `BURB2`); there is no single-player swamp at all, which fits slot
`0A` being an unnamed secret at this point.

The `TERRAIN/` collision sets are identical between builds.

## Cars: almost nothing changed

The car lookups are at `0x80126B94` (asset code, table `0x800D18BC`) and
`0x80126CB8` (name, table `0x800D18FC`), both bounded at 15.

**The asset codes are identical to retail** — `HH OT WH MG PV TH SP RK IR AX
FL HS ST MN BB`, in the same order, with `BB` (Dark Tooth) at index 14. No
character was added, removed, or reordered.

Only one name differs:

| # | Prototype | Retail |
|---|---|---|
| `00` | **Hammer Head** | **Hammerhead** |

There is also a per-car integer table (`0x800D196C` in the prototype,
`0x800CFC68` in retail) mapping index to `16 × n`. It is **identical in both
builds**, including Sweet Tooth's odd `n = 1` where everyone else follows
roster order:

```
32 48 64 80 96 112 128 144 160 176 192 208 16 224 240
```

### Dark Tooth specifically

Unchanged at the table level — same index, same code, same name, same per-car
value. What did change is the model data:

| | Prototype | Retail | Delta |
|---|---|---|---|
| `BB.TMS` | 159,776 | 166,760 | +6,984 |
| `BB.DMD` | 58,668 | 61,660 | +2,992 |

It was already by far the largest vehicle in August 1996 and grew ~10 KB more
before release.

For comparison, every other car's `.TMS` is **byte-identical** between builds
except Minion, whose textures nearly halved (41,812 → 22,432). The `.DMD`
models see small revisions across most of the roster.
