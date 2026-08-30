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

Verified working end to end: the package resolves, the plugin activates, the
cheat engine patches through the recompiled code path, and the OSD renders.
Proven with the two FMV-skip cheats, which took the game straight to the title
screen with `F1  2 cheats active` shown.

**The key bindings themselves have not been exercised** — they need a real
keyboard, which automated testing here cannot provide. If F1 does not respond,
that is the first thing to check, and the likely cause is SDL keyboard focus
rather than the plugin.

## Possible upgrades

- A real multi-line overlay menu with pause-while-open and controller
  navigation, matching how F7/F8 work. That means adding a `HOST_KEYMAP_*`
  entry and an overlay module to the `psxrecomp` submodule, so it was
  deliberately deferred to keep the framework unforked.
- Promoting verified cheats into named functions in `symbols.toml` as the RAM
  map firms up (see `docs/RAM_MAP.md`).
