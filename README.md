# Twisted Metal 2 Recompiled

A static recompilation of **Twisted Metal 2 (USA)** — the PlayStation 1
executable translated to C, compiled to native x64, and run against a
hardware-accurate runtime. Not an emulator: the game becomes a program your
CPU executes directly.

Built on [psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/mstan/recomp-ui).

> **Status: early (v0.1.0).** It boots, renders, plays audio and takes
> controller input, and the menus and in-game 3D work. It has not been played
> through and is not validated end to end. Expect problems.

---

## You need your own copy of the game

This repository contains **no game data whatsoever**. To build or run
anything you must supply a dump of a disc you own:

- **Twisted Metal 2 (USA)**, serial **`SCUS-94306`**
- A full **12-track** Redump-style `.cue` + `.bin` set — the data track alone
  is not enough, because the music is on CD audio tracks 2–12

Other regions and revisions will not work. The project is pinned to this one
executable, and the disc identity is checked against the digests in
`catalog_identity.json`.

Nothing here will download, or help you obtain, a copy of the game.

---

## AI assisted

**This project was built with heavy AI assistance.** The reverse engineering,
the tooling, the debug menu and its GUI, and essentially all of the
documentation in `docs/` were produced by [Claude](https://claude.ai)
(Anthropic) driven through Claude Code, working from the disc and the
executable.

That is stated up front because it should affect how you read the findings
here. Everything in `docs/` distinguishes what was **measured** from what was
**inferred**, and inferences are labelled as such — but the ratio of
machine-generated analysis to human review is high, so treat it as a strong
starting point rather than as settled fact. Where a claim mattered it was
checked against the running game; where it could not be, the documentation
says so.

The framework underneath (`psxrecomp`, `recomp-ui`) is a separate project and
not ours.

---

## How it works

A conventional emulator interprets the game's MIPS instructions at runtime.
A static recompiler translates them ahead of time:

1. **Translate.** The boot executable `SCUS_943.06` (735,232 bytes of MIPS,
   loaded at `0x800CDC54`) is decoded into ~4,000 functions and emitted as C —
   about **1.97 million lines** across 53 shards.
2. **Compile.** That C is compiled and linked as a normal native binary,
   alongside a recompiled PlayStation BIOS (the open-source OpenBIOS, bundled;
   no BIOS dump needed).
3. **Run.** A runtime supplies the hardware the code expects — GPU, SPU, CD-ROM,
   MDEC, GTE, DMA, interrupts, timers, controllers, memory cards. The game's
   own code runs natively against it.

Twisted Metal 2 suits this unusually well: **it loads no code overlays.**
Every file it reads from the disc is data — models, textures, terrain, FMV,
audio — so there is no code streamed in at runtime that a compiler cannot see
ahead of time. That makes 100% static coverage a realistic goal, which is not
true of most PS1 titles. See `docs/DISC_NOTES.md`.

### Why `generated/` is not in this repository

The recompiled C is derived directly from copyrighted game code, so it is not
distributed. You generate it locally from your own disc as a build step. That
is why building takes a while the first time and why the repo is small.

---

## Building

### Requirements

- CMake 3.20+, Ninja, and a C/C++ toolchain (MSVC, Clang, or GCC)
- Python 3 — on 3.10 or older also `pip install tomli`
- Git, for the submodules
- Roughly **300 MB** of disk for the generated C and build output
- SDL3 is fetched automatically during configure

### Steps

```bash
git clone --recurse-submodules <this repository>
cd TwistedMetal2Recomp

# 1. Generate the game C from your own disc (a few minutes)
python psxrecomp/psxrecomp_cli.py generate \
    --config game.toml --project-root . \
    --disc "/path/to/Twisted Metal 2 (USA).cue"

# 2. Build (this compiles ~2M lines of C; expect several minutes)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Where the executable lands

```
build/TwistedMetal2_Recompiled.exe
```

On macOS and Linux the same path without the `.exe`. Run it from anywhere; on
first launch the bundled launcher asks for your disc image, and the path is
remembered. To skip the launcher and point it straight at a disc:

```bash
build/TwistedMetal2_Recompiled.exe --no-launcher --disc "/path/to/game.cue"
```

No disc path is committed, so a fresh clone prompts you for the disc (and,
if you want one, a BIOS) on first launch. The launcher remembers the choice.

---

## Debug menu

An optional in-game debug menu exposing a large GameShark code list as
toggleable functions, plus an external Dear ImGui panel with a checkbox per
entry, category sections and value pickers for the "modifier" codes.

It is a **mod**, not a change to the game. The recompiled game code stays
vanilla; everything custom lives in `src/mods/` as a trusted plugin selected
at load time by a default-disabled `.psxmod` package. Press **F1** in game, or
build and run the GUI:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTM2_BUILD_DEBUG_GUI=ON
cmake --build build --target tm2-debug-gui
```

Full details, including the control protocol and the two things that are
specific to patching a *recompiled* game, are in `docs/DEBUG_MENU.md`.

---

## Mods

Everything optional ships as a `.psxmod` package under
`mods/preloaded/packages/`, **default disabled**, with every executable patch
guarded on the stock instruction so it fails closed on the wrong disc revision.

| Package | What it does |
|---|---|
| `tm2.debug` | The cheat engine, F1 menu and control server described above |
| `tm2.password-fix` | Makes the game accept the passwords it already prints for Sweet Tooth, Minion and Dark Tooth |

`tm2.password-fix` is three instructions. The game's password *encoder* writes
the driver number as a five-bit field; its *validator* reads four bits and
treats the fifth as a flag. For the twelve selectable drivers the value fits
and nobody notices. For the three that are not selectable it overflows, so the
game displays a perfectly well-formed password — right stage, right checksum,
computed from the real driver index — and then refuses it on entry. The patch
widens the validator to agree with the encoder. No password changes; the codes
it accepts are the ones the unmodified game was already showing you.

`docs/PASSWORDS.md` has the full format, including the fact that level 8 has
**two** passwords — Hong Kong and the Dark Tooth battle — separated by a
single bit.

---

## Documentation

| File | Contents |
|---|---|
| `docs/DISC_NOTES.md` | What is on the disc, the executable's layout, and why this title has no code overlays |
| `docs/CARS.md` | The 15-vehicle roster recovered from the game's own lookup tables, and what is unusual about Dark Tooth |
| `docs/LEVELS.md` | The 12 level slots — including one that loads a Jet Moto course under a name for a level that was never built |
| `docs/RAM_MAP.md` | Player and enemy structures, the game-setup block, the pad word, and errors found in the source cheat list |
| `docs/DEBUG_MENU.md` | The mod, the GUI, the control protocol, and open problems |
| `docs/PASSWORDS.md` | The password format, fully decoded — and why Dark Tooth's displayed password can never be entered |
| `docs/DARK_TOOTH.md` | The head: the per-vehicle behaviour mode, the attachment code, and the global that decides what the head follows |
| `docs/PROTOTYPE.md` | The Aug 1996 prototype: the two cheat modes cut before release, and why |
| `docs/ROADMAP.md` | Current status and what is next |
| `CLAUDE.md` | Working notes and build environment quirks |

---

## Repository layout

```
game.toml                 game identity, disc path, runtime settings
symbols.toml              progressive symbol map -> psx_symbols.h
seeds/ghidra_funcs.txt    function-entry seeds fed to the recompiler
src/mods/                 our code: cheat engine, debug menu, control server
tools/                    GameShark importer, debug GUI, password generator, static xref
mods/                     .psxmod packages and the cheat/value tables
docs/                     project documentation
psxrecomp/  recomp-ui/    framework submodules
```

Generated and derived artefacts — `generated/`, `disc/`, `build/`, saves — are
gitignored and never committed.

---

## Legal

This repository distributes **no copyrighted Sony or SingleTrac material**:
no disc image, no executable, no BIOS, no assets, and no recompiled game code.
All of that is produced locally from a disc you already own.

Two small exceptions, stated for completeness:

- `mods/tm2-debug/cheats.toml`, `src/mods/tm2_cheat_table.h` and the
  `tm2.password-fix` manifest together contain **18 32-bit words** (72 bytes)
  copied verbatim from the game executable. They are the original instructions
  at the addresses a code patch overwrites, recorded so a patch can verify
  what it is replacing and fail closed on the wrong disc revision.
- `seeds/ghidra_funcs.txt`, `symbols.toml` and `psx_symbols.h` contain
  **addresses**, not code.

Twisted Metal 2 is © Sony Computer Entertainment / SingleTrac. This project is
unaffiliated with, and unendorsed by, either. It is a personal preservation and
reverse-engineering exercise.

### Licensing

This repository is **PolyForm Noncommercial 1.0.0** (see `LICENSE`), matching
the framework it is built on. Read, fork, modify, redistribute, publish your
own findings — all fine. Selling it, or using it as part of something you
sell, is not.

| Component | Licence |
|---|---|
| This repository | PolyForm Noncommercial 1.0.0 |
| [`psxrecomp`](https://github.com/mstan/psxrecomp) — recompiler + runtime | PolyForm Noncommercial 1.0.0 |
| [`recomp-ui`](https://github.com/mstan/recomp-ui) — launcher | MIT |
| OpenBIOS (bundled by the framework) | MIT |
| Dear ImGui, SDL3 | MIT / zlib |

A permissive licence here would have been misleading, since every build links
against a noncommercial framework regardless of what this repository says.

To be precise about terms: PolyForm Noncommercial is **source-available**, not
open source in the OSI sense — the OSI definition forbids restricting the
field of use, and this restricts commercial use. The source is public, the
history is public, and anyone may build on it; it simply is not "open source"
as that phrase is formally defined.

None of the above applies to the game itself, which is not licensed to anyone
by this project.

---

## Credits

- [psxrecomp](https://github.com/mstan/psxrecomp) and
  [recomp-ui](https://github.com/mstan/recomp-ui) by
  [@mstan](https://github.com/mstan) — the recompiler and runtime this is
  built on, and the reason any of this is possible
- OpenBIOS, from [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux)
- [Dear ImGui](https://github.com/ocornut/imgui) and
  [SDL](https://github.com/libsdl-org/SDL)
- The GameShark code list this project's RAM map was bootstrapped from, whose
  original compilers are unknown
