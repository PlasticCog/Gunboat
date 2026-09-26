# Gunboat — faithful C++ / SDL3 source port

A reimplementation of Accolade's **Gunboat: River Combat Simulation** (DOS, 1990), rebuilt
function by function from the original `GB.EXE` in C++ on SDL3. It reads the original game files
at run time. **No game data is included**; you need your own copy of the DOS game.

The method follows the finished Test Drive III port by Krzysztof Kania
([test-drive-3-sdl3](https://github.com/kylofon/test-drive-3-sdl3)), a sibling Accolade game
from the same year: map the executable, write one specification per subsystem, then port each
original function and check it against the original machine code.

## Status

| Phase | State |
| --- | --- |
| Executable map | Done: 597 functions indexed, 504 named (`reverse_engineering/RE_GUIDE.md`) |
| Subsystem specs | All eight done: simulation, 3D renderer, world, game flow, cockpit/HUD, platform, video, sound (`reverse_engineering/spec/`) |
| Port core | Done: GB.EXE loaded into the original memory layout, the SDL3 host in C++, generated symbols, and differential tests that run the original code in Unicorn and compare all memory with the C++ (`gunboat-port/PORTING.md`) |
| Porting | Done for VGA: the whole game runs natively and matches the original (433 functions of GB.EXE and the Ad Lib driver's 52, each verified; `gunboat-port/Run Port.cmd`) |
| Enhancements | Optional, chosen in the launcher: a high-resolution 3D view, smooth 60 fps motion, widescreen, picture aspect and scaling filters. "Original" shows the faithful picture; F11 switches in the game. The game itself is the same either way |

## Layout

| Path | What |
| --- | --- |
| `CLAUDE.md` | Porting rules and conventions |
| `reverse_engineering/` | Executable map, symbols, specs, format notes and Python tools |
| `gunboat-port/` | C++ / SDL3 code (CMake) and differential tests against the original code |

## Building and playing (Windows)

MSYS2 UCRT64 (GCC, CMake, Ninja) and SDL3: `powershell -File gunboat-port/Build.ps1` builds
and tests everything. `gunboat-port/Run Port.cmd` plays the game from `Original DOS version/` next to
`gunboat-port/`, or run `gunboat-port/build/gunboat.exe --game-dir <folder with GB.EXE>`.

The launcher opens first: choose the game folder, **Original** or **Enhanced** (or each enhancement
on its own), the picture and the sound, then Play. The choices are saved. In the game, F11
switches between the enhanced and the original picture and Alt+Enter toggles full screen.
`gunboat.exe --help` lists the command-line options (`--no-launcher`, `--original`, `--enhanced`,
...).

## Credits

Tools and platform layer adapted from test-drive-3-sdl3 (MIT, © 2026 Krzysztof Kania).
OPL emulation: Nuked-OPL3 (LGPL-2.1). Gunboat is © 1990 Accolade; this project is not
affiliated with the rights holders.
