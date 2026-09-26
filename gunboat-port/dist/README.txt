GUNBOAT - the faithful C++ / SDL3 port, version @VERSION@
=========================================================

Accolade's Gunboat: River Combat Simulation (DOS, 1990), rebuilt function by function from the
original GB.EXE in C++ and running natively on Windows. It is not an emulator. It reads the
original game's files at run time; they are NOT included: you need your own copy of the DOS game.

Source code, documentation and issues: https://github.com/PlasticCog/Gunboat


HOW TO PLAY
-----------

1. Copy the files of your original DOS Gunboat (the contents of its disks, or of the folder it was
   installed in) into the folder "Game" that is next to gunboat.exe. Game\README.txt lists the
   files the port reads (GB.EXE, DATAA.DAT, DATAB.DAT, DATAC.DAT, VALK12.MUS, VALKPC.MUS, and
   optionally ADLIB.COM for the AdLib music).

       Gunboat\
       +-- Game\          <- your original game files
       +-- gunboat.exe
       +-- SDL3.dll
       +-- README.txt

2. Double-click gunboat.exe. The launcher opens: check that it says "Game found", choose your
   settings and press Play. Your choices are remembered (%APPDATA%\Gunboat\gunboat.ini).

Keep the folder somewhere you can write to (Documents or the Desktop, not Program Files): the game
saves its roster (GBROSTER.DAT) in the Game folder. If Windows says "Windows protected your PC",
click "More info", then "Run anyway" (the program is not signed).

Requirements: 64-bit Windows 10 or 11.


THE LAUNCHER AND THE ENHANCEMENTS
---------------------------------

Every enhancement is optional. The "Original" preset shows the picture exactly as the DOS game
drew it; "Enhanced" turns them all on. The game itself plays the same either way: the
enhancements only change how its frames are shown.

  3D view        High resolution: the world outside drawn again at your screen's resolution from
                 the game's own terrain and objects; the cockpit stays the original art.
  Motion         Smooth: 60 frames per second in the 3D view, drawn between the game's own frames
                 (the view is one game frame behind).
  Draw distance  Extended: the terrain and objects beyond the 3 x 3 cells the game draws, out to
                 5 cells: islands and shores on the horizon.
  Widescreen     Wide cockpit: in a wide window the cockpit art is widened to the edges where it
                 has the least detail. Extended world: the world continues beside the cockpit.
                 Off: black borders.
  Picture        4:3 as on a VGA monitor, or square pixels.
  Scaling        Sharp pixels, nearest, smooth, or CRT scanlines.
  Sound          AdLib music (needs ADLIB.COM in the Game folder) or the PC speaker.

In the game: F11 switches between the enhanced and the original picture, Alt+Enter toggles full
screen.

gunboat.exe --help lists the command-line options (for example --no-launcher, --original,
--enhanced, --game-dir FOLDER, --fullscreen).


CONTROLS (from the original)
----------------------------

  Arrow keys / keypad   pilot: steer and throttle; gun stations: aim; chase view: move the camera
  Enter                 gun stations: fire; pilot: slow down
  Z / X / C             pilot: look left / ahead / right
  V / N / B             bow, midship, stern gun
  ,                     chase view
  M                     map        /  damage report        .  mission assignment
  + / -                 time compression / control rate    Backspace (held): fast forward
  F1 - F10              panel switches and crew orders (F9 identify target, F10 open/cease fire)
  D                     detail level           S  sound on/off
  Tab                   return to base         Esc  pause
  Ctrl+Q                quit

The original manual describes the missions and the boat in full.


WHAT IS DIFFERENT FROM THE ORIGINAL
-----------------------------------

* The copy protection is removed (the game runs as after a correct answer) and there are no disk
  prompts. The configuration questions are answered for VGA.
* VGA graphics only (the EGA, CGA, Tandy and Hercules modes are not ported). Music on the AdLib
  (through the original ADLIB.COM driver, translated to C++, with the Nuked-OPL3 chip emulator)
  or the PC speaker; the MT-32 and Game Blaster devices are not ported.
* The 3D stations run at 15 frames per second, so that the mission clock runs in real time (the
  original ran as fast as the PC could draw).
* Everything else, including the original's quirks, is kept on purpose.


CREDITS AND LICENSES
--------------------

Gunboat is (c) 1990 Accolade. This project is not affiliated with the rights holders, and no game
data is distributed with it.

The host layer, VGA model and EXEPACK loader are adapted from the Test Drive III SDL3 port
(MIT, (c) 2026 Krzysztof Kania). SDL 3 (zlib license). Nuked-OPL3 (LGPL 2.1 or later; built from
the unmodified source in the repository, which also has everything needed to rebuild and relink
the program). The licenses are in the "licenses" folder, THIRD_PARTY.md lists the details.
