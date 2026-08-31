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
