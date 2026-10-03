GUNBOAT - the faithful C++ / SDL3 port, version @VERSION@
=========================================================

Accolade's Gunboat: River Combat Simulation (DOS, 1990), rebuilt function by function from the
original GB.EXE in C++ and running natively on Windows, macOS and Linux. It is not an emulator. It reads the
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
       +-- gunboat.exe              (Linux: gunboat; macOS: Gunboat.app)
       +-- gunboat_controller.exe   (Linux: gunboat_controller; macOS: Gunboat Controller.app)
                                    the controller mapping tool
       +-- SDL3.dll                 (Windows only)
       +-- README.txt

2. Start the game: on Windows double-click gunboat.exe; on macOS open Gunboat.app; on Linux run
   ./gunboat (or open it from your file manager). The launcher opens: check that it says "Game
   found", choose your settings and press Play. Your choices are remembered (Windows:
   %APPDATA%\Gunboat\gunboat.ini, macOS: ~/Library/Application Support/Gunboat/gunboat.ini, Linux:
   ~/.local/share/Gunboat/gunboat.ini).

Keep the folder somewhere you can write to (on Windows Documents or the Desktop, not Program
Files): the game saves its roster (GBROSTER.DAT) in the Game folder. If Windows says "Windows
protected your PC", click "More info", then "Run anyway" (the program is not signed).
On macOS the apps are not notarized, so macOS refuses to open them at first. Open the Terminal in
the Gunboat folder and run  xattr -dr com.apple.quarantine .  once. (Or open Gunboat.app, then click
"Open Anyway" in System Settings > Privacy & Security; macOS may then run the app from a hidden copy,
so if the launcher says the game is not found, point it at your Game folder.)

Requirements: 64-bit Windows 10 or 11, macOS 11 or later (Apple silicon or Intel), or 64-bit Linux
(x86-64, glibc 2.35 or newer, as in Ubuntu 22.04, Debian 12, Fedora 36 and later) with X11 or Wayland
and PulseAudio, PipeWire or ALSA. The files in Game may have their names in any case (GB.EXE or gb.exe).


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
  Impact debris  On: small debris where the shots hit: sparks off metal, wood chips, blood,
                 stone chips, splashes on the water, grass and dust on land. Hills hide the
                 debris behind them.
  Widescreen     Wide cockpit: in a wide window the cockpit art is widened to the edges where it
                 has the least detail. Extended world: the world continues beside the cockpit.
                 Off: black borders.
  Picture        4:3 as on a VGA monitor, or square pixels.
  Scaling        Sharp pixels, nearest, smooth, or CRT scanlines.
  Music          AdLib music (needs ADLIB.COM in the Game folder; the default when it is
                 there) or the PC speaker.
  Hills stop     A change to the game, off by default: the hills stop your shots, and
  bullets        grenades and mortar shells burst at the hill (in the original a shot hits
                 what it lands on, hills or not).
  Sound effects  AdLib (the default): every effect plays its original notes on an FM
                 instrument, each on its own channel (the guns no longer cut the engine and
                 the explosions); the grenade launcher and the mortar get their own firing
                 sounds (the original reuses the explosion's); and AdLib adds sounds where
                 the original is silent: explosions for destroyed targets, grenades and
                 mortar shells bursting, soldiers killed, and the impacts of your bullets,
                 by what they hit: metal, wood, trees, bridges, stone, sandbags and flesh.
                 Or the PC speaker, as in the original.

In the game: F11 switches between the enhanced and the original picture, F12 saves a screenshot
(in the settings folder's Screenshots: the window as shown, the game's own picture, and the game's
memory at that moment, which helps with bug reports; a note in the corner says it was saved),
Alt+Enter toggles full screen.

gunboat --help (gunboat.exe on Windows, Gunboat.app/Contents/MacOS/gunboat on macOS) lists the
command-line options (for example --no-launcher, --original, --enhanced, --game-dir FOLDER,
--fullscreen, --video ega, --effects adlib).


CONTROLLER
----------

An Xbox controller (or any controller your system knows) plays the game: each button, trigger and
stick direction presses one of the game's keys, so every function of the keyboard can be put on the
controller. The keyboard still works too. The defaults:

  Left stick, D-pad     steer and throttle (pilot), aim (guns), menus
  A                     fire (guns), slow down (pilot), select       (Enter)
  B                     continue on screens and in menus              (Space)
  X                     crew: open fire / cease fire                  (F10)
  Y                     map                                           (M)
  LB / RB               pilot: look left / look right                 (Z / C)
  Right stick click     pilot: look ahead                             (X)
  Right stick           up: bow gun, left: midship gun, down: stern gun, right: chase view
  LT                    time compression                              (+)
  RT (hold)             fast forward                                  (Backspace)
  View / Menu           damage report / pause                         (/ / Esc)
  Left stick click      mission assignment                            (.)

gunboat_controller changes them: it shows the controller with the key each control presses, lights
up what you press, and lets you choose any of the game's functions (the crew's orders F1-F9, the
stations, detail, sound, return to base, ...) for any control. Save writes controller.ini next to
gunboat.ini; the game uses it from its next start. Typing a name needs the keyboard.


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
  Ctrl+Q                quit (anywhere in the port; the original only in a mission)
  Ctrl+H                this list in the game; the game pauses until Ctrl+H again
  Ctrl+S / Ctrl+L       quicksave / quickload: two saves per mission, loading as often as you
                        like (back to the latest save); a new mission gives two saves again

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
* Ctrl+Q quits the game wherever it is pressed (the original: in a mission only), as closing the
  window does. Neither saves anything; the original's Ctrl+Q didn't either.
* Everything else, including the original's quirks, is kept on purpose.


CREDITS AND LICENSES
--------------------

Gunboat is (c) 1990 Accolade. This project is not affiliated with the rights holders, and no game
data is distributed with it.

The host layer, VGA model and EXEPACK loader are adapted from the Test Drive III SDL3 port
(MIT, (c) 2026 Krzysztof Kania). SDL 3 (zlib license). Nuked-OPL3 (LGPL 2.1 or later; built from
the unmodified source in the repository, which also has everything needed to rebuild and relink
the program). The licenses are in the "licenses" folder, THIRD_PARTY.md lists the details.
