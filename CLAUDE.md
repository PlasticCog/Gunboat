# Gunboat: faithful C++/SDL3 source port

Goal: a behaviour-exact port of Accolade's **Gunboat** (DOS, 1990; `GB.EXE`), rewritten function by
function in C++ on SDL3. It reads the original game files at run time and never redistributes them.
The method is the one that produced the finished Test Drive III port (`test-drive-3-sdl3/`, same
engine family): **reverse-engineer → document → port each original function → verify it against the
original machine code.** Enhancements (resolution, widescreen, 60 fps, draw distance) live in a separate
layer (`gunboat-port/src/enhanced/`), are each the player's choice, and never change the faithful
core.

## Ground rules

* **The shipped game is C++ and SDL3 only.** No Python, Java, JavaScript or emulator at run time.
  Any tool is fine for research and tests: Python, Capstone, Unicorn, Ghidra (JDK), DOSBox.
* **No invented gameplay.** Every rule must come from the original code. If something is unknown,
  find it in the disassembly; don't approximate it.
* **Enhancements are options, never changes to the game.** Each is switched by the player (the
  launcher, the command line, F11 in the game); "Original" shows exactly the faithful picture. They
  only read the game's memory: anything they run on it (the capture's scratch runs of ported
  routines) is undone before the game continues, and `scene_enhanced.py` checks that.
* The original game data is copyrighted and never goes to GitHub: the game files live in `Game/`
  (as in the Test Drive III port; only `Game/README.md` is tracked), and everything derived from
  them (`reverse_engineering/out/`, `gunboat-port/out/`) stays out of git too.
* **GitHub** (`origin` = github.com/PlasticCog/Gunboat, public): push only what the remake needs
  (port code, tests, specs, symbols, the RE tools and format docs). No game files, generated
  output, tool installs, editor settings or archived material.

## Layout

| Path | What |
| --- | --- |
| `gunboat-port/` | The C++ port (CMake + SDL3). **`gunboat-port/PORTING.md`**: code layout, memory API, how to port and test a function |
| `gunboat-port/tests/difftest/` | Differential tests: `gbdiff.py` harness, `bridge.cpp`, `test_*.py` |
| `gunboat-port/src/enhanced/` | The presentation layer: launcher, settings, the enhanced 3D view and presenter (PORTING.md, "The enhancement layer") |
| `reverse_engineering/RE_GUIDE.md` | **Read first**: addresses, segment map, files, how to regenerate |
| `reverse_engineering/symbols.csv` | The single name list for functions and globals |
| `reverse_engineering/map/` | Function index and TD3 matches (generated, tracked) |
| `reverse_engineering/spec/` | One spec per subsystem, with its `<owner>_symbols.csv` |
| `reverse_engineering/tools/` | Unpacker, indexer, matcher, disassembler, Ghidra scripts, format tools |
| `reverse_engineering/out/` | Generated: unpacked EXE, Ghidra project, decompilation `decomp/gb_ds.c` (ignored) |
| `Game/` | The original game's files (ignored except its README): the port, the tests and the tools read them here |
| `Original DOS version/` | Local reference copy of the release (ignored): its DOSBox set-up, `SteelThunder/` (the predecessor game) |
| `test-drive-3-sdl3/` | Reference port (MIT, separate git repo, ignored): specs, tools, `td3port/` skeleton |
| `_tools/` | Ghidra install (ignored) |

## Porting rules (adapted from `test-drive-3-sdl3/td3port/PORTING.md`)

* **Memory model.** Original state lives in one `mem[]` array at its original real-mode addresses:
  the image at segment `0x1000`, DGROUP at `0x2B73`, VGA page 0 at `A000:0000`. Every global,
  table and heap block is read and written there through typed accessors, named from
  `symbols.csv` (`ds_u16(DS_station)`), or as a raw offset with a comment. **Never keep game
  state in C++ variables that outlive a function.** This lets each ported function drop straight
  into the running game and be tested against the original on identical memory.
* **One C++ function per original function**, named as in `symbols.csv`, with a leading comment
  `// 0919:8930 game_frame (spec simulation.md §4)`. Assembly routines with register arguments
  become functions with explicit parameters and returns named after the registers.
