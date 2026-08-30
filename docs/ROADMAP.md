# Roadmap

Status of the recompilation, and what comes next. Update as things land.

## Done

- [x] Disc probed; retail `SCUS-94306` established as the single build target
      (`docs/DISC_NOTES.md`).
- [x] Project scaffolded on the New Project Layout with `psxrecomp` +
      `recomp-ui` submodules.
- [x] Framework recompiler built from source; game C + OpenBIOS backend
      generated (53 shards, ~1.97M lines, 2,744 dispatch entries, 4,008
      functions).
- [x] `build/` (Release) and `build-dev/` (RelWithDebInfo + debug server)
      both compile and link.
- [x] **First boot runs**: 3,712 frames in ~62 s (≈60 fps), PC inside the
      game's own text, VBlank IRQs delivering, no fatal.

## Next — bring-up

1. **See the screen.** Drive `build-dev/` over the TCP debug server
   (`screenshot_hires`, `present_shot`) and confirm what actually renders:
   Sony/SingleTrac logos → `MOVIES\SLOGO15S.STR` → attract loop → title.
2. **FMV.** The intro is MDEC `.STR`; verify streams decode and XA audio
   is in sync.
3. **Menus and input.** Shell (`SHELLDB1`/`SHELLDB2`) navigation via
   `set_input`; then car select, level select, options.
4. **Get into a match.** Load a level (`TMS`/`DMD`/`TERRAIN` assets) and
   confirm the 3D pipeline, GTE output and collision.
5. **Audio.** `SCREEN\UACORE.VAB` SPU bank, XA `.DA` streams, CDDA tracks
   02–12.
6. **Memory card.** Password/save flow.
7. **Soak.** Long runs per level hunting for divergence, freezes, and the
   ~4.5M interpreted `dirty_ram` instructions (find out what code that is —
   the game has no disc overlays, so it should be identifiable and
   statically recompilable).

## Then — coverage and accuracy

- Grow `seeds/ghidra_funcs.txt` from anything the runtime reports as
  not-yet-native; drive toward 100% static coverage. This title has **no code
  overlays**, so 100% is genuinely reachable — that is the headline goal.
- Populate `symbols.toml` as functions are identified. Use the Aug 1996
  prototype as a cross-reference (75.9% opcode-level match).
- Cycle/IRQ timing accuracy; SPU reverb; renderer parity SW vs GL.

## Then — enhancements

- **Widescreen.** Locate the cull sites and projection maths, then fill in a
  `[widescreen]` block. TM2 is a true 3D engine, so this is the 3D path.
- Geometry correction + perspective texturing (`[video]` flags, opt-in).
- Higher internal resolution / supersampling.

## Then — mods (`.psxmod` packages)

Planned, all default-disabled and independently toggleable:

- **Cut content from the Aug 1996 prototype** — `Kali Mode`, `Thor Mode`,
  the `ROOFEZ` easy-layout variant, and the reworded/cut strings listed in
  `docs/DISC_NOTES.md`.
- Quality-of-life toggles (skip FMV, unlock everything, etc.).

## Deferred

- **Netplay.** The framework supports rollback netplay and TM2 is a 2-player
  game, so it is an obvious fit — but the docs are explicit that a title
  should boot and soak first. Flip `PSX_NETPLAY` in `CMakeLists.txt` when
  bring-up is done.
- CI / release packaging / GitHub remote. Nothing is published yet.
