# Reverse-engineering guide: GB.EXE

Conventions and the executable map for the faithful C++/SDL3 port. Read this first; porting rules
are in `/CLAUDE.md`. The method follows the Test Drive III port (`/test-drive-3-sdl3`,
`td3port/PORTING.md`, `port/RE_GUIDE.md`), a sibling Accolade game from 1990 with the same archive
format, LZW decompressor, graphics library and Microsoft C 5.1 runtime.

## Files

| Path | What |
| --- | --- |
| `out/GB_unp.exe` | EXEPACK-unpacked MZ with its 1,584 relocations (`tools/unexepack.py`). Image offsets exclude the MZ header. 172,304 image bytes, identical to the C++ loader's output and to `out/gunboat_unpacked_image.bin` before relocation. |
| `map/gb_functions.json` / `.csv` | Capstone function index (`tools/gbindex.py`): extent, near/far, callers/callees, DS reads/writes, strings, INTs, ports, jump tables |
| `map/gb_starts.txt` | Function start image offsets for the Ghidra `DecompileAll` script |
| `map/gb_td3_matches.csv` | Functions whose code matches a TD3 function (`tools/gbmatch.py`) |
| `symbols.csv` | **The** name list: functions and globals (`tools/symbols.py` validates it) |
| `out/decomp/gb_ds.c` | Ghidra decompilation of every indexed function, names applied, DGROUP globals renamed `DS_xxxx` |
| `out/decomp/gb_globals_xref.txt` | For each DS global: the functions that use it |
| `tools/x86dis.py out/GB_unp.exe dis SSSS:OOOO LEN` | Ground-truth disassembly when the decompiler looks wrong |
| `tools/adlib_dis.py [--fn OOOO ...] [--data]` | Disassembly of the user's `ADLIB.COM` (the Ad Lib driver GB.EXE calls through INT 65h; spec `spec/adlib_driver.md`) |
| `/gunboat-port/tests/difftest/` | Differential tests: original x86 in Unicorn against the port's C++, all memory compared (`gunboat-port/PORTING.md`) |
| `ORIGINAL_WORLD_FORMAT.md`, `OBJECT_FORMAT.md` | Decoded world, tile and object formats |

Everything under `out/` is generated and git-ignored (much of it is derived from the original
game data). `map/` and `symbols.csv` are tracked.

## Addresses

* Microsoft C 5.1, medium model: 47 code segments (from the relocation table), far calls between
  them (`call far` = `9A`, `retf`), near calls inside a segment.
* Write code addresses as **`SSSS:OOOO`**, with the segment as stored in the file:
  `0919:8930`. Image offset = SSSS×16 + OOOO (`0x11AC0`). The Codex notes use image offsets.
* Ghidra and the Unicorn oracle load the image at segment `1000`: the same function is
  `FUN_1919_8930` in raw Ghidra output (the post-processed `gb_ds.c` rewrites names back to
  file segments) and runs at physical `0x10000 + image` in Unicorn.
* **DGROUP** is segment `0x1B73` (image `0x1B730`, runtime `0x2B73`). `DS:xxxx` = image
  `0x1B730 + xxxx`. C code always runs with DS = DGROUP; assembly routines that load DS
  themselves are flagged `sets_ds` in the index.
* Entry `15ee:0016` (`_astart`), which calls `main` at `0000:0000`. Initial stack `1ad7:08c8`.
* Segments overlap: the same code can be reached as two `SSSS:OOOO` pairs. `0919:0000` is
  `08e1:0380` (image 9190h); Ghidra's output may use either label. The index and `symbols.csv`
  use the segment the indexer assigns (`gbindex.py`).
* Segment `0919` crosses linear `0x20000` (Ghidra `1919`, at offset `6E70`). Ghidra wraps near
  branch targets there wrongly; `tools/ghidra/FixNearFlows.java` repairs them.

## Segment map (first pass)

597 functions; 86% of the 112,432 code bytes are reached by recursive descent (most of the rest
is data inside code segments). "TD3" counts the functions whose code matches a Test Drive III
function.

