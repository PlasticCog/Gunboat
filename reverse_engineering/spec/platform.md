# Platform: timers, input, files, memory, LZW, text (GB.EXE `121b`, `08e1`, `0000`, `1469`–`15ea` helpers, `17f0`)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Symbols: `spec/platform_symbols.csv`.
Confidence tags as in `simulation.md`.

About half of this code is **the same code as Test Drive III's** (`map/gb_td3_matches.csv`); for
those routines `test-drive-3-sdl3/port/spec/platform.md` (TD3 below) is the specification and
this document only records Gunboat's addresses and differences. In the port everything here
becomes the host layer (the only code that includes SDL), as in TD3 (`td3port/PORTING.md`).

## 1. Timers **verified**

Two different timer interrupt handlers are used, never at the same time:

| | Menu / music timer | Mission timer |
|---|---|---|
| Installed by | `timer_install` `121b:0c9a` (INT 8, PIT divisor 3400h) | `sfx_install` `12ed:08c0` (vector `DS:DA8F`, PIT divisor 13B1h) |
| Rate | 89.63 Hz | 236.695 Hz |
| Handler | `121b:0ce2` | `12ed:00a3` → `12ed:00b1` |
| Work per interrupt | `DS:08C0++`; `DA44` counts 0..4; `music_tick` (`1af5:0006`); `speaker_music_tick` (`1b37:00c0`) | `DS:08C0++` on 4 of every 13 interrupts (**72.83 Hz**); `sfx_timer_tick` (`12ed:00eb`) |
| Chains to the BIOS | every 5th interrupt (17.93 Hz) | every 13th (18.21 Hz) |

`timer_restore` (`121b:0cc4`, TD3) and `sfx_remove` (`12ed:08f3`) put back 18.2 Hz. `music_start`
(game_flow §3.1) installs the menu timer; the mission uses the sfx timer (sound spec).
`DS:08C0` is the game's tick counter (simulation §1.1, the waits of the full-screen stations and
of `wait_key`/`bios_wait_ticks`).

**PORT:** the host calls the active handler's body at its exact rate from sample time (as the
Codex `OriginalAudio` renderer already does for the sfx timer), and every busy-wait loop of the
original pumps the host (TD3 PORTING.md "Timing").

## 2. Keyboard **verified** (TD3)

`kbd_install` (`121b:0a4d`), `kbd_restore` (`121b:0a88`), `kbd_isr` (`121b:0a9c`): TD3 platform
§4.4 with Gunboat's tables `121b:093E` (E0 keys), `121b:0952` / `09C2` (unshifted / shifted) and
the held-key bitmask `DS:DA43` (up 1, down 2, left 4, right 8, Enter 10h; simulation §3.1). Key
code `DS:DA42`, last scancode `DS:DA41`, E0/E1 state `DS:DA3F`, typematic filter `DS:DA40`,
Backspace `DS:DA39`, Ctrl `DS:DA3A`. **PORT:** SDL key events become the XT byte stream fed to the
translated ISR (TD3 `host_set_kbd_handler`).

## 3. Joystick **verified**

| Routine | Role |
|---|---|
| `joystick_axis` `146a:000b` | Counts one axis on port 201h (the one-shot at `out 201h`, `in` until the bit falls, counter step 8, overflow = FFFFh) |
| `joystick_read` `1473:0008` | Direction from both axes against the calibration limits `DS:DD17..` → a keypad-style code and bits; button via `15d7:000d` |
| `joystick_calibrate` `146e:000e` | Called by `config_load` when the joystick is enabled |
| `15ea:0003` | Axis helper |

Used only when `DS:F394` is set (game_flow §2). **PORT:** an SDL gamepad or joystick produces the
same codes and `B7F0` bits (simulation §3.1).

## 4. Files and archive **verified**

DOS wrappers (TD3): `dos_open_read` `121b:0524`, `dos_seek` `121b:050a`, `dos_file_size`
`121b:0536`, `dos_read` `121b:055c`, `dos_close` `121b:0575`. The archive (`archive_open`,
`name_hash`, `file_load_near` / `file_load_far`) is described in game_flow §1 and
ORIGINAL_WORLD_FORMAT.md; the C runtime's `fopen`/`fread`/`fwrite` open `GBROSTER.DAT` and
`GUNBOAT.CFG`. **PORT:** case-insensitive lookup in the game folder; the roster is written there.

## 5. Memory: far buffers (`mem_alloc_all`, 0000:0a4e) **verified**

