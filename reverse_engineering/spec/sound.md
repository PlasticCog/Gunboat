# Sound: effects, music, devices (GB.EXE `12ed`, `1af5`, `1ace`, `1b37`, `1b5f`)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Symbols: `spec/sound_symbols.csv`.
Confidence tags as in `simulation.md`. Unlike the platform and video code, Gunboat's sound code
is **not** Test Drive III's: TD3 has its own AdLib driver, Gunboat drives Ad Lib Inc.'s resident
driver and an MT-32.

## 1. Two systems

| | Effects (missions) | Music (title) |
|---|---|---|
| Code | `12ed` | sequencer `1af5:0006`, device back ends `1af5`, `1ace`, `1b37`, `1b5f` |
| Devices | PC speaker, Tandy 3-voice | MT-32, AdLib, Game Blaster (CMS), Tandy, PC speaker |
| Timer | 236.695 Hz, own handler (platform §1) | 89.63 Hz, `121b:0ce2` |
| Data | 13 effect programs in DGROUP (`DS:DB1E`) | `VALK12.MUS`, `VALK3V.MUS`, `VALKPC.MUS` + `ADLIB.BIN` |

## 2. Effects **verified** (Codex: `gunboat-port/src/original_audio.cpp`, ORIGINAL_AUDIO.md)

68,700 timer updates over 92 cases matched the original exactly. API: `sfx_play(id)`
(`12ed:0025`, ids 0..12), the engine note (`engine_sound_update`, `0919:3cc9`, effect 6 looping
with the throttles), mute `DS:007E` (the `S` key), `sfx_install` (`12ed:08c0` → `sfx_speaker_init`
`12ed:0800`, timer 13B1h), `sfx_remove` (`12ed:08f3`). `12ed:0000` / `12ed:0063` switch the engine
sound on / off around screens; `12ed:0018` is the far C wrapper of `sfx_play`. Effect callers:
simulation spec (weapons 1/2/3/8, hits 7/9/0Ah/0Bh, missile 4, sinking 7, menu 12, …). Tandy
(`DS:DA47 == 1`, port C0h) is parked.

## 3. Device detection (`sound_detect`, 1ace:00e8) **verified**

Called with a mask of allowed devices; the first one found wins:

| `DS:F2A0` | Device | Test | Back end (`F294` / `F298` / `F29C`, segment 1af5) | Voices `F292` |
|---|---|---|---|---|
| 8 | Roland MT-32 (MPU-401 UART) | `mpu_reset` `1af5:01be`: FFh to port 331h, FEh acknowledged on 330h | 013F / 0163 / 0125 | 10h |
| 4 | AdLib through `ADLIB.COM` | `adlib_driver_present` `1af5:0290`: "SOUND-DRIVER-AD-LIB" signature at the INT 65h vector | 01E5 / 01FE / 0213 | 9 |
| 2 | Game Blaster (CMS) | `cms_detect` `1b5f:000e`, then `CMS.DRV` loaded | 02CE / 02F0 / 030C | 8 |
| 1 | Tandy 3-voice | ROM byte `'!'` at F000:C000 | 033F / 0394 / 03B8 | 3 |
| 0 | PC speaker | — | same as Tandy | 1 |

The three pointers are the back end's note on, note off and control/program change routines;
`F296`, `F29A`, `F29E` hold their segment (`2AF5` at run time).

## 4. Music **verified** (structure), **likely** (event format)

`music_start` (`00f2:1044`, game_flow §3.1): the file by device, `VALKPC.MUS` (PC speaker, Tandy?),
`VALK12.MUS` (MT-32 and AdLib), `VALK3V.MUS` (Tandy 3-voice); for AdLib, three driver calls set
the instruments from `ADLIB.BIN` (`adlib_call(15h, …)` `1af5:02b9` = INT 65h with SI = function);
the file is loaded into far memory (`1af5:03bd`/`03dc`/`03d0`) and `timer_install` starts the
89.63 Hz timer, whose handler calls `music_tick` (`1af5:0006`) and `speaker_music_tick`
(`1b37:00c0`). `music_stop` (`00f2:119c`) silences the CMS (`1ace:005c`) and restores the timer.

`music_tick` reads the event stream through the far pointer `DS:F5D8`: MIDI-style messages
(`VALK12.MUS` starts `D7 21 00 C0 00 00 C1 00 00 …`: a header, then program changes C0h–C6h, then
timed note events) and dispatches them to the device back end through `F294`/`F298`/`F29C`.

The AdLib path does not program the OPL chip itself: it calls the **Ad Lib Inc. sound driver
V1.51** (`ADLIB.COM`, a TSR on INT 65h, shipped with the game; it must be run before `GB.EXE`).
`ADLIB.BIN` (960 bytes) holds the instrument (timbre) data handed to it.

## 5. PORT decisions

* Effects: the Codex `OriginalAudio` translation, on the host's audio stream (PC speaker square
  wave). Tandy parked.
* Music: **AdLib via an OPL2 emulator** (Nuked-OPL3, already vendored) is the target, as in TD3.
  It needs a translation of the parts of `ADLIB.COM` that the game calls (INT 65h functions:
  initialisation, timbre set, note on/off, volume, etc.), from `ADLIB.COM` itself — a separate
  small reverse-engineering task (13 KB program, strings "Sound driver V1.51 (C) Ad Lib Inc. 1987,
  1989"). The PC speaker music (`VALKPC.MUS`, `1b37`) is the fallback. MT-32, CMS and Tandy
  music are parked.
* Timing: the music tick at 89.63 Hz and the effect tick at 236.695 Hz are run from sample time.

## 6. Open questions

* The exact `.MUS` event format and the loop/end handling in `music_tick` (`1af5:0006`).
* Which INT 65h functions the game uses (all calls go through `1af5:01e5`, `01fe`, `0213`,
  `02b0`, `02b9`) and their `ADLIB.COM` implementation.
* `VALKPC.MUS` versus `VALK3V.MUS` for devices 0 and 1.
