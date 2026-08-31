# Dark Tooth: the head

Working notes on the two questions that matter for making Dark Tooth a proper
player vehicle:

1. **What places the head?**
2. **What decides whether the head drives itself?**

Both now have answers in code. What is still missing is a runtime observation
of the head actually attached — see [Open](#open) at the end.

Everything below was found with `tools/xref.py` (static) and the TCP debug
server (runtime), on retail `SCUS-94306`.

---

## Short version

There is a per-vehicle **behaviour mode** at `vehicle + 0x10C`. It selects one
of eight handlers through a jump table. **Mode 7 is "attached"**: a mode-7
vehicle does not compute its own transform — it copies one from whatever
`func_8011D128()` returns, which is a *single globally-indexed slot* in the AI
vehicle array:

```
func_8011D128()  ->  0x80188B5C + (*(int *)0x80180FAC) * 0x854
```

So the head is positioned **relative to one specific AI vehicle**, named by a
global index at `0x80180FAC`. Nothing in that path can name the *player*.

That is the shape of the bug. When Dark Tooth is an AI opponent, the boss is
in the AI array and the head has something to attach to. When the player
drives Dark Tooth, the thing the head wants to follow is not in that array at
all, and `0x80180FAC` still indexes the AI array — so the head reads a
transform belonging to some other car, or to an empty slot, which is why it
ends up parked at a fixed point.

---

## The behaviour dispatcher

`func_80102BE8(vehicle *a0, ? a1)`:

```
80102C04  lw    $v0, 268($s0)      ; vehicle + 0x10C  = behaviour mode
80102C0C  addiu $v1, $v0, -1
80102C10  sltiu $v0, $v1, 8        ; modes 1..8
80102C14  beq   $v0, $zero, 0x80102C7C
80102C1C  lui   $at, 0x800D
80102C24  lw    $v0, -3876($at)    ; jump table at 0x800CF0DC
80102C2C  jr    $v0
```

Table at **`0x800CF0DC`**, eight entries, index `mode - 1`:

| Mode | Target | Path |
|---|---|---|
| 1, 2, 3, 4, 7 | `0x80102C34` | calls `func_8011D388`, then **`func_80102CC4`** |
| 5, 6 | `0x80102C5C` | calls `func_8011D388`, skips `func_80102CC4` |
| 8 | `0x80102C7C` | the common tail only |

**Mode 2 is the human player.** Read live from the player vehicle while
driving: `*(0x80187B10 + 0x10C) == 2`.

The mode is **re-derived every frame** — writing a new value into `+0x10C`
from the debug server is overwritten before the next read. It is maintained
state, not a latch you can flip.

## The attachment code

`func_80102CC4(vehicle *a0)` opens by rejecting everything that is not mode 7:

```
80102CE4  lw    $v1, 268($s1)      ; mode
80102CE8  ori   $v0, $zero, 7
80102CEC  bne   $v1, $v0, 0x80102D54    ; not mode 7 -> use own fields
```

The two branches differ in exactly one way — **where the transform comes
from**:

```
mode 7                              everything else
------                              ---------------
jal  func_8011D128                  (no call)
a0 = the attach parent              s1 = itself
sp+56 = a0[0x748] (short)           sp+56 = s1[0x708] (short)
sp+58 = a0[0x74C]                   ...
sp+60 = a0[0x750]
sp+64 = a0[0x73C]
sp+68 = a0[0x740]
sp+72 = a0[0x744]
s4    = a0 + 108
s3    = a0[0x8B]  (byte)
```

`+0x73C`/`+0x740`/`+0x744` and `+0x748`/`+0x74C`/`+0x750` are two 3-component
groups — a position and an orientation. A mode-7 vehicle takes both from the
parent; every other mode reads its own copies at `+0x708`.

So **`func_80102CC4`, `0x80102CE4`–`0x80102D48`, is the head placement code**.

## The attach parent

`func_8011D128()`:

```
8011D128  lw    $v1, 1496($gp)     ; gp = 0x801809D4  ->  *(0x80180FAC)
          v0 = v1 * 2132           ; 0x854
8011D14C  lui   $v1, 0x8019
8011D150  addiu $v1, $v1, -29860   ; 0x80188B5C
8011D154  jr    $ra
8011D158  addu  $v0, $v0, $v1
```

= `&ai_vehicle[ *(int *)0x80180FAC ]`.

Six call sites: `0x800E74CC`, `0x800E74E0`, `0x800E74F4`, `0x80102CF4`,
`0x80102F1C`, `0x80103560`.

**`0x80180FAC` is the single most interesting address in this file.** It is
the "who is the boss" index, it only ever selects out of the AI array, and
every consumer of the attach parent goes through it.

---

## Structures confirmed at runtime

### Player vehicles — base `0x80187B10`, stride `0x814`

From `func_8011CEE8(i)`, which is a plain index-to-pointer helper:

```
8011CEE8  v0 = i * 2068            ; 0x814
8011CEFC  lui   $v1, 0x8018
8011CF00  addiu $v1, $v1, 31504    ; 0x80187B10
8011CF08  addu  $v0, $v0, $v1
```

Two slots: P1 at `0x80187B10`, P2 at `0x80188324`.

| Offset | Field | How it was established |
|---|---|---|
| `+0x10`,`+0x14`,`+0x18` | position (the vertical axis is `+0x10`) | driving forward moved `+0x14`/`+0x18` by an identical delta across ~20 points in the struct |
| `+0x10C` | behaviour mode | the dispatcher above; reads 2 for the human player |
| `+0x708`… | own transform source | the non-mode-7 branch |

This also relocates the player fields in `docs/RAM_MAP.md`: the base used
there, `0x80187BB8`, is this struct **`+0xA8`**.

### AI vehicles — base `0x80188B5C`, stride `0x854`

From `func_8011D128` above. Live readout in a Los Angeles match:

```
i   addr       mode  hp    transform (+0x73C,+0x740,+0x744)
0   80188B5C   0     89    [2543, -4883, 131]
1   801893B0   0     120   [4182, -5569, 56]
2   80189C04   0     1     [8403, -694, 119]
...
```

**Health is at `+0x5A0`**, not at the struct start. The GameShark list's
"Kill Enemy N" addresses (`0x801890FC`, `0x80189950`, …) are
`ai_vehicle[n] + 0x5A0` — the stride in `docs/RAM_MAP.md` was right, the base
was the health field rather than the object.

---

## Reaching the fight

The session object is at **`0x8016475C`** and is reached only through a family
of one-instruction accessors (which is why a plain address search finds no
references to it):

| Offset | Address | Field | Get | Set |
|---|---|---|---|---|
| `+8 + slot` | `0x80164764` | player car | — | `0x801378A4` |
| `+10 + slot` | `0x80164766` | AI car | — | `0x80137854` |
| `+24` | `0x80164774` | level | `0x80137808` | `0x80137814` |
| `+29` | `0x80164779` | final-battle flag | `0x801377F0` | `0x801377FC` |

`func_8012CD08` is the opponent-roster builder, and it is where the boss is
placed:

```
s0 = 0x8016475C
if (get_final_battle_flag(s0))
    set_ai_car(s0, 0, 14);        ; Dark Tooth into AI slot 0, no shuffle
else
    ...random opponent loop...
```

So in the final battle **Dark Tooth is `ai_vehicle[0]`**, which is consistent
with `*(0x80180FAC)` being 0.

The flag at `+29` has exactly one writer, `0x8012F448`, in the password-entry
path — and it is only written when the level is 8. That is the same bit 9 the
password format carries (see `docs/PASSWORDS.md`).

### Driving the debug server there

Holding `level = 8` and `final = 1` across the menu sequence does make the
roster builder take the boss branch (`ai_vehicle[0] = 14`, verified), and the
loading screen comes up as `Hong Kong : Hong Kong Krunch`. **The load then
wedges**: the CD goes idle after a Pause (`0x09`) and the game spins in
`TestEvent` (B0:0x0B). Whether that is a consequence of the forced entry or a
gap in this build's CD emulation is not yet established.

What does work reliably, and reproduces the reported bug, is playing as Dark
Tooth in Los Angeles: hold `0x80164764 = 14` across car select and the level
load. The truck drives away and the head stays behind — confirmed visually.

---

## Open

- **No mode-7 vehicle has been observed live.** In a normal level nothing sets
  mode 7, which is consistent with the head existing only in the boss fight,
  but "mode 7 is the head" is still inference from the code rather than a
  runtime observation. Confirming it needs the Hong Kong load to work.
- **What sets `0x80180FAC`**, and whether anything can legitimately point it
  at a player vehicle. If it cannot, then attaching the head to a player is
  not a matter of flipping a flag — the placement code would need the parent
  lookup redirected.
- **What sets `+0x10C` to 7**, and under what condition. The mode is
  recomputed per frame, so the interesting code is whatever decides it.

### Leads for picking this back up

Both searches have been run; neither has been read yet. Start here.

**Writers of `0x80180FAC`** (readers omitted; `func_8011D128` is one):

```
801196D4  sw $zero        <- cleared
8011A114  sw $v1          <- set from a computed value
8011DB24  sw $v0   8011DB34  sw $zero
8011EE14  sw $v0   8011EE24  sw $zero
8011EF00  sw $v0   8011EF10  sw $zero
```

The last three are all the same shape — `lw`, then `sw` a new value, then `sw
$zero` a few instructions later. That reads like *set the attach parent, do
something, clear it*, i.e. the index is scoped to one operation rather than
being a persistent "who is the boss". `0x8011A114` is the one that looks like
a real assignment. Worth checking whether any of them can be made to name a
player vehicle, since the whole fix hinges on it.

**The only store of a vehicle behaviour mode** — of the four `sw ..., 268(rX)`
sites in the executable, three are stack frames (`268($sp)`) and one zeroes
the field. Exactly one writes a real value into a vehicle:

```
800E75D8  sw $s1, 268($s0)
```

That is **inside the same function as three of the six `func_8011D128` call
sites** (`0x800E74CC`, `0x800E74E0`, `0x800E74F4`). So one routine around
`0x800E74xx`–`0x800E75xx` both looks up the attach parent and assigns a
vehicle's mode. If mode 7 is written anywhere, it is almost certainly there —
and that function is the head's spawn/attach setup.

**Next command:** `python tools/xref.py func 0x800E7400` (walk back for the
real entry first), then find its callers.
