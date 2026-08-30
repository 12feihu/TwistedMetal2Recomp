# Vehicle roster

Recovered by disassembling the two lookup functions rather than by guessing at
filenames, so the index/code/name mapping is exact.

## The table

`func_8012ADA0` (index -> two-letter asset code) and `func_8012AEC4`
(index -> display name) are both `switch` statements over the car index, each
with `sltiu ..., 15` as the bound and a jump table in the data segment
(`0x800CFBB8` and `0x800CFBF8`). Resolving both tables gives:

| # | Code | Name | Model bytes (TMS+DMD) |
|---|---|---|---|
| 0 | `HH` | Hammerhead | 46,788 |
| 1 | `OT` | Outlaw 2 | 55,948 |
| 2 | `WH` | Warthog | 55,468 |
| 3 | `MG` | Mr. Grimm | 63,212 |
| 4 | `PV` | Grasshopper | 59,256 |
| 5 | `TH` | Thumper | 56,972 |
| 6 | `SP` | Spectre | 57,072 |
| 7 | `RK` | Roadkill | 62,768 |
| 8 | `IR` | Twister | 62,108 |
| 9 | `AX` | Axel | 58,204 |
| 10 | `FL` | Mr. Slam | 48,880 |
| 11 | `HS` | Shadow | 62,524 |
| 12 | `ST` | Sweet Tooth | 53,964 |
| 13 | `MN` | Minion | 55,168 |
| 14 | `BB` | **Dark Tooth** | **228,420** |

Out-of-range indices return the string `"UNKNOWN CAR"` from both functions.

The codes are not mnemonic and several look like leftovers from the first
game's asset naming — `PV` (Pit Viper) is Grasshopper here, `IR` is Twister,
`FL` is Mr. Slam, `HS` is Shadow, and `BB` is Dark Tooth. Do not infer a car
from its code; use the table.

Note indices 12 and 13: the compiler emitted those two case bodies in the
opposite order to their indices, so reading the code linearly suggests Sweet
Tooth and Minion are swapped. They are not — both jump tables agree that
12 = `ST` = Sweet Tooth and 13 = `MN` = Minion.

Assets are built as `TMS\<code>.TMS` and `DMD\<code>.DMD` through the format
string `"%s%s.%s"` at `0x80180C1C`.

## Dark Tooth

The final boss, and the only entry in the roster the game treats as not a
player car.

**It is index 14, code `BB`.** Both lookup functions return it normally, so
nothing in the naming layer excludes it.

### It is the only car with no UI assets

Every other car ships a character-select plate and an info screen. Dark Tooth
ships neither:

| Asset | Other 14 cars | Dark Tooth |
|---|---|---|
| `TMS\<code>.TMS` model | yes | yes |
| `DMD\<code>.DMD` | yes | yes |
| `BIOS\<code>INFO.TIM` | yes | **absent** |
| `SCREEN\<code>PLATE.TIM` | yes | **absent** |

Minion — also a secret character — has both, so this is specific to Dark
Tooth rather than a property of hidden characters. It is the clearest
evidence that Dark Tooth was never meant to appear on the select screen.

### Its model is a huge outlier

```
Dark Tooth   BB   TMS 166,760   DMD  61,660   total 228,420
Mr. Grimm    MG   TMS  30,904   DMD  32,308   total  63,212   <- next largest
Hammerhead   HH   TMS  21,848   DMD  24,940   total  46,788   <- smallest
```

`BB.TMS` alone is 5.4x to 7.6x every other car's, and the total is 3.6x the
next largest. That is consistent with it being several vehicles in one file
rather than one, which the game's own text confirms.

### The fight is two-phase, and the text says so

```
0x800CF93C  Dark Tooth, Sweet Tooth's father, rises from
0x800CF96C  the sewers. ``You killed my son!`` he shouts.
0x800CF99C  ``I want my little clown boy back!!!``

0x800CF9C4  But wait...what's this? Inside the giant head you
0x800CF9F8  see the shriveled figure of an old man! ``Heads
0x800CFA28  up you freak!`` the senile fool screams!
```

The second block is the head detaching to reveal a smaller vehicle — which
accounts for the oversized model file. The preceding block at `0x800CF88C`
("Last year's boss rises from the lava") is Minion, not Dark Tooth.

### Playing as it

The game-setup block, from the GameShark list:

```
0x80164760  number of human players (0-2)
0x80164764  P1 car index          0x80164765  P2 car index
0x80164766..0x8016476E  computer cars 1-9
0x80164770  P1 lives              0x80164774  level index
```

Writing **14** to `0x80164764` selects Dark Tooth for player one. The list's
"Both Players Cars" cheat (`0x8003434A = 0x000E`) does the same thing through
the car-select screen's own data — `0x0E` is 14, so that cheat is literally
"set both players to Dark Tooth".

**Untested.** Nothing here has been confirmed in a running match; it is all
static analysis. Whether a player-controlled Dark Tooth is stable — it has no
select-screen plate, and its model is structured for a boss — is exactly the
kind of thing to check with the debug menu once a match can be reached.

## How to re-derive this

`func_8012ADA0` and `func_8012AEC4` are the two lookups. Each is a jump-table
switch; resolving the table entries and decoding the `lui`/`addiu` pair at the
head of each case body yields the string address directly. The scripts used
are throwaway, but the method is in this file's history and the technique
generalises to every other `switch` in the executable.
