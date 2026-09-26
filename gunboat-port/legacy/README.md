# Codex prototype (legacy)

What the project had before the faithful port started (phase 3). It is kept as a **reference**,
not as a base: nothing here is built on further, and it goes away once the port is playable.

* `src/game.cpp`, `src/simulation.cpp`: an **invented** patrol mode (float physics, hitscan, gun
  heat, a new renderer and instruments). These are not Gunboat's rules.
* `src/original_*.cpp`, `src/assets.cpp`: translations of original routines that were verified
  against GB.EXE (physics, weapons, PC-speaker effects, controls, sprites, terrain, archive, LZW),
  documented in `ORIGINAL_*.md` and tested by `tests/verify_*.py`. Phase 4 re-expresses them in
  the `mem[]` model of `../src/`, one original function at a time, with the new differential tests.
* `vendor/td3/`: the C host layer from the Test Drive III port (MIT, `vendor/td3/LICENSE`), which
  `../src/host.cpp` and `../src/platform/vga.cpp` replace in C++.

## Play it

Built with the main project unless `-DGB_LEGACY=OFF`: `build/gunboat_legacy.exe`, started by
`gunboat-port/Run Gunboat.cmd` (or `Play Native Gunboat.cmd` in the repository root). It needs
`GB.EXE` and `DATAA/B/C.DAT` in `Original DOS version/`.

Press **Enter** to start. Destroy the three numbered contacts, return to the green base marker on
the map, and stop. `BRG` is the bearing to the next objective, `HDG` the boat's heading.

| Control | Action |
| --- | --- |
| W / S or Up / Down | Throttle up / down; low throttle reverses |
| A / D or Left / Right | Steer |
| Space | Fire twin guns |
| Q / E, C | Aim left / right, center aim |
| X | Cut throttle and brake |
| M | Tactical chart |
| F5 | Cockpit / chase camera |
| F1–F4 | Load world 1–4 |
| Esc, Enter, R | Pause, start / resume, restart |
| Alt+Enter, F10 | Fullscreen, quit |

Options: `--practice`, `--world N`, `--seconds N`, `--state FILE.json`, `--screenshot FILE.bmp`,
`--dump-assets DIR`, `--check`, `--self-test`.

## Its tests

From the repository root: `python gunboat-port/legacy/tests/verify_assets.py` (executable
unpacking, six LZW pictures, 10,036 object records, 52,138 triangles, 1,056 sprite views, all
against the original), `verify_original_physics.py`, `verify_original_weapons.py`,
`verify_original_audio.py`, `verify_original_controls.py`, `verify_original_damage.py`,
`smoke_sdl.py`. They compare hand-picked state; the new tests in `../tests/difftest` compare
all memory.