* **Exact arithmetic.** Use `u8/s8/u16/s16/u32/s32` at the original register widths and truncate
  where the original does. Signed shift = `s16(x) >> n`. Model `adc/sbb/rcl/rcr` carries
  explicitly. Every DIV/IDIV goes through one helper that reproduces divide-error behaviour.
* **Keep the original's bugs and quirks.** Mark every deliberate deviation `// PORT: why`, and
  every unresolved doubt `// TODO(verify): what`.
* **Hardware and DOS** (port I/O, interrupts, INT 21h files, PIT, keyboard ISR, VGA registers,
  speaker, AdLib) go through the host layer. SDL is included only by the host (`src/host.cpp`)
  and the presentation layer (`src/enhanced/`); the core (`gbcore`) never includes it.
  Every busy-wait loop of the original calls the host pump once per iteration.
* Copy protection: take the "passed" path (`// PORT:`). Only VGA mode 13h is ported first;
  EGA/Tandy/CGA paths and the CMS driver are parked.

## Verification (required for every ported function)

1. **Differential test**: run the original function in Unicorn and the C++ function on the same
   randomized memory, then compare all memory and the return registers:
   `Harness.check` in `gunboat-port/tests/difftest/gbdiff.py` (PORTING.md).
2. **Scene checks**: compare frames against DOSBox captures of the original
   (`reverse_engineering/out/dosbox_captures`) and headless snapshots (`SDL_VIDEO_DRIVER=dummy`):
   `gunboat-port/tests/scenes/scene_check.py` (title), `scene_mission.py` (cockpit, map);
   `scene_enhanced.py` checks the enhanced view against the original's pixels.
3. Report results honestly, with numbers. A test that was skipped or narrowed must be stated.

## Commands (Windows; PowerShell 5.1 or Git Bash)

```text
# toolchain: MSYS2 UCRT64 (GCC 15, CMake, Ninja) at C:\msys64\ucrt64\bin; Python 3.13 with capstone, unicorn, pillow
powershell -File gunboat-port/Build.ps1                        # configure, build, ctest (3 tests)
gunboat-port/build/gunboat.exe --check                         # finds Game/ by itself
python gunboat-port/tests/difftest/run_all.py [-k name]        # differential tests (builds gb_difftest)
python gunboat-port/tests/scenes/scene_enhanced.py             # the enhancements, headless (needs the game)
gunboat-port/build/gunboat.exe --help                          # launcher, --original / --enhanced, display options
python reverse_engineering/tools/merge_symbols.py              # spec/*_symbols.csv -> symbols.csv
python reverse_engineering/tools/symbols.py                    # validate symbols.csv after editing it
python reverse_engineering/tools/gen_symbols.py                # symbols.csv -> gunboat-port/src/symbols.hpp
powershell -File reverse_engineering/tools/decompile.ps1       # Ghidra headless decompile, ~minutes
python reverse_engineering/tools/x86dis.py reverse_engineering/out/GB_unp.exe dis 0919:8930 0x40
```

Regenerating the map from scratch: `reverse_engineering/RE_GUIDE.md`, "Regenerating".

## Current state (2026-09-26)

* **Phases 1–3 (map, specs, core): done.** 597 functions indexed; eight subsystem specs plus
  `adlib_driver.md`; `mem[]`, the EXEPACK loader, the SDL3 host, the differential harness.
* **Phase 4, the faithful port: done for VGA.** The whole game runs natively in `gunboat.exe`:
  start-up, the title with its music (PC speaker, or AdLib through a translation of the user's
  `ADLIB.COM`), the menu and the demo, the headquarters, the front end, every mission (the four
  regions and the practice missions: simulation, 3D view, cockpit, all stations, map, damage
  report), the debrief and the roster file. 433 GB.EXE functions and 52 ADLIB.COM routines are
  ported. Not ported, by design: the EGA/CGA/Tandy/Hercules drawing paths (parked, VGA only),
  the MT-32 and CMS sound back ends (parked), the text-mode console of segment 17f0 (PORT: the
  configuration questions are answered with Enter, fatal messages go to a message box), the C
  runtime internals (modelled), dead code.
* **Verification:** 214 differential tests, 174,770 cases, 0 mismatches (all memory, registers
  where callers use them, DAC writes, speaker/timer/OPL events, open and written files), including
  whole missions (the demo, and missions touring every station) and whole front-end runs. Scene
  checks against the DOSBox captures: the title screens 100%, the pilot's cockpit 99.47% and the
  map 99.97% (the differences are the moments: water marks, the clock, the boat marker). How the
  work was split and merged: packages ported by parallel agents in git worktrees
  (`.claude/worktrees/pkg-*`), each function tested before merging.
