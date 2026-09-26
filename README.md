# Gunboat — faithful C++ / SDL3 source port

A reimplementation of Accolade's **Gunboat: River Combat Simulation** (DOS, 1990), rebuilt
function by function from the original `GB.EXE` in C++ on SDL3. It reads the original game files
at run time. **No game data is included**; you need your own copy of the DOS game.

The method follows the finished Test Drive III port by Krzysztof Kania
([test-drive-3-sdl3](https://github.com/kylofon/test-drive-3-sdl3)), a sibling Accolade game
from the same year: map the executable, write one specification per subsystem, then port each
original function and check it against the original machine code.

## How to play (Windows)

You need the files of the original DOS *Gunboat* (`GB.EXE`, `DATAA.DAT`, `DATAB.DAT`, `DATAC.DAT`,
the `.MUS` music and, for AdLib music, `ADLIB.COM`). They are not included.

1. Open the [latest release](https://github.com/PlasticCog/Gunboat/releases/latest) and download
   `Gunboat-…-win64.zip`.
2. Unzip it somewhere you can write to (Documents or the Desktop, not Program Files).
3. Copy your original game files into its `Game` folder:

   ```text
   Gunboat\
   ├── Game\          <- your original game files (GB.EXE, DATAA.DAT, ...)
   ├── gunboat.exe
   ├── SDL3.dll
   └── README.txt     <- the options and the controls
   ```

4. Double-click `gunboat.exe`, check that the launcher says "Game found", choose **Original** or
   **Enhanced** (or each option) and press **Play**. If Windows says "Windows protected your PC",
   click **More info**, then **Run anyway** (the program is not signed).

## Status

| Phase | State |
| --- | --- |
| Executable map | Done: 597 functions indexed, 504 named (`reverse_engineering/RE_GUIDE.md`) |
| Subsystem specs | All eight done: simulation, 3D renderer, world, game flow, cockpit/HUD, platform, video, sound (`reverse_engineering/spec/`) |
| Port core | Done: GB.EXE loaded into the original memory layout, the SDL3 host in C++, generated symbols, and differential tests that run the original code in Unicorn and compare all memory with the C++ (`gunboat-port/PORTING.md`) |
| Porting | Done for VGA: the whole game runs natively and matches the original (433 functions of GB.EXE and the Ad Lib driver's 52, each verified; `gunboat-port/Run Port.cmd`) |
| Enhancements | Optional, chosen in the launcher: a high-resolution 3D view, smooth 60 fps motion, an extended draw distance, a wide cockpit (or the world beside the picture) in widescreen, picture aspect and scaling filters. "Original" shows the faithful picture; F11 switches in the game. The game itself is the same either way |

## Layout

| Path | What |
| --- | --- |
| `Game/` | **Your original game files** (not in the repository: `Game/README.md` lists them) |
| `CLAUDE.md` | Porting rules and conventions |
| `reverse_engineering/` | Executable map, symbols, specs, format notes and Python tools |
| `gunboat-port/` | C++ / SDL3 code (CMake) and differential tests against the original code |

## Building from source (Windows)

1. Copy the files of your original DOS *Gunboat* into the folder `Game` (`Game/README.md` lists
   the ones the port reads). They are never committed: Git ignores that folder.
2. MSYS2 UCRT64 (GCC, CMake, Ninja) and SDL3: `powershell -File gunboat-port/Build.ps1` builds and
   tests everything.
3. `gunboat-port/Run Port.cmd` (or `gunboat-port/build/gunboat.exe`) starts the game; it finds
   `Game` by itself. For a copy elsewhere, put `gunboat.exe` and `SDL3.dll` next to a `Game`
   folder, as in the Test Drive III port:

   ```text
   Gunboat\
   ├── Game\          <- your original game files (GB.EXE, DATAA.DAT, ...)
   ├── gunboat.exe
   └── SDL3.dll
   ```

The launcher opens first: choose the game folder, **Original** or **Enhanced** (or each enhancement
on its own), the picture and the sound, then Play. The choices are saved. In the game, F11
switches between the enhanced and the original picture and Alt+Enter toggles full screen.
`gunboat.exe --help` lists the command-line options (`--no-launcher`, `--original`, `--enhanced`,
...).

## Credits

Tools and platform layer adapted from test-drive-3-sdl3 (MIT, © 2026 Krzysztof Kania).
OPL emulation: Nuked-OPL3 (LGPL-2.1). Gunboat is © 1990 Accolade; this project is not
affiliated with the rights holders.