| Segment(s) | Size | Funcs | TD3 | Contents |
| --- | ---: | ---: | ---: | --- |
| `0000` | 3.8 K | 20 | 8 | `main`, archive open (DATAA/B/C.DAT), memory, fatal messages, `GUNBOAT.CFG` setup (graphics mode, joystick), disk prompts, `random`, filename hash |
| `00f2` | 4.4 K | 17 | 6 | Title sequence (`ACCO.LZ`, `TITLE*.LZ`, `FOOTIT*.LZ`), copy-protection screen (`COPY.LZ`), text printing, `wait_key` |
| `020d` | 3.1 K | 8 | 1 | HQ and roster: `HQ*.LZ`, `QUIZCOLR.BIN`, `PENCIL.MPP`, `GBROSTER.DAT` read/write |
| `02d2` | 11.7 K | 30 | 0 | Front end: briefing and outfitting (`GEN*.LZ`, `FOLDER.LZ`, `SPEC1..6.LZ`, `INSIG.LZ`, `ADHEAD.LZ`, `DAT5.DAT`) |
| `05bd` | 12.6 K | 18 | 0 | Mission and world loading: `DAT10/11.DAT`, `DAT1A/B..`, `TILE.BIN`, cockpit art `BD*.LZ`, `CLIP.LZ` |
| `08e1` | 0.9 K | 12 | 6 | LZW decompressor (same code as TD3) |
| `0919` | 36 K | 256 | 13 | **The engine**: key handlers, stations, controls, boat physics, weapons, projectiles, objects/AI, mission clock, 3D projection, terrain and sprite rendering, cockpit; `game_frame` at `0919:8930` |
| `121b` | 3.3 K | 25 | 11 | Platform: DOS file wrappers, text colours, keyboard ISR (INT 9, port 60h), PIT timer install/restore |
| `12ed` | 2.3 K | 28 | 0 | PC speaker / Tandy (port C0h) sound-effect driver, 13 effects (`ORIGINAL_AUDIO.md`) |
| `137e`–`15ea` | 9.8 K | 35 | 23 | Graphics library, one module per primitive, dispatching on the video mode (as TD3) |
| `15ee` | 8 K | 70 | 51 | Microsoft C 5.1 runtime (`_astart`, stdio, malloc, string) |
| `17f0` | 11.5 K | 39 | 0 | Text output to the screen (character printer with row/column state at DS:E84F..E85B), palette tables; the 8.8 KB after `17f0:0b94` is unreached (tables or pointer-called code) |
| `1ace` | 0.6 K | 3 | 0 | `CMS.DRV` (Game Blaster) loader |
| `1af5` | 1 K | 26 | 0 | AdLib music: `adlib.bin`, calls the resident `ADLIB.COM` driver through INT 65h |
| `1b37`, `1b5f` | 1 K | 10 | 0 | Speaker/Tandy and port-I/O helpers (ports 42h/43h/61h/C0h/C1h; DX-indexed) |

## Shared with Test Drive III

`map/gb_td3_matches.csv`: 119 functions match TD3 code, 110 of them named there. Shared: the
Microsoft C runtime; the whole graphics library (`gfx_*`); the LZW decompressor; DOS file
wrappers; the keyboard ISR and timer install; `file_load_near/far`, `random`, `mem_alloc_all`,
the archive filename hash; text printing (`print_records`, `wait_key`); and 11 sprite-scaling
and projection routines in `0919`. A TD3 spec in `test-drive-3-sdl3/port/spec/` that covers a
matched function is a strong head start, but **confirm every low-score match** (below 0.8, noted
in `symbols.csv`) against the Gunboat disassembly. Callers, globals and constants differ.

## Known addresses inside functions

These Codex-era addresses are not function entries; they are recorded in the notes of the
function that contains them:

| Address | Inside | Role |
| --- | --- | --- |
| `9B62` | `0919:0953` +7F | Pilot control mask handling |
| `976B..97D8` | `0919:03cb` +1A0 | Engine switch / startup countdown |
| `B451` | `0919:2273` +4E | Propulsion: both engines, two acceleration steps, rotation |
| `CB8C` | `0919:3977` +85 | Object damage and destroyed-variant selection |
| `1036D` | `0919:7158` +85 | Speed reversal on shore contact |
| `10755` | `0919:7523` +A2 | Projection: shore contact candidates D901/D903 |
| `113FC` | `0919:8229` +43 | Attached camera coordinates |
| `11826` | `0919:8665` +31 | Daytime colour substitution |
| `11B6F`, `11BAD` | `game_frame` +AF, +ED | Simulation iteration loop; outer loop |
| `137B7` | `12ed:08c0` +27 | PIT reload 13B1h |

## Code reached through pointers

`tools/gbindex.py` seeds these by hand:

* **Key handlers.** `key_dispatch` (`0919:038e`) reads the key code at `DS:EE9C`. Codes 01h–7Fh
  call through the code-segment table `0919:0290` (127 entries; `0919:03c7` is the default
  "nothing" handler). Codes 81h–8Ah call through `DS:D615` (10 entries, odd-aligned), and only
  while the station number `DS:0086` is below 5. Same design as TD3 (`0e12:0000`, `DS:B6EF`).
