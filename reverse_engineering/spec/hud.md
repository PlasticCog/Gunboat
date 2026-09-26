# Cockpit and screens (hud): panel, instruments, station screens, presentation

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Addresses as in `RE_GUIDE.md`.
Symbols: `spec/hud_symbols.csv`. Confidence tags as in `simulation.md`.

This spec gives the structure, the data model and the rules. The screen coordinates, rectangle
sizes and bitmap offsets are constants in the listed routines; the port transcribes them
routine by routine (`tools/fn.py NAME -k` lists every drawing call with its constant
arguments) and checks each screen against DOSBox captures. Only the VGA (mode 13h) paths are
specified; each drawing helper below has CGA/EGA/Tandy twins that are parked.

## 1. Pages and presentation **verified**

| Page | Where | Use |
|---|---|---|
| 0 | A000:0000, `DS:D9B6` | visible screen |
| 1 | RAM, `DS:D9B8` | drawing page: the 3D view (render3d §1.1) and the decoded cockpit art |
| 2 | RAM, `DS:D9BA` | only outside VGA (`DS:0074 = 2`); in VGA `DS:0074 = 0` |

`DS:007A` is the current draw page for the graphics library (`gfx_set_draw_page`); the page
segment table starts at `DS:D9B6` (indexed by page number, `0919:8a3f`). A frame is:
`game_frame` draws into page 1 (render3d), then `mission_run` calls **`view_present`**
(`05bd:1fd4`, page `DS:0074`) which copies the parts of page 1 visible through the current
station's cockpit openings to the screen and adds the station overlays (§6). In chase view the
loop copies the view rectangle directly instead (world §3.2).

## 2. The cockpit model **verified**

The boat record (`DAT10/11.DAT` at `DS:D502`, simulation §2.2) *is* the cockpit model:

