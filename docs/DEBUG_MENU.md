# Debug menu

An in-game debug menu exposing the imported GameShark list as toggleable
functions. It is a **mod**, not a change to the game: `generated/` stays
vanilla, and the whole thing can be switched off.

## How the pieces fit

```
../Cheats.txt                     raw GameShark list (source of truth)
  |  tools/gs_import.py
  +-> mods/tm2-debug/cheats.toml  human-readable record, validated vs the EXE
  +-> src/mods/tm2_cheat_table.h  the same table, compiled in

src/mods/tm2_debug.c              plugin: cheat engine + F1 menu
  |  CMakeLists.txt EXTRAS_SOURCES
  +-> compiled into psx-runtime, registered as id "tm2.debug.menu"

mods/preloaded/packages/tm2.debug/1.0.0/manifest.toml
                                  .psxmod package selecting that plugin id;
                                  default-disabled, channel = "developer"
```

The archive never carries native code. The manifest names a **registry id**,
and the implementation compiled into the executable claims it. That is the
framework's trusted-plugin contract, and it is why the debug menu can be a mod
without the mod system being able to run arbitrary code.

Regenerate the table after editing the cheat list:

```bash
python tools/gs_import.py ../Cheats.txt -o mods/tm2-debug/cheats.toml \
    --emit-c src/mods/tm2_cheat_table.h
```

## Enabling it

`channel = "developer"` and `default_enabled = false`, so it is off until
selected. Turn it on in the launcher's Mods page, or write
`build-dev/mods/state.toml` directly:

```toml
format_version = 2

[[feature]]
package_id = "tm2.debug"
id = "debug-menu"
enabled = true
```

## Controls

The framework already owns F7 (savestates) and F8 (rewind). Everything else
here is deliberately chosen from keys that are **not** in `keybinds.ini`, so
the game never receives a keystroke meant for the menu — the plugin polls SDL
directly and cannot consume events the runtime owns.

| Key | Action |
|---|---|
| `F1` | open / close the menu |
| `PageUp` / `PageDown` | previous / next cheat |
| `Home` / `End` | previous / next category |
| `Insert` | toggle the selected cheat |
| `Delete` | turn every enabled cheat off |

The OSD is a single 64-character line, so the menu is a one-line selector
rather than a list:

```
 12/132 RAM God Mode P1                [ON]
```

While the menu is closed and anything is active, the line becomes a reminder
(`F1  2 cheats active`) so a stray toggle is never invisible.

## Scripted sessions

`TM2_DEBUG_CHEATS` switches cheats on at activation, by the slug ids in
`cheats.toml`. Useful for reproducible debugging and for driving the engine
from a script:

```bash
TM2_DEBUG_CHEATS=skip-single-trac-movie,skip-intro-movie \
  build-dev/TwistedMetal2_Recompiled.exe --no-launcher --debug-port 4370 \
  --disc "..."
```

Unknown ids are reported on stderr and skipped.

## External GUI panel

`tools/tm2_debug_gui` is a Dear ImGui panel with a checkbox per cheat,
collapsible category sections, a filter box and colour-coded region tags.

It is a **separate process on purpose.** The plugin's callback runs on the
emulation thread at guest VBlank, so an in-process panel would have to create
a window and borrow the game's OpenGL context from inside guest execution.
Out of process, nothing it does can disturb the renderer, the event loop or
timing, and if it crashes the game keeps running.

It costs no new dependencies: Dear ImGui and the SDL3/OpenGL3 backends are
already vendored in `recomp-ui` and linked into this build.

```bash
cmake -S . -B build-dev -G Ninja -DPSX_DEBUG_TOOLS=ON -DTM2_BUILD_DEBUG_GUI=ON
cmake --build build-dev --target tm2-debug-gui
```

The binary lands next to the game. Start the game (with the mod enabled), then
run `tm2-debug-gui`. It polls twice a second, so it follows changes made with
the in-game F1 menu and reconnects on its own when the game restarts.

