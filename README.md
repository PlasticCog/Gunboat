# Gunboat — faithful C++ / SDL3 source port (work in progress)

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
| Executable map | Done: 597 functions indexed, 239 named (`reverse_engineering/RE_GUIDE.md`) |
| Subsystem specs | Simulation done (`reverse_engineering/spec/simulation.md`); render3d next |
| Faithful port | Not started. Verified translations of the boat physics, weapons, PC-speaker effects, sprites and terrain loading already exist in `gunboat-port/src/original_*.cpp` and `assets.cpp`. |
| Playable build | `gunboat-port/` contains an earlier *non-faithful* patrol mode on the original assets, kept as a reference until the port replaces it |

## Layout

| Path | What |
| --- | --- |
| `CLAUDE.md` | Porting rules and conventions |
| `reverse_engineering/` | Executable map, symbols, specs, format notes and Python tools |
| `gunboat-port/` | C++ / SDL3 code (CMake) and differential tests against the original code |

## Building the current build (Windows)

MSYS2 UCRT64 (GCC, CMake, Ninja) and SDL3: `powershell -File gunboat-port/Build.ps1`, then run
`gunboat-port/build/gunboat.exe --game-dir <folder with GB.EXE and DATAA/B/C.DAT>`.

## Credits

Tools and platform layer adapted from test-drive-3-sdl3 (MIT, © 2026 Krzysztof Kania).
OPL emulation: Nuked-OPL3 (LGPL-2.1). Gunboat is © 1990 Accolade; this project is not
affiliated with the rights holders.