| DS | Count | Content |
|---|---|---|
| D502..D51F | 30 | **Indicator bytes** `ind[i]` |
| D520..D530 | 17 | **Panel switch bytes** `sw[n]` |
| D531.. | 2 × 30 | indicator positions (x/8, y/8) |
| D56D.. | 30 | indicator graphic numbers |
| … D5BE | | further layout bytes; `D5BE` = blink timer (FFh at load) |
| D5BF.. | | graphic source rectangles (in the executable's data) |

Indicator and switch byte:

| Bits | Meaning |
|---|---|
| 0–1 | state (0–3): selects the lamp / switch picture. Components: 3 intact, 1 damaged, 2 destroyed. Switches: bit 0 set = **off** |
| 2 | locked (state changes are ignored) |
| 3 | visible from the stern station (4) |
| 4 | visible from the pilot station (1) |
| 5 | visible from the bow station (2) |
| 6 | visible from the midship station (3) |
| 7 | blinking |

Indicator numbers are the byte offsets from `D502`, so each component's condition byte *is* its
lamp: 1/2 engine indicators, 4/5 fuel tanks, 6/7 engines, 8/9 waterjets, 0Ah middle
spotlight, 0Bh radar, 0Ch front spotlight, 0Dh rear spotlight, 0Eh hull, 10h–13h crew
(seaman, gunner's mate, captain, engineman), 14h–17h bow mount lamps, 18h–1Ah midship, 1Bh–1Dh
stern.

### 2.1 `indicator_draw` (0919:0027, AL = mode | id, AH = value) **verified**

```
id = AL & 3Fh (ignored if >= 1Eh or AL == FFh)
mode 00h: if not locked: ind[id] = (ind[id] & 78h) | (AH & 87h)        (set)
mode 40h: no change                                                    (redraw)
mode 80h: if ind[id] >= 80h: ind[id] ^= 3                              (blink step)
mode C0h: ind[id] &= 7Ch                                               (clear state and blink)
if chase view: return
if the indicator is not visible from the current station (bits 3–6): return
picture = graphic ind_gfx[id] (D56D), frame = state (bits 0–1);
   frame 0 (dark) instead when the main switch is off, or for ids 14h–17h / 18h–1Ah / 1Bh–1Dh
   when the bow / midship / stern mount (sw 5 / 9 / 0Eh) is off
copy the frame from the panel art (page 1) to (x·8, y·8) on the current draw page
```

`panel_redraw_all` (`0919:0000`): modes 40h for ids 0..1Dh. `panel_blink` (`0919:000e`, every
frame from `game_frame`): `--D5BE`; when it reaches 0: `D5BE = 1` and mode 80h for all ids (after
the first 255 frames, every blinking lamp toggles each frame). `switch_draw` (`0919:0178`) is the
same for switch numbers 0..10h (`sw[n]` at `D520 + n`, modes as above); `panel_switches_redraw`
(`0919:016a`) redraws them all.

### 2.2 Panel switches and F-keys **verified**

`panel_switch_toggle` (`0919:03cb`, BX = entry) uses the table `CS:0270` (switch number,
indicator to redraw or FFh) and flips bit 0 of the switch (simulation §3.3):

| Station | F1 | F2 | F3 |
|---|---|---|---|
| Pilot | sw 0 **main switch** (lamp 0); off also stops the engines | sw 1 / 2 **engines** (lamps 1 / 2), through `engine_switch` | sw 4 **radar** |
| Bow | sw 5 mount (lamp 17h) | sw 7 then sw 6 mount (lamps 15h, 14h) | sw 8 **bow spotlight** (lamp 16h) |
| Midship | sw 9 mount (lamp 18h) | sw 0Ah mount (lamp 19h) | sw 0Dh **midship spotlight** (lamp 1Ah) |
| Stern | sw 0Eh mount (lamp 1Bh) | sw 0Fh mount (lamp 1Ch) | sw 10h **stern spotlight** (lamp 1Dh) |

A gun fires only when **both** of its mount switches are on (bow `D525`/`D526`, midship
`D529`/`D52A`, stern `D52E`/`D52F`; simulation §6.1). Other switches: sw 3 (`D523`, cycled by the
unreachable `0919:0591`), 0Bh time-compression lamp (`+`), 0Ch control rate (`-`).

## 3. Pilot station instruments (per frame) **verified**

Drawn by `game_frame` after the boat passes (simulation §1.2) and by `propulsion`:

* `jet_marker` (`0919:2786`): when the jet angle (`B818 & 7Fh`) changes (`D6B9`), erase and redraw
  a 4 × 4 marker (`DS:D6BB`) on row 72h at a column from the angle.
* `throttle_needles` (`0919:2324`): for each engine, the needle end point for the throttle
  (`CS:2173[throttle]`, packed x/y) is compared with the last one (`B812`/`B814`); on a change the
  old needle is erased and the new one drawn as a line (`needle_draw` `0919:2704`, colour 7Dh) from
  the gauge centre for the current look direction (`DS:D6C1 + 12·F346`).
* `radar_scope` (`0919:2810`, from `propulsion`, only when looking right, `F346 == 2`): colour 6
  (off), or 0Ch / 0Eh when the main switch and radar switch (sw 4) are on and the radar
  (`D50D`) is damaged / intact. It draws the sweep (`D6B8`) and one blip per visible object from
  the visible-object list (distance `4C96`, bearing `4E00`; render3d §5.2) with `radar_plot`
  (`0919:2932`).
* `jet_indicator_draw` (`0919:0f77`): the waterjet direction lamp, phase `D632` (simulation §4.1).

The pilot's dashboard is a **panorama of five pictures `BD1`..`BD5`**; each look direction shows
three (§5.1). The radar is in `BD5`.

## 4. Message line (`message_line_draw`, 0919:17d4) **verified** (structure)

Once per frame: text colours (4, or 2 in CGA) and the message line's cell position by station and
fitted bow weapon (pilot and gun stations have their own row/column, e.g. row 11h), then the
current message text (`show_message`, simulation §9.1) is printed through `0919:1558` (one
character, `121b:03d8`), `16e3`, `174e`, `176e`, `1799`. Messages from the drawing part of the frame
use `show_message_page0` so they land on the visible page.

## 5. Station screens (`05bd`, drawn when the station changes; world §3.2) **verified** (calls)

Each draws on page `DS:0074`, decodes its pictures (`08e1:01bd` into `DS:1094`, VGA draw
`121b:08a8`, others `1390:0000`), copies the result to the copy page, and most end with
`view_restore` (one `game_frame`, world §3.3).

| Station | Routine | Content |
|---|---|---|
| 1 pilot | `pilot_screen` 05bd:048c | Dashboard `BD1`+`BD2`+`BD3` (looking left, `F346 = 0`), `BD2`+`BD3`+`BD4` (ahead), `BD3`+`BD4`+`BD5` (right); `jet_indicator_draw`; message 29h "Main cabin." |
| 2 bow | `bow_screen` 05bd:08be | Bow weapon art (`BG1/2` or `BF1/2`); `gun_panel_copy` (`05bd:30ae`); `gun_sprites_capture` (`05bd:1ed0`) cuts the sight and barrel sprites out of the art into DGROUP buffers (`E9E2`, `E9FD`, `EA18`, `EA33`, `EA4E`, `EA69`, `EA88`, `ECA9`, `EE94`, `F0F0`, `F0F4`, `F112`, `F21E`, `F24B`, `F348`, `F375`, `F5E0`…); message 2Ah "Bow gun." |
| 3 midship | `midship_screen` 05bd:10f4 | Midship weapon art (`BM`, `BR` or `B6`); same scheme; message 28h "Midship gun." |
| 4 stern | `stern_screen` 05bd:0e9e | Stern weapon art (`B6` or `BGR`); same scheme; message 2Bh "Rear gun." |
| 5 map | `map_screen` 05bd:19cc | `MAP.LZ` frame and the region's map sheets `MPnA`/`MPnB`; per-frame markers by `mission_run` (world §3.2) |
| 6 chase view | `chase_view_screen` 05bd:07da | Black frame, message 25h "Chase boat view.", text `DS:D2DC`, a status panel (`02d2:1598`), `view_restore` |
| 7 damage report | `damage_report_screen` 05bd:1ba4 | Clipboard (`CLIP.LZ`), title `DS:D41E`, labels `DS:D390`; for each (x, y, id) triple of the list at `[DS:D392]` the condition word `DS:D394[(ind[id] & 3) + (id < 10h ? 4 : 0)]`; the leak line by `B7FE`: `DS:D3D1` (0), `D3E5` (1), else `D3BE` in red |
| 8 assignment | `assignment_screen` 05bd:1dba | Clipboard, title `DS:D385`, the mission's glyph map `DS:ECB4` (world §6.2), text `DS:B31E` |

`screen_clear` (`05bd:2efc`) blanks the screen before each; `window_cracks_draw` (`05bd:3114`)
copies one 21h × 7 crack picture per set bit of `DS:0B49 + station` (simulation §8.6).

## 6. `view_present` (05bd:1fd4) **verified** (structure)

Per station, after each `game_frame`:

1. Copy the view from page 1 to the screen through the cockpit openings: rectangles with
   `gfx_copy_rect` and the special copies `view_copy_*` (`0919:8a32`, `8ad5`, `8b43`, `8bf9`,
   `8cd3`, `8d45`, `8db7`, `8e47`; each selects its VGA variant `8a72`, `8b15`, `8b83`, `8c39`,
   `8d13`, `8d85`, `8df7`, `8e87` and takes the source and destination page numbers). The VGA
   `8a72`, for example, copies a trapezoid of rows narrowing by 8 words per row (the window
   opening), then a strip of the view centre repeated 5 times per row (a magnified sight).
2. Draw the station overlays from the captured sprites: gun sight and barrel pictures
   (`gfx_draw_bitmap` of the `E9E2`…`F5FD` buffers) at positions from the gun elevation and the
   muzzle flash counters (`F10B`, `F132`, `EA86`, `ECA8`: this frame's and last frame's stern and
   midship flashes, copied by `mission_run`), the gun colour `F107`/`F109`/`F10A`, and the
   recoil frames; the sight animation `D965`/`D966`.
3. `gun_frame_draw` (`05bd:2cde`): the side pieces of the gun frame (`F112`, `EA88`) and fills.

## 7. Symbols and tables

| Address | Name |
|---|---|
| 0919:0000 | panel_redraw_all |
| 0919:000e | panel_blink |
| 0919:0027 | indicator_draw |
| 0919:016a | panel_switches_redraw |
| 0919:0178 | switch_draw |
| 0919:03cb | panel_switch_toggle |
| 0919:0f77 | jet_indicator_draw |
| 0919:17d4 | message_line_draw |
| 0919:2324 | throttle_needles |
| 0919:2704 | needle_draw |
| 0919:2786 | jet_marker |
| 0919:2810 | radar_scope |
| 0919:2932 | radar_plot |
| 0919:8a32 … 8e47 | view_copy_1 … view_copy_8 (VGA 8a72 … 8e87) |
| 05bd:1ed0 | gun_sprites_capture |
| 05bd:1fd4 | view_present |
| 05bd:2cde | gun_frame_draw |
| 05bd:30ae | gun_panel_copy |
| 05bd:3114 | window_cracks_draw |

`CS:0270` panel switch table; `CS:2173` throttle needle end points; `DS:D6C1` gauge centres by look
direction; `DS:D6BB` jet marker bitmap; `DS:D390..D41E` damage report texts and list.

## 8. Open questions

* Switch 7 (`D527`, bow F2 together with sw 6): its role (a second bow mount switch?) and the
  lamp picture numbers.
* The exact placement arithmetic in `view_present` for each station (elevation → sight row,
  recoil frames) and the eight view copy shapes: transcribe and compare with DOSBox captures.
* The left look direction's instruments (`F346 = 0`): only the radar (right) is identified.
