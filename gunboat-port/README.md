# Gunboat — native C++ / SDL3 port foundation

A playable first native build using the SDL platform layer from the supplied
**Test Drive III SDL3 port**. It opens the original Gunboat DOS files directly.
There is no DOSBox, x86 interpreter, browser, Python, or extracted PNG/JSON asset
dependency in the game executable.

**This is an asset-faithful port foundation with a replacement patrol mode.**
It is not yet a complete behavioral translation of Gunboat. Original campaigns,
enemy AI, weapons, crew systems, sound, and boat physics still need porting.

## Play

Double-click **[Run Gunboat.cmd](Run%20Gunboat.cmd)**, or **Play Native Gunboat.cmd**
in the project root. A Windows x64 build is already in `build/gunboat.exe`, beside
`SDL3.dll`. It needs these files in the project's `Original DOS version` folder:

- `GB.EXE`
- `DATAA.DAT`
- `DATAB.DAT`
- `DATAC.DAT`

Press **Enter** to start. Destroy the three numbered contacts, return to the green
base marker on the map, and stop. Contacts return fire within range. Continuous
fire heats the guns; release Space to let them cool. Red numbered markers show
visible contacts. `BRG` is the compass bearing to the next objective; `HDG` is the
boat's current heading. Match them to turn toward the objective, using the chart
to follow the river around land.

| Control | Action |
| --- | --- |
| W / S or Up / Down | Increase / decrease throttle; low throttle reverses |
| A / D or Left / Right | Steer |
| Space | Fire twin guns |
| Q / E | Aim left / right independently of steering |
| C | Center aim |
| X | Cut throttle and brake |
| M | Tactical chart; the simulation continues |
| F5 | Original cockpit / chase camera |
| F1–F4 | Load world 1–4 and show its briefing |
| Esc | Pause / resume |
| Enter | Start / resume |
| R | Restart current patrol |
| Alt+Enter | Fullscreen |
| F10 | Quit |

An SDL gamepad can also steer with the left stick, adjust throttle with its
vertical axis, fire with A, and stop with B. Keyboard controls were tested;
physical gamepad operation has not been tested.

For an invulnerable practice patrol:

```powershell
.\gunboat-port\build\gunboat.exe --practice --world 3
```

The native renderer currently uses the original **320×200 indexed-color frame**,
scaled to a resizable 4:3 window. Chase view is a new convenience. A higher
resolution renderer can be added independently of these asset loaders.

## What came from Test Drive III

The supplied repository's game is C; its launcher is C++. This Gunboat game is
C++17, linked to selected C platform modules:

- SDL3 window, presentation, fullscreen, keyboard, gamepad and timing services.
- Indexed VGA framebuffer and palette presentation.
- PC speaker / OPL host audio infrastructure. The current patrol uses only a
  simple new speaker firing sound; Gunboat's music and sound drivers are unported.
- Its data-only EXEPACK unpacking method and LZW dictionary semantics, adapted
  into bounded C++ loaders after verifying the shared formats.

The TD3 racing simulation, track loaders, and executable memory layout are not
compatible with Gunboat. This build supplies Gunboat-specific asset loaders and
a new renderer/simulation around that reusable platform.

See [THIRD_PARTY.md](THIRD_PARTY.md) for exact provenance and local modifications.

## Accuracy boundary

