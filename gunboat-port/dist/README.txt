GUNBOAT - the faithful C++ / SDL3 port, version @VERSION@
=========================================================

Accolade's Gunboat: River Combat Simulation (DOS, 1990), rebuilt function by function from the
original GB.EXE in C++ and running natively on Windows and Linux. It is not an emulator. It reads the
original game's files at run time; they are NOT included: you need your own copy of the DOS game.

Source code, documentation and issues: https://github.com/PlasticCog/Gunboat


HOW TO PLAY
-----------

1. Copy the files of your original DOS Gunboat (the contents of its disks, or of the folder it was
   installed in) into the folder "Game" that is next to the program. Game/README.txt lists the
   files the port reads (GB.EXE, DATAA.DAT, DATAB.DAT, DATAC.DAT, VALK12.MUS, VALKPC.MUS, and
   optionally ADLIB.COM for the AdLib music).

       Gunboat/
       +-- Game/                    <- your original game files
       +-- gunboat.exe              (Linux: gunboat)
       +-- SDL3.dll                 (Windows only)
       +-- README.txt

2. Start the game: on Windows double-click gunboat.exe; on Linux run ./gunboat (or open it from
   your file manager). The launcher opens: check that it says "Game found", choose your settings
   and press Play. Your choices are remembered (Windows: %APPDATA%\Gunboat\gunboat.ini, Linux:
   ~/.local/share/Gunboat/gunboat.ini).

Keep the folder somewhere you can write to (on Windows Documents or the Desktop, not Program
Files): the game saves its roster (GBROSTER.DAT) in the Game folder. If Windows says "Windows
protected your PC", click "More info", then "Run anyway" (the program is not signed).

Requirements: 64-bit Windows 10 or 11, or 64-bit Linux (x86-64, glibc 2.35 or newer, as in Ubuntu
22.04, Debian 12, Fedora 36 and later) with X11 or Wayland and PulseAudio, PipeWire or ALSA. The
files in Game may have their names in any case (GB.EXE or gb.exe).


THE LAUNCHER AND THE ENHANCEMENTS
---------------------------------

The first setting is the video card the game runs on, as in the original's setup: VGA (256
colours), EGA or Tandy (16 colours), CGA (4 colours) or Hercules (monochrome, 640 x 300). Each
one draws the game exactly as the original did on that card. The enhancements below need VGA;
with the other cards you see the original picture at the scaling you choose.

Every enhancement is optional. The "Original" preset shows the picture exactly as the DOS game
drew it; "Enhanced" turns them all on. The game itself plays the same either way: the
enhancements only change how its frames are shown.

  3D view        High resolution: the world outside drawn again at your screen's resolution from
                 the game's own terrain and objects; the cockpit stays the original art. The
                 enhanced view draws the horizon as a soft haze instead of the original grey line.
  Motion         Smooth: 60 frames per second in the 3D view, drawn between the game's own frames
                 (the view is one game frame behind).
  Draw distance  Extended: the terrain and objects beyond the 3 x 3 cells the game draws, out to
                 5 cells: islands and shores on the horizon.
  Widescreen     Wide cockpit: in a wide window the cockpit art is widened to the edges where it
                 has the least detail. Extended world: the world continues beside the cockpit.
                 Off: black borders.
  Picture        4:3 as on a VGA monitor, or square pixels.
  Scaling        Sharp pixels, nearest, smooth, or CRT scanlines.
  Music          AdLib music (needs ADLIB.COM in the Game folder) or the PC speaker.
  Sound effects  PC speaker, as in the original, or AdLib: every effect plays its original
                 notes on an FM instrument instead.

In the game: F11 switches between the enhanced and the original picture, Alt+Enter toggles full
screen.

gunboat --help (gunboat.exe on Windows) lists the command-line options (for example --no-launcher,
--original, --enhanced, --game-dir FOLDER, --fullscreen, --video ega, --effects adlib).


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
  prompts. The configuration questions are answered by the launcher (the video card).
* All five graphics cards of the original are there (VGA, EGA, Tandy, CGA, Hercules). Music on the
  AdLib (through the original ADLIB.COM driver, translated to C++, with the Nuked-OPL3 chip
  emulator) or the PC speaker; the MT-32 and Game Blaster devices and the Tandy sound chip are
  not ported. The AdLib sound effects are an addition of this port (optional).
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
