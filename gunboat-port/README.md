# Gunboat: the C++ / SDL3 port

The faithful port of `GB.EXE`, rebuilt one original function at a time on the original memory
layout (see `/CLAUDE.md` for the method and `PORTING.md` for how the code is organised).
**The whole game runs natively** in VGA: the title and its music, the menu and the demo, the
practice missions, the headquarters, the front end, every campaign mission with its stations, map
and damage report, the debrief and the roster. `Run Port.cmd` starts it (`--fps N` sets the 3D
stations' frame rate, default 15; `--sound adlib|speaker`).

| Part | State |
| --- | --- |
| Memory model and loader (`src/mem.*`) | GB.EXE (EXEPACK) unpacked, relocated and checked in C++ |
| Host layer (`src/host.*`, `src/platform/vga.*`) | TD3's SDL3 host in C++: window, the PIT rates GB.EXE programs, keyboard, gamepad, OPL2 + speaker audio, files |
| Platform (`src/platform/`) | DOS files and memory, the C runtime models, the BIOS model, the graphics library (VGA), palette, LZW pictures, text, keyboard and timer interrupts, joystick (host gamepad) |
| Game flow (`src/game/flow_*`) | main, config_load, the archive and far buffers, keys and the demo script, the title sequence, the credits, the menu, the headquarters quiz, the roster file, the whole front end (office, roster, personnel files, briefings, maps, spec sheets, outfitting, debrief) |
| Simulation (`src/game/sim_*`) | game_frame, keys and controls, crew pilot and gunners, routes, engines and propulsion, motion, weapons and projectiles, enemies and incoming fire, damage, messages, the mission clock |
| 3D renderer (`src/render/`) | camera, terrain window and projection, primitives and VGA spans, visible objects, the sprite cache and blitter, spotlights, flash and shake |
| Cockpit (`src/hud/`) | lamps, switches, gauges, radar, the view copies, the gun frames, window cracks, map, damage report and assignment screens |
| Mission (`src/mission/`) | mission_run, mission_load and mission_setup, the station screens, view_present, view_restore |
| Sound (`src/sound/`) | the effects driver, the music sequencer, the title music on the PC speaker or on an AdLib through the translated Ad Lib driver `ADLIB.COM` (`--sound adlib\|speaker`, default adlib when the game folder has it), the device detection (MT-32, CMS parked) |
| Ported functions | 433 GB.EXE functions and 52 ADLIB.COM routines, each matching the original on all memory (not ported by design: the EGA/CGA/Tandy/Hercules paths, MT-32/CMS, the text console) |
| Differential tests (`tests/difftest/`) | 214 tests, 174,770 cases, 0 mismatches: memory, registers, DAC writes, speaker, timer and OPL events, open and written files; whole missions and front-end runs |
| Scene checks (`tests/scenes/`) | the title screens match DOSBox captures on 100% of pixels, the pilot's cockpit on 99.47% and the map on 99.97% (`scene_mission.py`: the rest is the moment: water marks, clock, boat marker) |

## Build and test (Windows)

MSYS2 UCRT64 (GCC 15, CMake, Ninja); SDL3 3.4.16 is under `deps/`:

```powershell
powershell -File gunboat-port/Build.ps1        # configure, build, ctest
```

or by hand:

```text
cmake -S gunboat-port -B gunboat-port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build gunboat-port/build
ctest --test-dir gunboat-port/build --output-on-failure
```

The tests: `load_exe` (`gunboat --check`), `host_timer` (`gunboat --host-test`, headless),
`difftest` (`tests/difftest/run_all.py`, needs Python 3 with `unicorn` and `capstone` and
`reverse_engineering/out/GB_unp.exe` from `unexepack.py`).
`python gunboat-port/tests/scenes/scene_check.py` runs the port headless and compares its frames
with the DOSBox captures in `reverse_engineering/out/dosbox_captures`.
`GB_GAME_DIR` points the tests at the original files (default `../Original DOS version`).

`python gunboat-port/tests/difftest/run_all.py [--scale N] [--seed N] [-k name]` runs the
differential tests alone (it builds `gb_difftest` first) and writes `out/difftest-report.json`.

GCC's runtime is linked statically: `gunboat.exe` needs only `SDL3.dll` beside it.

## Credits

Host layer, VGA model, EXEPACK decoder and tools adapted from the Test Drive III port (MIT,
© 2026 Krzysztof Kania); Nuked-OPL3 (LGPL-2.1); SDL3 (zlib). Details in `THIRD_PARTY.md`.
