# Twisted Metal 2 Recompilation — working notes

Static recompilation of **Twisted Metal 2 (USA), SCUS-94306** on the
[psxrecomp](https://github.com/mstan/psxrecomp) framework (New Project Layout:
`psxrecomp/` and `recomp-ui/` are root-level submodules).

Read `docs/DISC_NOTES.md` first — it records everything established about the
disc and the executable.

## Repo layout

```
game.toml              game identity, disc path, recompiler + runtime settings
symbols.toml           progressive symbol map -> psx_symbols.h (tools/sync_symbols.py)
seeds/ghidra_funcs.txt function-entry seeds fed to the recompiler
disc/SCUS_943.06       boot EXE extracted from the disc (gitignored)
generated/             recompiled game C (gitignored, ~2M lines / 53 shards)
psxrecomp/generated/   recompiled OpenBIOS backend (gitignored)
docs/                  project documentation
analysis/              local scratch: rips, captures, parity dumps (gitignored)
```

## Build on this machine

There is no CMake/Ninja/compiler on `PATH`. Everything must run through the
MSVC environment wrapper:

```bat
scripts\env.cmd <command...>
```

It calls `vcvars64.bat` from the VS 2022 install at `G:\Tools\Visual Studio`
and prepends VS's bundled CMake 3.31 + Ninja 1.12 and Git's `bin`/`usr\bin`
(for `bash`, needed by `psxrecomp/tools/ci/*.sh`).

```bat
scripts\env.cmd python psxrecomp\psxrecomp_cli.py generate --config game.toml --project-root .
scripts\env.cmd cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
scripts\env.cmd cmake --build build --parallel
```

### Two build trees

| Dir | Config | Use |
|---|---|---|
| `build/` | `Release` | what a player would get; `PSX_DEBUG_TOOLS` is **OFF** |
| `build-dev/` | `RelWithDebInfo -DPSX_DEBUG_TOOLS=ON` | day-to-day work |

`PSX_DEBUG_TOOLS=ON` is what enables the **TCP debug server** (`--debug-port
4370`). Without it `--debug-port` is silently accepted and nothing listens.
The server is how we inspect and drive the game without a human at the
keyboard:

```bat
scripts\env.cmd python psxrecomp\tools\debug_client.py ping
scripts\env.cmd python psxrecomp\tools\debug_client.py regs
scripts\env.cmd python psxrecomp\tools\debug_client.py screenshot_hires path=shot.png
scripts\env.cmd python psxrecomp\tools\debug_client.py set_input buttons=...
```

Full command list: `psxrecomp/TCP_COMMANDS.md`.

Launch for a work session:

```bat
build-dev\TwistedMetal2_Recompiled.exe --no-launcher --debug-port 4370 ^
  --disc "D:\ClaudeProjects\TWISTE~1\GAME(P~1\TWISTE~1\TWISTE~1.CUE"
```

## Environment gotchas (all hit and worked around already)

1. **Spaces in the parent path.** The repo lives under
   `D:\ClaudeProjects\Twisted Metal 2 Recomp\`. CMake and the framework handle
   that fine, but the *released* `psxrecomp.exe` CLI splits its own framework
   path on the first space. Use the 8.3 short path
   `D:\ClaudeProjects\TWISTE~1\...` for any tool that misbehaves.

2. **Windows PowerShell 5.1 only** (no `pwsh`). It reads a BOM-less script as
   cp1252, so a UTF-8 em dash decodes to `â€"` — and that trailing `"` is
   U+201D, which PowerShell treats as a *string delimiter*. Any framework
   `.ps1` with an em dash fails to parse. Fix: add a UTF-8 BOM to the script.
   Hit during scaffolding via
   `psxrecomp/tools/new_project_layout/setup_project.ps1`; that script has
   already done its job here, but the same trap applies to any other
   framework `.ps1` we reach for.

3. **Python 3.10** — the framework's `sync_symbols.py` wants 3.11+ `tomllib`.
   `pip install tomli` satisfies it (done).

4. **Set `PYTHONUTF8=1`** for framework Python tools; several print em dashes
   and die on the cp1252 console encoding otherwise.

5. **No ccache.** The generated C is ~2M lines; without ccache every git branch
   operation forces a full recompile. Worth installing.

## Recompilation status

`generate` emits **2,744 dispatch entries / 4,008 functions** covering the
whole 0xB3800 text, plus the OpenBIOS backend. The game ships **no code
overlays** (see `docs/DISC_NOTES.md`), so 100% static coverage is reachable —
unusual and the main reason this title is a good fit.

## Prototype disc

`Twisted Metal 2 (Prototype - Aug 26 1996)` is a **reference, not a build
target**. 75.9% of retail's code matches it at the opcode level, so it is
useful for naming functions, and it carries cut content (`Kali Mode`,
`Thor Mode`, `ROOFEZ`, reworded level names) that is planned as toggleable
`.psxmod` packages once retail boots.

## Custom code lives in mods, not in the game

The recompiled game C in `generated/` stays **vanilla**. Everything we add is
a trusted plugin under `src/mods/`, compiled into `psx-runtime` via
`EXTRAS_SOURCES` in `CMakeLists.txt` and selected at resolve time by a
`.psxmod` package in `mods/preloaded/packages/`. The archive never carries
native code; the manifest names a registry id and the compiled-in
implementation claims it.

First consumer: the F1 debug menu / GameShark cheat engine. See
`docs/DEBUG_MENU.md` and `docs/RAM_MAP.md`.

## Conventions

- Never commit disc images, BIOS dumps, `generated/`, or ripped assets.
- Record every function you identify in `symbols.toml`, then re-run
  `python tools/sync_symbols.py --game "Twisted Metal 2"`. Keep
  `status = "guessed"` until verified; only set `emit = true` when the entry is
  safe to own for AOT / host hooks.
