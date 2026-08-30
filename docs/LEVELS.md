# Level table

Recovered from the two lookup functions, then **corrected against what the
game actually loads** — two slots do not match the names in the code.

`func_8012ABD0` (index → asset prefix) and `func_8012ACB8` (index → display
name) are jump-table switches. Both do `index - 1` first, so the table is
**one-based**, then bound at 12. The tables live at `0x800CFB58` and
`0x800CFB88`.

| ID | Asset prefix | Name in the code | What it is |
|---|---|---|---|
| `01` | `FREEWAY` | Los Angeles : Quake Zone Rumble | |
| `02` | `DISH` | Moscow : Suicide Slide | |
| `03` | `PARIS` | Paris : Monumental Disaster | |
| `04` | `ISLANDS` | Amazonia : Fire Walk | |
| `05` | `ROOF` | New York : The Big Leap | |
| `06` | `GLACIER` | Antarctica : The Drop Zone | |
| `07` | `DEN` | Holland : Field of Screams | |
| `08` | `HKONG` | Hong Kong : Hong Kong Krunch | |
| `09` | *(none)* | It's a Secret | **glitches** — see below |
| `0A` | `SWAMP` | Florida : Suicide Swamp | **a Jet Moto course** — see below |
| `0B` | `BURB` | Los Angeles : Cyburbia | |
| `0C` | `SROOF` | Los Angeles : Roof Tops | |

The level index lives at `0x80164774` in the game-setup block.

## `09` — a name with nothing behind it

Slot 9 has a display name ("It's a Secret") but its entry in the **asset-code**
jump table points at that function's *default* handler, which returns
`"UNKNOWN LEVEL"`. So there is no prefix to build a filename from and nothing
to load. Selecting it glitches, which matches what the cheat lists say.

## `0A` — named for a level that was never built

This one is worth spelling out, because the code and the content disagree.

The code calls slot 10 **"Florida : Suicide Swamp"** and its asset prefix is
`SWAMP`, so it loads `TMS\SWAMP.TMS` and `DMD\SWAMP.DMD` — and a full set of
`TERRAIN\SWAMP.{GRP,PTS,RMP}` exists on the disc too. Everything about the
naming says "swamp level".

What actually loads is a **Jet Moto course** — SingleTrac's other game.
Forcing the level to `0x0A` and looking at it shows a linear mud track with
whoops, banked wooden jump ramps, marker buoys along both sides and a pine
forest boundary. It is a race track, not a Twisted Metal arena.

So the Florida/Suicide Swamp level was never made, and the slot carries a Jet
Moto track as an easter egg instead. The name string and the `SWAMP` prefix
are what survived of the intended level.

Corroborating detail: the executable contains **no** occurrence of `JET` or
`MOTO` in any casing, and there is no Jet-Moto-named asset on the disc. The
course ships entirely under the `SWAMP` name.

## Verifying a level yourself

The debug menu can force a level without touching the menus:

```bash
# via the control server, with the debug mod enabled
SETVAL <always-play-level index> 9    # choice 9 + base 1 = level 0x0A
SET    <always-play-level index> 1
```

Then let the attract demo start and take a `present_shot`. That is how the
`0A` result above was established — `0x80164774` read back `0x000A` and the
frame showed the race course.
