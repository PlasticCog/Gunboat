# Gunboat: faithful C++/SDL3 source port

Goal: a behaviour-exact port of Accolade's **Gunboat** (DOS, 1990; `GB.EXE`), rewritten function by
function in C++ on SDL3. It reads the original game files at run time and never redistributes them.
The method is the one that produced the finished Test Drive III port (`test-drive-3-sdl3/`, same
engine family): **reverse-engineer → document → port each original function → verify it against the
original machine code.** Enhancements (resolution, widescreen, 60 fps) come later, in a separate
layer, and never change the faithful core.

## Ground rules

* **The shipped game is C++ and SDL3 only.** No Python, Java, JavaScript or emulator at run time.
  Any tool is fine for research and tests: Python, Capstone, Unicorn, Ghidra (JDK), DOSBox.
* **No invented gameplay.** Every rule must come from the original code. If something is unknown,
  find it in the disassembly; don't approximate it.
* The original game data is copyrighted: `Original DOS version/` and everything derived from it
  (`reverse_engineering/out/`, `gunboat-port/out/`) stay out of git.
* **GitHub** (`origin` = github.com/PlasticCog/Gunboat, public): push only what the remake needs
  (port code, tests, specs, symbols, the RE tools and format docs). No game files, generated
  output, tool installs, editor settings or archived material.

## Layout

| Path | What |
| --- | --- |
| `gunboat-port/` | The C++ port (CMake + SDL3). See "Current state" below. |
| `reverse_engineering/RE_GUIDE.md` | **Read first**: addresses, segment map, files, how to regenerate |
| `reverse_engineering/symbols.csv` | The single name list for functions and globals |
| `reverse_engineering/map/` | Function index and TD3 matches (generated, tracked) |
| `reverse_engineering/spec/` | One spec per subsystem (next phase) |
| `reverse_engineering/tools/` | Unpacker, indexer, matcher, disassembler, Ghidra scripts, format tools |
| `reverse_engineering/out/` | Generated: unpacked EXE, Ghidra project, decompilation `decomp/gb_ds.c` (ignored) |
| `Original DOS version/` | The original game (ignored). `SteelThunder/` is the predecessor game, for reference. |
| `test-drive-3-sdl3/` | Reference port (MIT, separate git repo, ignored): specs, tools, `td3port/` skeleton |
| `archive/` | Local only (git-ignored): abandoned prototypes, old research notes and tools. Don't build on them. |
| `_tools/` | Ghidra install (ignored) |

## Porting rules (adapted from `test-drive-3-sdl3/td3port/PORTING.md`)

* **Memory model.** Original state lives in one `mem[]` array at its original real-mode addresses:
  the image at segment `0x1000`, DGROUP at `0x2B73`, VGA page 0 at `A000:0000`. Every global,
  table and heap block is read and written there through typed accessors, named from
  `symbols.csv` (`ds_u16(DS_mission_clock)`), or as a raw offset with a comment. **Never keep game
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
  speaker, AdLib) go through the host layer, which is the only code that includes SDL.
  Every busy-wait loop of the original calls the host pump once per iteration.
* Copy protection: take the "passed" path (`// PORT:`). Only VGA mode 13h is ported first;
  EGA/Tandy/CGA paths and the CMS driver are parked.

## Verification (required for every ported function)

1. **Differential test**: run the original function in Unicorn and the C++ function on the same
   randomized memory snapshot, then compare the whole DGROUP (64 KB), the affected heap and VGA
   memory, and the return registers. See `gunboat-port/tests/verify_original_physics.py` for the
   pattern. Current tests compare hand-picked bytes; new tests must compare all memory.
2. **Scene checks**: compare frames against DOSBox captures of the original
   (`reverse_engineering/out/dosbox_captures`) and headless snapshots (`SDL_VIDEO_DRIVER=dummy`).
3. Report results honestly, with numbers. A test that was skipped or narrowed must be stated.

## Commands (Windows; PowerShell 5.1 or Git Bash)

```text
# toolchain: MSYS2 UCRT64 (GCC 15, CMake, Ninja) at C:\msys64\ucrt64\bin; Python 3.13 with capstone, unicorn, pillow
powershell -File gunboat-port/Build.ps1                        # configure, build, ctest
gunboat-port/build/gunboat.exe --game-dir "Original DOS version" --check
python gunboat-port/tests/verify_assets.py                     # asset loaders vs original routines
python gunboat-port/tests/verify_original_physics.py           # (and the other verify_*.py)
python reverse_engineering/tools/symbols.py                    # validate symbols.csv after editing it
powershell -File reverse_engineering/tools/decompile.ps1       # Ghidra headless decompile, ~minutes
python reverse_engineering/tools/x86dis.py reverse_engineering/out/GB_unp.exe dis 0919:8930 0x40
```

Regenerating the map from scratch: `reverse_engineering/RE_GUIDE.md`, "Regenerating".

## Current state (2026-09-25)

* **Phase 1, executable map: done.** 597 functions indexed (86% of code bytes); 144 named from
  TD3 matches and Codex notes; the Ghidra decompilation of all of them is regenerated by
  `decompile.ps1`.
* **Phase 2, subsystem specs**, one at a time (the user's choice): `spec/simulation.md` and
  `spec/render3d.md`, `spec/world.md`, `spec/game_flow.md` done (2026-09-25; 335 functions
  named). Next: hud (cockpit, station screens, map, damage report), then platform, video, sound. Then phase 3, a new `mem[]`-based skeleton in `gunboat-port/`, reusing TD3's host/VGA
  layer (convert it to C++).
* `gunboat-port/src/game.cpp` + `simulation.cpp` are Codex's **invented** patrol mode (float
  physics, hitscan, gun heat). They're a playable reference only and will be replaced.
* `gunboat-port/src/original_*.cpp` and `assets.cpp` hold **verified** translations (physics,
  weapons, PC-speaker effects, sprites, terrain). Re-express them in the `mem[]` model, keeping
  their tests. `gunboat-port/ORIGINAL_*.md` document them.
