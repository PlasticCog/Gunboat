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
| `gunboat-port/src/enhanced/` | The presentation layer: launcher, settings, the enhanced 3D view and presenter, the AdLib sound effects (PORTING.md, "The enhancement layer") |
| `gunboat-port/tools/sfx_editor/` | `gunboat_sfx_editor`: the AdLib sound effects' instruments (C++, SDL3, Dear ImGui) |
| `gunboat-port/data/sfx.ini` | The AdLib instruments the port ships with, built into the game; the editor saves it when run from `gunboat-port/build` |
| `.github/workflows/release-linux.yml` | Builds the Linux package of a published release (Ubuntu 22.04, SDL3 static) and attaches it |
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
* Copy protection: take the "passed" path (`// PORT:`). Every video card of the original is ported
  (VGA, EGA, Tandy, CGA, Hercules: the player's choice, `platform/card.*`); the CMS and MT-32
  drivers and the Tandy sound chip are parked.

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
powershell -File gunboat-port/Release.ps1                      # Windows release zip (exes, SDL3.dll, empty Game/)
bash gunboat-port/Release.sh                                   # Linux release tar.gz (CI: .github/workflows/release-linux.yml)
gunboat-port/build/gunboat_sfx_editor.exe [--wav DIR]          # the AdLib effects editor; --wav renders every effect
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
* **Phase 4, the faithful port: done, on every video card.** The whole game runs natively in `gunboat.exe`:
  start-up, the title with its music (PC speaker, or AdLib through a translation of the user's
  `ADLIB.COM`), the menu and the demo, the headquarters, the front end, every mission (the four
  regions and the practice missions: simulation, 3D view, cockpit, all stations, map, damage
  report), the debrief and the roster file. 485 GB.EXE functions and 52 ADLIB.COM routines are
  ported. Not ported, by design: the MT-32 and CMS sound back ends and the Tandy sound chip (parked), the text-mode console of segment 17f0 (PORT: the
  configuration questions are answered with Enter, fatal messages go to a message box), the C
  runtime internals (modelled), dead code.
* **Verification:** 359 differential tests, 193,273 cases, 0 mismatches (all memory, registers
  where callers use them, DAC writes, speaker/timer/OPL events, open and written files, and on the
  other video cards the whole card state), including
  whole missions (the demo, and missions touring every station) and whole front-end runs. Scene
  checks against the DOSBox captures: the title screens 100%, the pilot's cockpit 99.47% and the
  map 99.97% (the differences are the moments: water marks, the clock, the boat marker). How the
  work was split and merged: packages ported by parallel agents in git worktrees
  (`.claude/worktrees/pkg-*`), each function tested before merging.
* **The other video cards (2026-09-26)**: EGA (mode 0Dh), Tandy (9), CGA (4) and Hercules (the
  game's CGA mode 4 shown by the Hercules CRTC, 640 x 300), chosen in the launcher ("Video card") or
  with `--video`; `config_load` gets the matching mode (PORT: the configuration's answer). The card
  is a model outside `mem[]` (`platform/card.*`: EGA planes, latches, sequencer, graphics
  controller, attribute palette; CGA/Tandy registers; Hercules CRTC; the picture each shows), with
  its twin `cardmodel.py` for the original in Unicorn (EGA memory as MMIO); every check compares the
  whole card state. Ported by five parallel packages: the library's handlers of each mode, the
  palette routines, the display offset, the EGA text, the dissolve, the renderer's twins
  (`render/mode_ega/cga/tandy.cpp`), the view copies' twins (`hud/views_*.cpp`), the EGA plane
  set-ups; 52 more functions. Tests `test_modes*.py` (card by card) and, end to end, the title, the
  demo, three practice missions touring the stations and five whole front ends on each card with
  the original untouched (`test_modes_whole.py`, 52 cases) and with the port's library
  (`test_modes_game.py`). Headless runs reach the title, the menu and the cockpit on each card.
  Not done: DOSBox captures of those cards (none exist; the whole-run tests stand in), the EGA
  pel panning in the picture (the sub-byte part of the screen shake).
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
  * the horizon line as haze: in the enhanced view the original's two rows of colour 8 become a
    band fading from the sky into a light (by day pale blue) haze and on into the water, reaching
    3 rows up into the sky (present.cpp `Haze`, applied to the colour 8 and sky pixels of those
    rows when the indices become colours); the Original preset keeps the grey line;
  * extended draw distance: the terrain cells 2..5 from the boat's (the game draws 3 x 3), loaded
    with `tile_load` on the scratch memory (with their structures and scenery) when the window's
    centre cell or the time-of-day colours change, drawn behind the game's terrain far to near
    with the objects standing in them (scenery and authored objects; their images built like the
    visible ones, by kind and view);
  * impact debris (`debris.cpp`, launcher "Impact debris", `--debris`): `projectile_impact` and
    `hit_objects` notify the host where a shot lands and what it hits (`host_shot_landed`,
    `host_object_hit`: PORT, no memory change); the debris module makes small particles in world
    coordinates by the stuff hit (sparks off metal, wood chips, blood, stone chips, sandbag sand;
    on the ground the drawn pixel decides: a splash on water, grass on green, dust in the ground's
    colour), and the presenter draws them into the enhanced view (true colour, before the cockpit)
    with the view's projection (`view3d_projection`, interpolated as the view);
  * checks (`scene_enhanced.py`): the view drawn again at 1x equals the original's pixels on
    94.7% (pilot practice) and 97.0% (night gunnery) of the view (the rest: sub-pixel terrain
    edges, the original's bit-pattern sprite scaling), no capture changes the game's memory,
    60 presents/s with the game at 15.0 frames/s; capture 0.3-0.4 ms, drawing ~0.5-1.3 ms per
    present at 710-852 x 400-480 (software renderer).
  * the game folder `Game/` (2026-09-26, as the TD3 port): `gunboat.exe` looks for `Game` next to
    itself, one or two folders up (a build in `gunboat-port/build`), then in the current folder;
    the launcher or `--game-dir` can point elsewhere. The files were copied there from the local
    `Original DOS version/`.
  * the AdLib sound effects (launcher "Sound effects", `--effects adlib`): the effects driver runs
    unchanged; the timer dispatch tells the host when its handler runs, and the host passes its
    speaker changes to a filter (`sfx_adlib.cpp`) that finds the effect from the driver's program
    counter and plays the notes on the effect's FM instrument on a second OPL2 (`sfx_fm.cpp`), on
    the speaker, or not at all. The instruments: `gunboat-port/data/sfx.ini` built in (CMake makes
    it a string; a change reconfigures), under the player's `sfx.ini` next to `gunboat.ini`. The
    editor `gunboat_sfx_editor` (tools/sfx_editor) runs the original driver (the core with the
    tests' stub host) on GB.EXE's programs for each effect's notes, plays them on the speaker or
    the instrument, and saves the player's bank and, when it runs from `gunboat-port/build`, also
    `data/sfx.ini`, so the next build ships the edits. On the AdLib every effect has its own
    channel: `sfx_play` notifies the host (`host_sfx_play`), and each effect runs its own copy of
    the ported driver (one `sfx_timer_tick` per tick on a copy of DS:DA48-DB1D, the game's bytes
    put back), so guns no longer cut the engine or explosions; at most three OPL2 channels per
    effect, as in the editor. Additions where the original is silent (AdLib only; the game
    notifies `host_object_hit` / `host_target_destroyed` from hit_objects, PORT, no memory
    change): the Explosion (8) for targets destroyed by a shot (the original plays it only for
    wreck 4Bh), and three new bank effects 13-15 playing the notes of original programs:
    "Soldier killed" (enemy infantry; effect 9's notes), "Impact: metal" and "Impact: wood" (a
    bullet's hit by the object's material; effect 0's note; not on flesh, not grenades/mortar).
* **Release 1.0.0 (2026-09-26):** a GitHub release with `Gunboat-1.0.0-win64.zip` made by
  `Release.ps1` (no game files: the player adds them to `Game`); tag `v1.0.0`.
* **Release 1.1.0 (2026-09-26):** every video card, the AdLib sound effects (the user's
  instruments built in), the horizon haze; the editor is a development tool and is **not** in the
  release packages (the user's decision); Windows (`Release.ps1`, built here) and Linux (`Release.sh` on GitHub Actions when
  the release is published: Ubuntu 22.04, SDL3 3.4.16 built static; untested by us on a Linux
  desktop, the CI runs `--version` and `--host-test` headless).
* **Release 1.1.1 (2026-09-26):** the AdLib effects on separate channels, the explosion, soldier
  and impact sounds, the user's instruments; Windows and Linux as for 1.1.0.
* **Next:** optionally the parked sound devices; more enhancements only as player options.
* The Codex prototype (an invented patrol mode, `gunboat-port/legacy/`) and the local `archive/`
  were removed on 2026-09-26 when the port replaced them (the prototype is in the git history).