* **Register flows** matter across Gunboat's assembly routines: several callees take and leave
  AX/SI that later code stores (cockpit lamps store AH, `caller_si` DS:D6F6 stores SI, the view
  copies leave SI). They are threaded as parameters and return values (simulation.md §13,
  hud.hpp, sim.hpp); the whole-mission tests check them.
* **PORT decisions of the mission:** frame pacing of the 3D stations (`--fps`, default 15: the
  mission clock in real time; the original ran as fast as the PC drew); `visible_list_rebuild`'s
  runaway copy on an empty terrain window (Mare Island open water) stops with a fatal error
  (render3d.md). Kept original crashes: `route_point`'s divide error (R6003) when the crew pilot
  leaves the map's top row. Both are `TODO(verify)` in DOSBox.
* **Phase 5, enhancements: first version done (2026-09-26)**, in `gunboat-port/src/enhanced/`,
  every one optional (the player's settings in `%APPDATA%\Gunboat\gunboat.ini`, written by the
  launcher; command-line overrides; F11 switches enhanced/original in the game):
  * a launcher in the game's window (SDL's debug font): game folder, preset Original / Enhanced,
    each enhancement, picture aspect (4:3 or square pixels), scaling (sharp, nearest, smooth,
    CRT scanlines), window or full screen, sound device;
  * high-resolution 3D view: at each frame of a 3D station the host's frame hook captures DGROUP,
    page 1's view window, which page 0 pixels show the view and from where (view_present run on
    a scratch copy of memory with the window filled with its coordinates) and the visible
    objects' sprites at their natural size (sprite_cache_build at size 17h); `view3d` draws the
    view again at the window's resolution with the original's projection, draw order, colours and
    sprite scale factors in floating point; the presenter shows it under the original cockpit
    (drawn with holes where the view shows);
  * smooth motion: the view drawn at 60 fps between the last two captured frames (the camera
    and moving objects interpolated, one game frame behind); the game keeps its 15 frames/s;
  * widescreen, three choices: off; the world beside the 4:3 picture (not for the pilot's
    sheared side windows or the black-framed chase view); or the **wide cockpit** (default of
    the Enhanced preset): the frame widened to the window's edges by seam insertion (`widen.cpp`):
    in each outer band the 16 cheapest top-to-bottom paths (cost: neighbour differences, strong
    edges weighted and spread 3 columns, sideways steps, the view's openings free, pixels seen
    changing avoided) get a block of the columns before them repeated; the message line is padded
    at its ends; at most 28 columns a side (16:9 with square pixels), a wider window draws the
    widened frame a little wider (the view too, so aiming stays exact); the widening is made again
    when the station's cockpit changes or a changing pixel lands on a repeated column;
  * extended draw distance: the terrain cells 2..5 from the boat's (the game draws 3 x 3), loaded
    with `tile_load` on the scratch memory (with their structures and scenery) when the window's
    centre cell or the time-of-day colours change, drawn behind the game's terrain far to near
    with the objects standing in them (scenery and authored objects; their images built like the
    visible ones, by kind and view);
  * checks (`scene_enhanced.py`): the view drawn again at 1x equals the original's pixels on
    94.7% (pilot practice) and 97.0% (night gunnery) of the view (the rest: sub-pixel terrain
    edges, the original's bit-pattern sprite scaling), no capture changes the game's memory,
    60 presents/s with the game at 15.0 frames/s; capture 0.3-0.4 ms, drawing ~0.5-1.3 ms per
    present at 710-852 x 400-480 (software renderer).
  * the game folder `Game/` (2026-09-26, as the TD3 port): `gunboat.exe` looks for `Game` next to
    itself, one or two folders up (a build in `gunboat-port/build`), then in the current folder;
    the launcher or `--game-dir` can point elsewhere. The files were copied there from the local
    `Original DOS version/`.
* **Next:** optionally the parked video modes and sound devices; more enhancements only as
  player options.
* The Codex prototype (an invented patrol mode, `gunboat-port/legacy/`) and the local `archive/`
  were removed on 2026-09-26 when the port replaced them (the prototype is in the git history).