| Area | Current implementation |
| --- | --- |
| Original executable | EXEPACK data decompression and relocation in C++; 172,304 output bytes match the research dump exactly |
| Archives | Original two-key filename hash and 84-record `DATAC.DAT` directory; bounds-checked reads |
| Terrain | Original four 17×11 grids, tile rotations, vertex arrays, triangle commands and daytime color rules |
| Object placements | Original map entries plus repeated tile scenery, with original X/Y, type, and low flag bits |
| Sprites | Direct native translation of the original directional / composite sprite routines; 1,056 tested type-and-view combinations match original DOS routine execution pixel for pixel |
| Cockpit | Original `BF1.LZ` / `BF2.LZ` artwork, decoded at startup |
| Perspective / size | New software depth-buffer renderer; height factor, sprite world scale, camera and facing convention remain provisional |
| Cockpit composition | New opening mask and replacement status instruments; original instrument update / clipping routines are unported |
| Navigation | New collision grid derived from displayed land triangles, with a bridge clearance heuristic; original collision records are not yet used |
| Starting position | Preserves the source coordinate for audit; moves the playable boat to nearby navigable water (under 0.5 display units in these four worlds) |
| Patrol / damage / weapons | New three-contact objective, throttle, gun heat, assisted hitscan, return fire and hull damage |
| Original game systems | Campaign/HQ, mission scripts, moving AI, stations, grenade ballistics, crew/equipment, saving, music, night rendering and original timing remain unported |

The sprite decoder uses eight cached viewing directions at original cache scale
47 (the original routine selects 46 for types 58/59). Exact sampled sprite pixels
do **not** imply pixel-identical full scenes or faithful original gameplay.

## Build

This workspace was built and tested with Windows x64, MSYS2 UCRT64 GCC 15.2,
CMake, Ninja and SDL3 3.4.16:

```powershell
powershell -File .\gunboat-port\Build.ps1
```

The script discovers this machine's `C:\msys64\ucrt64\bin` toolchain. The SDL SDK
is already under `deps/SDL3-3.4.16`. With another compiler/platform, supply SDL3
through its CMake package and use:

```text
cmake -S gunboat-port -B gunboat-port/build -DCMAKE_BUILD_TYPE=Release
cmake --build gunboat-port/build
ctest --test-dir gunboat-port/build --output-on-failure
```

Only Windows was compiled and run in this iteration. The source uses portable
SDL/C++ APIs, but other platforms are unverified. GCC runtime libraries are
linked statically; the Windows game needs no MSYS2 runtime DLLs beside it.

## Verification

```powershell
.\gunboat-port\build\gunboat.exe --check
.\gunboat-port\build\gunboat.exe --self-test
python .\gunboat-port\tests\verify_assets.py
python .\gunboat-port\tests\smoke_sdl.py
```

- The native checks load all four worlds and exercise reachable routes,
  movement, collision, combat, victory, defeat, and practice mode.
- The asset comparison checks the compiled executable's output against the
  earlier extraction and the original DOS sprite routine, executed by Unicorn
  **only in the development test**. It needs the existing research fixtures,
  Python, Pillow and Unicorn. Result: **0 mismatches** across 172,304 executable
  bytes, six LZW images, 10,036 object records, 52,138 triangles, and 1,056 sprite
  views. The source worlds contain no line commands, though the loader supports
  them.
- SDL smoke tests run the actual game with dummy video/audio, scripted keys,
  saved states, and screenshots. They cover all four worlds, throttle, steering,
  firing, chase view, world switching, chart and pause.
- Route checks sample paths continuously. The combat test places the boat at
  verified firing approaches; it is not a complete human playthrough of each
  mission.

Diagnostic options: `--seconds N`, `--state FILE.json`, `--screenshot FILE.bmp`,
`--dump-assets DIR`, `--game-dir DIR`, `--scale 1..6`, `--fullscreen`.
The inherited host also accepts `GB_KEYS` and `GB_SNAPSHOT_DIR` for automation;
see `tests/smoke_sdl.py` for examples.

## Next iteration

1. Translate Gunboat's player movement and the original tile collision records,
   then replace the provisional navigation code with replay comparisons.
2. Recover the world projection, vertical scale, object orientation and distance
   tables to compare whole scenes against fixed DOS camera captures.
3. Translate mission initialization and object update dispatch; bring back moving
   patrols and the original objective/friendly/enemy classifications.
4. Port weapons, stations, crew/damage, sound and the campaign frontend in small,
   independently verified steps.

`src/assets.cpp` contains recovered data/routine translations. Replacement game
rules live separately in `src/simulation.cpp`; presentation lives in `src/game.cpp`.
The existing Three.js viewer, reverse-engineering files and supplied TD3 source
remain available for comparison.