`_fmalloc` blocks (paragraph pointers in DGROUP), fatal error 1 when one fails:

| Pointer | Size | Main use |
|---|---|---|
| F0DA:F0DC | F410h | sprite cache A (segment `D883`, render3d §5.6) |
| F0E0:F0E2 | D010h | sprite cache B (segment `D885`) |
| F280:F282 | 3A34h | `TILE.BIN`; HQ pictures; `F63C` = +10000 in the front end |
| F5BA:F5BC | 244Ah | pictures (LIGHTS, MAP, title) |
| F5BE:F5C0 | 21F2h | bow art 2 |
| F5C2:F5C4 | 2382h | midship art 2 |
| F5C6:F5C8 | 251Ch | bow art 1; `F5CC` = +0B54h (+125Ch in the front end), `F63C` = +170Ch in a mission |
| F5D0:F5D2 | 1AEAh | stern art 2 |
| F5D4:F5D6 | 1A7Ch | world B data copy |
| F5E4:F5E6 | 2AD0h | CLIP; `F612` = +1838h, `F616` = +0C1Ch |
| F61A:F61C | 1310h | map sheet B |
| F622:F624 | 0D0Ch | BD4 |
| F0FC:F0FE | 1784h | BD1; `F61E` = +0AD2h (BD5) |
| F102:F104 | 0CC6h | BD2 |

(The remaining allocations continue in the same routine; `mem_free_all` `0000:0c7a` frees them.)
**PORT:** these become regions of the emulated real-mode memory at fixed segments, so that the
far pointers stored in DGROUP keep their original values (TD3 `mem.h` model).

## 6. LZW pictures **verified** (TD3)

`lzw_alloc` / `lzw_free` / `lzw_decode_body` / `lzw_fill_input` / `lzw_get_code` /
`lzw_add_entry` (`08e1`) are TD3's LZW decoder (TD3 platform §4.5). `lzw_decode_picture`
(`08e1:01bd`, far source, DS destination): decodes a `.LZ` entry into `DS:1094` (the terrain
arrays' memory, used as a scratch buffer). Then `1390:0000` (all modes) or `121b:08a8` (VGA) draws
it: the RLE picture format of TD3 (`pic_draw`, bottom-to-top rows). `08e1:016f` decodes the crack
picture for the cockpit (`window_cracks_draw`). Verified by Codex against the original
(`gunboat-port/legacy/tests/verify_assets.py`: six pictures).

## 7. Text **verified** (structure)

| Routine | Role |
|---|---|
| `text_set_colours` `121b:0397` (TD3) | foreground/background colours for the game font |
| `text_goto_cell` `121b:03b0` | text cursor at (column, row) in character cells |
| `121b:03c7` | cursor / colour helper used by the message line |
| `text_draw_char` `121b:03d8` | draws one character of the game font into the current page |
| `121b:0581` | glyph drawing (557 bytes, writes video memory; likely) |
| `print_records` / `print_text` / `print_chars` (`00f2`) | TD3 game_flow §4.6 |
| `17f0` segment | BIOS text output for the configuration questions and fatal messages (the string printer `17f0:0002` handles CR/LF with the cursor in `DS:E84F..E85B`); INT 10h helpers |

Glyphs 13h–17h are used by the assignment map (world §6.2).

## 8. Small helpers

| Address | Name | Role |
|---|---|---|
| 0000:0780 | random | 32-bit LCG (simulation §11.1) |
| 121b:036e | world_a_base | returns 6E54h |
| 121b:0372 | pit_random | `in al, 42h` (PIT channel 2 counter) + argument: a hardware-timing random value (quiz only) |
| 121b:0380 | far_normalize | TD3 |
| 1469:0008 | gfx_saved_mode | returns `DS:DCFC`, the video mode the graphics library saved at start-up (restored on exit) |
| 15d4:0007 | bios_wait_ticks | TD3 (INT 1Ah ticks) |
| 121b:0902 | cga_composite_setup | the special mode 0Ch (parked) |

## 9. PORT summary

| Original | Port |
|---|---|
| INT 8 handlers, PIT | host tick at 89.63 / 236.695 Hz; `DS:08C0` advanced as the handler does |
| INT 9, ports 60h/61h | SDL keys → XT bytes → translated ISR |
| Port 201h | SDL gamepad |
| INT 21h files, archive | host file access (game folder) |
| `_fmalloc` | fixed regions in the emulated memory |
| INT 10h text (setup, fatal) | host message box / console |
| `pit_random` | only the skipped quiz uses it |