| Flag | Effect |
|---|---|
| `--port N` | control port (default 4371; `TM2_DEBUG_GUI_PORT` also works) |
| `--selftest` | connect, toggle a cheat, verify, restore, exit with a status code |
| `--screenshot F.bmp` | render one frame to a file and exit |

The last two exist so the panel can be checked without a human at the screen,
which is otherwise impossible for a GUI.

### Control protocol

Line-oriented text on `127.0.0.1:4371`, served by `src/mods/tm2_debug_ipc.c`.
Not JSON: the peer is ours, and a hand-rolled JSON parser in C would have been
the largest and least reliable part of that file.

```
LIST                OK <n>, then n rows, tab-separated:
                    <index> <enabled> <region> <category> <name> <id>
SET <index> <0|1>   OK
TOGGLE <index>      OK <enabled>
CLEAR               OK
STATUS              OK <active> <total>
PING                OK
```

The server is bound to loopback, non-blocking throughout, and polled from the
VBlank callback -- a single blocking `accept`, `recv` or `send` there would
stall the game, so partial sends are buffered per client and retried on later
frames.

## Two things specific to a recompilation

**Code patches cannot just poke RAM.** The game is statically recompiled, so
the native code for an address already exists and would never observe a plain
memory write. Ops in the EXE region go through `psx_mod_write_code_word()`,
which replaces the instruction *and* routes that address through the runtime's
executable-RAM path. This is what makes "Skip Intro Movie" work: it NOPs a
`jal` and the recompiled code actually takes the new path.

**Everything is re-applied every VBlank**, which is what real GameShark
hardware does. A one-shot write would be undone by the game's own logic on the
next frame.

**Turning a code patch off restores the original instruction.** RAM writes
need no undo -- the game overwrites those itself, which is precisely why they
must be re-applied every frame. An instruction is different: nothing in the
game ever writes it back, so the stock word is recorded at import time and put
back on the transition to disabled. Verified by round-tripping
`drive-through-walls`: `jal` -> NOP -> `jal` -> NOP -> `jal`.

## Region tags, and the risk

Each entry is tagged by the memory it touches:

| Tag | Meaning |
|---|---|
| `RAM` | runtime state (BSS); safe at any time |
| `EXE` | patches an instruction inside the executable |
| `LVL` | streamed level data in low RAM |

`LVL` entries are only meaningful once the matching level is loaded. Enabling
one at the wrong moment writes into whatever else currently occupies that
memory, which can corrupt level data and produce a confusing crash later.
**Nothing is gated** — this was a deliberate call; the tag is information, not
a restriction.

## Status

Verified end to end against the running game:

- the package resolves and the plugin activates;
- the cheat engine patches through the recompiled code path (the two FMV-skip
  cheats took the game straight to the title screen, with `F1  2 cheats
  active` on the OSD);
- enabling and disabling round-trips an instruction cleanly;
- the control server answers `LIST` with all 132 entries, and `SET` from a
  socket reaches guest memory;
- `tm2-debug-gui --selftest` passes, and a rendered frame was inspected.

**Two things have not been exercised**, both needing a human at the machine:
the F1 key bindings, and clicking the GUI's checkboxes. The code paths behind
both are covered by the tests above — the same `SET` the checkbox sends is
what the selftest drives — but the input plumbing itself is unproven. If F1
does not respond, suspect SDL keyboard focus before the plugin.

## Possible upgrades

- A real multi-line overlay menu with pause-while-open and controller
  navigation, matching how F7/F8 work. That means adding a `HOST_KEYMAP_*`
  entry and an overlay module to the `psxrecomp` submodule, so it was
  deliberately deferred to keep the framework unforked.
- Promoting verified cheats into named functions in `symbols.toml` as the RAM
  map firms up (see `docs/RAM_MAP.md`).
