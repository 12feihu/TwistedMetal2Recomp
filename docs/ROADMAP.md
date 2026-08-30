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
- [x] **First boot runs.** 12,477 frames over ~3.5 min at ~60 fps, VBlank
      IRQs delivering, `fatal: null`, no crash dump.
- [x] **Boots all the way into 3D.** Verified by screenshot over the TCP
      debug server: SingleTrac logo FMV (MDEC 24bpp scanout) -> attract
      loading screen (`LOADING / LOS ANGELES`, TIM art and fonts correct) ->
      **in-game attract demo rendering the Los Angeles arena** with textured
      terrain and walls, car model, skybox, a semi-transparent light beam and
      a health pickup. GPU command traffic climbs steadily while the
      demo runs (~7.4M GP0 writes over a 3 s sample).
- [x] **Audio works.** The main-menu music that looked missing simply starts
      when an option is selected — not a bug.
- [x] **Controller input works.** Root cause was the pad type, not the
      bindings: the runtime presented a DualShock (id 0x73) and the game's
      1996 libpad discards it. Pinned to digital via `[controller] lock_mode`
      in `game.toml` — see `docs/DISC_NOTES.md`.

## Next — bring-up

1. **Play a real match.** Menus and input respond; next is an actual game —
   collision, weapons, HUD, and each of the levels.
2. **Two players / split-screen.** Untested. Note `settings.toml` carries
   `multitap = true` while the game has no multitap support; check whether
   that matters once a second pad is seated.
3. **Memory card.** Password and save flow.
4. **Renderer parity.** Compare software vs OpenGL output on the same frames;
   the software rasterizer is the reference look.
5. **Audio detail.** Broadly working; still worth checking XA/CDDA transitions
   and FMV sync closely.
6. **Soak.** Long runs per level hunting divergence and freezes.

## Then — coverage and accuracy

- Account for the interpreted `dirty_ram` instructions (~5.2M over 12.5k
  frames). The game loads no disc overlays, so whatever runs there should be
  identifiable and statically recompilable. Driving it to zero is the
  headline goal.
- Grow `seeds/ghidra_funcs.txt` from anything the runtime reports as
  not-yet-native; push toward 100% static coverage.
- Populate `symbols.toml` as functions are identified. Use the Aug 1996
  prototype as a cross-reference (75.9% opcode-level match).
- Cycle/IRQ timing accuracy; SPU reverb.

## Then — enhancements

- **Widescreen.** Locate the cull sites and projection maths, then fill in a
  `[widescreen]` block. TM2 is a true 3D engine, so this is the 3D path.
- Geometry correction + perspective texturing (`[video]` flags, opt-in).
- Higher internal resolution / supersampling.

## Then — mods (`.psxmod` packages)

Planned, all default-disabled and independently toggleable:

- **Cut content from the Aug 1996 prototype** — `Kali Mode`, `Thor Mode`, the
  `ROOFEZ` easy-layout variant, and the reworded/cut strings listed in
  `docs/DISC_NOTES.md`.
- Quality-of-life toggles (skip FMV, unlock everything, and so on).

## Deferred

- **Netplay.** The framework supports rollback netplay and TM2 is a 2-player
  game, so it is an obvious fit — but the docs are explicit that a title
  should boot and soak first. Flip `PSX_NETPLAY` in `CMakeLists.txt` when
  bring-up is done.
- CI / release packaging / GitHub remote. Nothing is published yet.