* **Per-video-mode row drawers.** `0919:3fc6..4036` store a pair of near code pointers in
  `DS:D8F8`/`D8FA` by mode: `788e`/`7943` (VGA mode 13h, 320-byte rows), `4dfa`/`4eb6` (EGA
  planar, 40-byte rows), `5693`/`5756` (Tandy 16-colour: four 2000h banks, 160-byte rows),
  `46e8`/`47cf` (CGA: two 2000h banks, 80-byte rows). Only the VGA pair matters for the port.

## Still to resolve

* Ghidra decompiles 590 of the 597 functions. It fails on `02d2:1306` (front end: read the
  assembly) and `15ee:0de0` (runtime, timeout); a few indexed starts fall inside functions
  Ghidra built itself and are not emitted separately (`_cinit` is folded into `_astart`).
* Unreached code: `0919:26b2` (82 bytes) and the 8.8 KB after `17f0:0b94` (partly tables).
  `0919:0feb..1528` starts with a table of near offsets (`105b`, `105c`, `1071`, ...) into the
  same block: records or strings, to identify in the specs.
* **Data in code segments**: sine/aim tables (e.g. `0919:3502`, the 129-word projectile sine
  table), masks, mode tables. Name them `cglobal` in `symbols.csv`.
* The `engine` owner of `0919` has to be split by call tree into `simulation`, `render3d` and
  `hud`, starting from `game_frame`'s callees.

## Subsystem specs

Status: **all eight specs done** (2026-09-25): simulation, render3d, world, game_flow, hud,
platform, video, sound. 409 of 597 functions named; the unnamed rest are mostly the
EGA/Tandy/CGA twins of drawing routines, BIOS text helpers (`17f0`), AdLib/CMS back-end helpers
and C runtime internals. Open questions are listed at the end of each spec.

As TD3: one spec per subsystem in `reverse_engineering/spec/<owner>.md`, each with an overview
and call graph, a function table, a globals table, pseudocode, file formats, the DOS/hardware
dependencies with their SDL3 replacement, timing, and open questions. Names found in a spec go
into `spec/<owner>_symbols.csv` (`kind,address,name,type,notes`) and are merged into
`symbols.csv` by `tools/merge_symbols.py` (spec names win; the old name is kept in the notes).

Helpers for reading code: `tools/fn.py NAME|SSSS:OOOO` (index facts, Ghidra C, disassembly with
names; `-r SSSS:OOOO LEN` for any range), `tools/calltree.py NAME [depth]`,
`tools/gbfile.py NAME` (extract an archived file).

| Spec | Scope |
| --- | --- |
| `platform` | `121b`, `08e1`, `1b37`/`1b5f`, `17f0` text output; timer, keyboard, joystick, config, files, memory, LZW |
| `video` | Graphics library `137e`–`15ea` (mode 13h path only), page flipping, palette |
| `game_flow` | `0000`, `00f2`, `020d`, `02d2`: `main` state machine, title, copy protection (dropped), HQ, roster, briefing, outfitting, debrief |
| `world` | `05bd`: mission and world loading, DAT files, tile and object setup |
| `simulation` | `0919` update tree: controls, stations, physics, weapons, projectiles, damage, crew, AI, mission clock |
| `render3d` | `0919` drawing tree: camera, projection, terrain, sprites, sky, effects |
| `hud` | Cockpit instruments, station views, messages, tactical map |
| `sound` | `12ed` effects, `1af5` AdLib music (`VALK*.MUS`), `1ace` CMS (identify only) |
| `adlib_driver` | Not GB.EXE: the resident `ADLIB.COM` V1.51 (INT 65h functions 0, 13h–15h, its INT 8 handler, its installation) |

## Regenerating

From the repository root, with Python 3 + Capstone, and JDK 21 or later for Ghidra:

```text
python reverse_engineering/tools/unexepack.py "Original DOS version/GB.EXE" reverse_engineering/out/GB_unp.exe
python reverse_engineering/tools/gbindex.py reverse_engineering/out/GB_unp.exe reverse_engineering/map/gb [gaps]
python reverse_engineering/tools/unexepack.py <TD3 game>/TDIII.EXE reverse_engineering/out/td3/TDIII_unp.exe
python reverse_engineering/tools/gbmatch.py reverse_engineering/out/td3/TDIII_unp.exe \
    test-drive-3-sdl3/port/tdiii_functions.json test-drive-3-sdl3/port/symbols.csv \
    reverse_engineering/map/gb_td3_matches.csv
python reverse_engineering/tools/merge_symbols.py
python reverse_engineering/tools/symbols.py
python reverse_engineering/tools/gen_symbols.py
powershell -File reverse_engineering/tools/decompile.ps1
```

`decompile.ps1` runs Ghidra headless (`_tools/ghidra_12.0.4_PUBLIC`, git-ignored) with
`SetDS` → `FixNearFlows` → `ApplySymbols` → `DecompileAll`, then `postprocess.py`. Rerun it
whenever `symbols.csv` changes.
