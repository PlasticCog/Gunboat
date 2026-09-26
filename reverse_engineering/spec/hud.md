# Cockpit and screens (hud): panel, instruments, station screens, presentation

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Addresses as in `RE_GUIDE.md`.
Symbols: `spec/hud_symbols.csv`. Confidence tags as in `simulation.md`.

**Ported** (package H, `gunboat-port/src/hud`, tests `tests/difftest/test_hud.py`): every routine of
§2.1, §3, §5 (the three full-screen stations, the gun sprites and panels, `screen_clear`, the window
cracks) and §6 (`view_copy_*`, `gun_frame_draw`), with the library's `gfx_line_to` /
`gfx_fill_rect_clipped`. Package V: `view_present` itself (§6; `gunboat-port/src/mission`, tests
`tests/difftest/test_mission.py`). The pseudocode below was corrected from the verified ports.

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
| 2 | RAM, `DS:D9BA` | the view page outside VGA (`DS:0074 = 2`); in VGA `DS:0074 = 0` and page 2 is the screen segment A000h too |

`DS:007A` is the current draw page for the graphics library (`gfx_set_draw_page`); the page
segment table starts at `DS:D9B6` (indexed by page number, `0919:8a3f`). A frame is:
`game_frame` draws into page 1 (render3d), then `mission_run` calls **`view_present(1, 0)`**
(`05bd:1fd4`; `view_restore` calls `view_present(1, DS:0074)`), which draws the gun station's
sprites into the view on page 1 and copies the parts of page 1 visible through the current
station's cockpit openings to the screen (§6). In chase view the loop copies the view rectangle
directly instead (world §3.2).

## 2. The cockpit model **verified**

The boat record (`DAT10/11.DAT` at `DS:D502`, simulation §2.2) *is* the cockpit model:

| DS | Count | Content |
|---|---|---|
| D502..D51F | 30 | **Indicator bytes** `ind[i]` |
| D520..D530 | 17 | **Panel switch bytes** `sw[n]` |
| D531.. | 2 × 30 | indicator positions: **row/8, column/8** (column 0 = the lamp is not drawn) |
| D56D.. | 30 | indicator graphic numbers (0..3 in both records) |
| D58B.. | 2 × 17 | switch positions (row/8, column/8) |
| D5AD.. | 17 | switch graphic numbers |
| D5BE | | blink timer (FFh at load) |
| D5BF.. | 4 × 2 | (executable data from here) lamp graphic sizes (width, height): all 8 × 8 |
| D5C7.. | 4 × 4 × 2 | lamp pictures on page 1: (x, y) per graphic and state |
| D5E7.. | 4 × 4 × 2 | switch pictures on page 1: (x, y) per graphic and state |
| D607.. | 4 × 2 | switch graphic sizes: 16 × 8, then three 8 × 8 |

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

### 2.1 `indicator_draw` (0919:0027, AL = mode | id, AH = value) **verified** (ported)

```
if AL == FFh: return
scratch_b7e2 = AL; scratch_b7e3 = AH
id = AL & 3Fh; if id >= 1Eh: return
mode 00h: if not locked: b7e3 &= 87h; ind[id] = (ind[id] & 78h) | b7e3  (set)
mode 40h: no change                                                    (redraw)
mode 80h: if ind[id] < 80h: return (nothing drawn); ind[id] ^= 3       (blink step)
mode C0h: ind[id] &= 7Ch                                               (clear state and blink)
if chase view: return
visible bit of ind[id] for the station: 0 or 1 → 10h, 2 → 20h, 3 → 40h, 4 → 08h; above 4: return
g = ind_gfx[id]; (w, h) = D5BF[2g]; frame = state (bits 0–1), or 0 (dark) when the main switch is
   off, or for ids 14h–17h / 18h–1Ah / 1Bh–1Dh when the bow / midship / stern mount (sw 5 / 9 /
   0Eh) is off; (x0, y0) = D5C7[8g + 2·frame]
if column = 0: return
gfx_copy_rect(x0, x0+w−1, y0, y0+h−1, column·8, row·8 + h − 1, page 1, draw page DS:007A)
```

The routine returns AX as it leaves it (AH_in with the last AL on the early returns, 0 after a
copy): the callers pass AX on from one routine to the next (e.g. `boat_hit_component` calls
`panel_redraw_all` right after `switch_draw`), and the panel loops below store the caller's AH into
`scratch_b7e3` on every call, so the port threads AX through (`u16 f(u16 ax)`).

`panel_redraw_all` (`0919:0000`): modes 40h for ids 0..1Dh, AH the caller's. `panel_blink`
(`0919:000e`, every frame from `game_frame`): `--D5BE`; when it reaches 0: `D5BE = 1` and mode 80h
for all ids (after the first 255 frames, every blinking lamp toggles each frame). `switch_draw`
(`0919:0178`) is the same for switch numbers 0..10h (`sw[n]` at `D520 + n`) with the switch tables
(`D58B`, `D5AD`, `D5E7`, `D607`) and two differences: modes 80h **and** C0h both clear the state
and blink bits (switches never blink), and there are no dark pictures; it clears AH once the switch
number is valid. `panel_switches_redraw` (`0919:016a`) redraws them all.

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

* `jet_marker` (`0919:2786`): when the jet angle (`B818 & 7Fh`) changes (`D6B9`), clear rows
  6Fh..72h of the 45h pixels ending at x = 128h / C0h / 58h (look left / ahead / right) and draw
  the 8 × 4 marker bitmap `DS:D6BB` (colour 4, 2 in CGA) at x = that start + (angle >> 1), bottom
  row 72h. (The bottom row is pushed as CX; `gfx_set_colour(0)` has just left CX = 0.)
* `throttle_needles` (`0919:2324`): `B7E9` = 0 (erase), `B7EA` = 0Fh (draw), `gauge_row` = 12 ·
  look. For each engine the needle end point `CS:2173[throttle]` (packed dx/dy) is compared with the
  last one (`B812`/`B814`); on a change the old end goes to `needle_old_end` and, if the gauge centre
  x `D6C1`/`D6C3` + `gauge_row` is not 0 (the gauge is in view), `needle_draw` runs with the pivot
  offset `CS:2171` and centre y 7Dh. The fuel needles do the same with `CS:213D[fuel >> 11]`, pivot
  `CS:213B`, last ends `B80E`/`B810`, centres `D6C5`/`D6C7`, y 75h. Then the **throttle levers**:
  where the column `D6C9` / `D6CB` of the look direction is not 0, the lever picture (24 columns,
  rows 9Dh..C7h resp. 0..2Ah of page 1, from x = 24·(5 − ((throttle − 8) >> 4)) (+ 40h for the
  second), the throttle halved and the step negated in reverse, `B818` bit 7) is copied to that
  column, bottom row A1h, when its x changed (`B81A`/`B81B`). The lever arguments are pushed as
  words whose high byte is the high byte of `DS:007A`.
* `needle_draw` (`0919:2704`, CX = new end): pen to the pivot (`B7E0` + pivot x, `scratch_b7e3` +
  pivot y, 8-bit); unless the old end's dx is FFh, a line to it in the erase colour and the pen back
  to the pivot (the first `gfx_move_to`'s arguments, still on the stack); then a line to the new
  end in the draw colour.
* `radar_scope` (`0919:2810`, from `propulsion`, only when looking right, `F346 == 2`): erase colour
  6; draw colour 6 (invisible) when the main switch or the radar switch (sw 4) is off or the radar
  (`D50D & 3`) destroyed (2), else 0Ch damaged (0, 1), 0Eh intact (3 in CGA). The sweep (`D6B8`,
  128 end points of `CS:2173` from the centre (EEh, 77h) + the pivot offset) advances **4 steps per
  call**, each erasing the last line. Then, unless the scene is being rebuilt (`D8BC`) or the radar
  is off or destroyed, the range is `4000h >> (sw 3 & 3)` and the visible-object list is walked from
  its last entry down (`B83D` − 1 .. 0): an entry with object word ≥ 48h (2 · index, index ≥ 36: an
  authored object) and a kind byte ≠ 0 is plotted, and the first one beyond the range ends the walk
  (the list is in distance order). Quirk kept: with `B83D` = 0 the index is `(FFFFh << 1) >> 1` =
  7FFFh and the walk scans 32768 entries of DGROUP.
* `radar_plot` (`0919:2932`, AH = bearing, CX = distance): a = bearing − 20h (kept in `B7EA`);
  colour by the angle behind the sweep (`scratch_b7e3` − a, 8-bit): below 70h 0Fh, below B0h 13h
  (7 outside VGA), below F0h 7 (8; CGA 1), else the erase colour. Position: the `sine_table`
  (`CS:3502`) words at the byte offsets 4a and −4a (8-bit: for a multiple of 40h both are the
  first word), exchanged when a & 40h; y = (distance · the second) >> 24, negated unless the
  quadrant a & C0h is 40h or 80h; x = (distance · the first) >> 24, negated for quadrants 80h and
  C0h; both `SAR` 2 − (sw 3 & 3) (a range of 3 shifts by 31: 0 or −1); the pixel at (FFh + x,
  87h + y).
* `jet_indicator_draw` (`0919:0f77`, pilot only): the waterjet direction for phase `D632`: two
  pictures of page 1 rows 83h..9Ch, 32 wide from x = `D633[phase]` and 56 wide from `D636[phase]`,
  to the bottom row C7h at x = `D639[look]` (not drawn when 0) and `D63C[look]` (always drawn, 0 when
  looking right).

The pilot's dashboard is a **panorama of five pictures `BD1`..`BD5`**; each look direction shows
three (§5.1). The radar is in `BD5`.

## 4. Message line (`message_line_draw`, 0919:17d4) **verified** (port, differential test)

Once per frame: text colours (4, or 2 in CGA) and the **readouts** of the message line (the message
texts themselves are printed by `show_message` when they change, simulation §9.1). The readout's
cell (column, text row): chase view (11h, 4Eh); bow (11h, 85h), or (22h, 64h) with the second bow
weapon; midship and stern (20h, 84h); other stations above 4 draw nothing (and keep colour 4). The
pilot looking left or ahead gets the mission clock (hours:minutes) at (0Fh / 2, B0h) when the
minutes changed (`DS:D649`), and the heading readout at (22h / 15h / 8, B0h) for left / ahead /
right. The heading readout (`heading_readout`, `0919:16e3`) prints the compass letters and the
degrees (`0919:1558` ':', `174e`, `176e`, `1799`) when the heading word changed (`DS:D647`): the
hull's heading at the pilot's station, the view heading elsewhere. Colours 0Fh after. Exact
pseudocode: simulation §9.1. Messages from the drawing part of the frame use
`show_message_page0` so they land on the visible page.

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
| 5 map | `map_screen` 05bd:19cc | `MAP.LZ` frame (full width) and the region's map sheets `MPnA`/`MPnB` (240 wide at (28h, 5Dh) and (28h, A5h)); per-frame markers by `mission_run` (world §3.2) |
| 6 chase view | `chase_view_screen` 05bd:07da | Black frame, message 25h "Chase boat view.", text `DS:D2DC`, a status panel (`02d2:1598`), `view_restore` |
| 7 damage report | `damage_report_screen` 05bd:1ba4 | Clipboard (`CLIP.LZ`), title `DS:D41E`, labels `DS:D390`; for each (column, row, lamp) triple of the list at `[DS:D392]` (up to 0, 0) the condition word `DS:D394[(ind[id] & 3) + (id < 10h ? 4 : 0)]`; the leak line by `B7FE`: `DS:D3D1` (0), `D3E5` (1), else `D3BE` in red |
| 8 assignment | `assignment_screen` 05bd:1dba | Clipboard, title `DS:D385` "MISSION:", the mission's glyph map `DS:ECB4` (world §6.2), and for mission 1 outside region 3 the practice note `DS:B31E` |

The damage report's title is `DS:D41E` "STATUS" (red); its leak line: `D3D1` "Bilge Pumps -OFF"
(0), `D3E5` "-ON" (1), else `D3BE` "SHIP IS SINKING!" in red; then in red `D3F8` "Mission not
done" when the objective lamp `D505 & 3` is 2, else `D40B` "MISSION COMPLETE". The three
full-screen stations start with `station_screen_colours` (`05bd:2f6a`: `ega_pal_entry` calls in
CGA, Tandy and EGA; nothing in VGA), draw on the view page and end on page 0 with the copy from the
copy page (unless `single_page_mode`), `pal_apply_vga` and `ega_pal_apply`.

`screen_clear` (`05bd:2efc`) does not clear the screen: it resets the instruments' last drawn
state (needle ends `B80E..B814` FFFFh, levers `B81A/B` FFh, jet marker `D6B9` 40h, and `D645`,
`D647`, `D649`, `D64A`, `D965`, `D966`, `D9AD` unless station 0Ah) and blanks the message strip, rows
0..0Bh of the view page. `window_cracks_draw` (`05bd:3114`, far; stations 1..4, not in chase
view) copies one **8 × 7** crack picture (page 1 at x = 100h + 8·picture, rows 21h..27h) per set bit
of `DS:0B49 + station` (simulation §8.6). The cracks are 3-byte records of `crack_table` (`08e1:00DF`,
read by `crack_table_entry` 08e1:016f: column, row, picture): 8 per station from entry 0 (pilot),
18h / 30h (bow, by the bow weapon), 78h / 60h (midship), 48h / 60h (stern); x = 8·column − 8
(± 68h for the pilot's side views; a column off the screen is skipped), bottom row 8·row − 1.

`gun_sprites_capture` (`05bd:1ed0`) reads six 8-pixel-wide 1-bit sprites from the gun art on the
draw page (`gfx_read_bitmap`): `F348`/`F21E` (45 rows at (20h, 38h) / (118h, 38h), colour `EEA0`),
`F375`/`F24B` (30 rows at (20h, 29h) / (118h, 29h), colour `F10A`), `F112`/`EA88` (32 rows at
(60h, 2Fh) / (58h, 2Fh), colour `F146`). `gun_panel_copy(parts)` (`05bd:30ae`, not in chase view):
page 1 x 100h..11Fh, rows 0..20h to x 0, bottom row 2Ch (parts 1), preceded by 120h..13Fh to x
120h (parts 2).

## 6. `view_present` (05bd:1fd4) **verified** (ported)

`view_present(src_page, dst_page)`, a far C function: `mission_run` calls it as `(1, 0)` after each
`game_frame` at the stations 1-4 when not in chase view, `view_restore` as `(1, DS:0074)` (world
§3.2-3.3). The gun stations draw their sprites into the new view on the **source** page (page 1),
then every station copies the view to the **destination** page: a rectangle for the rows below the
sky, then one of the eight fixed view copies (1. below). There is no elevation arithmetic: the
sprites have fixed positions (the gun's elevation shows through the view pitch, simulation §5.4), and
the only varying inputs are the sky top, the gun bearings, the weapon fits, the flash counters and
their latches, and the parity of the world pass counter. AX is left as the last callee leaves it;
both callers ignore it. Stations and look directions other than those below: nothing.

`view_sky_top` (`D965`) is the view's horizon row (render3d §3.1): the rows above it are sky that
the renderer left as it was. `view_sky_top_prev` (`D966`) holds the last present's value, so the rows
copied start at the higher of the two horizons (the rows the sky uncovered or covered since then);
`screen_clear` and the aiming routines set both to 0, so the next present copies the whole view
window from its top row 40h:

```
rows_copy(bottom, dy):                              (inline in each station)
    D966 = min(D965, D966) + 40h                    (8 bits: a sky top from C0h up wraps to a low row)
    if D966 <= bottom: gfx_copy_rect(28h, 127h, D966, bottom, 20h, dy, src, dst)   (to x 20h, bottom row dy)
    D966 = D965
```

**Pilot (1)**, by the look direction `F346`: the same idiom with two rectangles (bottom 77h, both
to the bottom row 4Fh), then the view copy of that direction:

| Look | Rectangles (view x → screen x) | Copy |
|---|---|---|
| 0 left | 78h..F7h → 80h, F8h..127h → 110h | `view_copy_1` |
| 1 ahead | 28h..A7h → 18h, A8h..127h → A8h | `view_copy_2` |
| 2 right | 28h..57h → 0, 58h..D7h → 40h | `view_copy_3` |

**Gun stations (2, 3, 4)**: draw page = src (`DS:007A` and `gfx_set_draw_page`);
`gun_frame_draw(heading[station] − heading[hull] + 20h)` (bow: `− 60h`; `heading` = `B81E` hull,
`B81F` bow, `B820` midship, `B821` stern); the station's sprites (below: 1-bit sprites captured by
the station screens, drawn by `gfx_draw_bitmap` at the pen, rows upward); draw page = dst; the rows
copy; the view copy. Colours: `gun_dark_colour` `ECAE` (VGA 08h), `muzzle_flash_colour` `ECAF`
(0Ch, with white 0Fh), `gun_light_colour` `F106` (14h), `gun_edge_colour` `F107` (15h),
`gun_shade_colour` `F108` (16h), `gun_rail_colour` `F10A` (17h), `midship_mount_colour` `EEA1`.
Flash counters (simulation §6.1): the bow reads `flash_bow` `B83A`/`B83B` directly, the midship
`flash_midship` `B839` for its muzzle flash; the ammunition belt and the stern use the latches that
`mission_run` and `view_restore` take before `game_frame` (`flash_midship_latch` `F132`,
`flash_stern_latch` `F10B`, after moving the old ones to `flash_midship_latch_prev` `ECA8` /
`flash_stern_latch_prev` `EA86`, world §3.3).

```
midship (3):
    sight post at (A0h, 79h): E9E2 16 x 25 in gun_dark_colour, EA14 in gun_rail_colour
    if B839: muzzle_flash()        (at (A0h, 77h): F2A4 16 x 14 in muzzle_flash_colour, F2C0 in white)
    if weapon B807 == 2: ammo_belt(F132, ECA8)
    weapon 0: at (B0h, 7Fh) F3A2 8 x 4 dark, F3A6 8 x 4 edge; at (98h, 7Fh) F5B4 8 x 6 edge, F5AE 8 x 6 dark
    weapon 1: at (B8h, 7Fh) F3A2 8 x 5 in EEA1; at (90h, 7Fh) F5B3 8 x 5 edge, F5AE 8 x 5 light
    else:     at (A0h, 79h) F3A2 16 x 3 shade, F3A8 16 x 3 edge
    D965 = min(D965, 21h); rows_copy(6Ch, 38h)
    weapon 1: view_copy_5, else view_copy_6
stern (4):
    sight post as the midship
    if F10B and weapon B806 == 0: muzzle_flash(); the grenade rack: page 1 x 88h..DDh (F10B == 1)
        or E0h..137h (else), rows 30h..3Ch, to page 0 at (10h, bottom row 77h)
    if weapon == 1: ammo_belt(F10B, EA86); at (A0h, 79h) F3A2 16 x 3 shade, F3A8 16 x 3 edge
    else: in gun_dark_colour F3A2 8 x 1 at (B0h, 79h), F5AE 8 x 1 at (98h, 79h)
    D965 = min(D965, 21h); rows_copy(6Ch, 38h)
    weapon != 0: view_copy_6, else view_copy_8
ammo_belt(latch, prev):            (midship weapon 2, stern weapon 1)
    if latch: muzzle_flash(); frame = 2 if prev and (D70C & 1) else 1
    elif prev: frame = 2
    else: return
    page 1 x C0h..12Fh, rows 9Dh..A8h (frame 1) or A9h..B4h (frame 2), to page 0 at (18h, bottom row 67h)
bow (2), weapon B804 == 0 (two barrels):
    mount edges 8 x 5 in edge colour: EE94 at (40h, 6Bh), ECA9 at (108h, 6Bh)
    the left barrel (counter B83A; flash F5E8 F5FD at x 88h; pieces F0F0 F0F4 at x 60h; barrel
    E9FD E9E2 EA18 at x 78h), then the right one (B83B; EEA6 EEBB at B0h; F5DC F5E0 at E8h;
    F2BF F2A4 F2DA at C0h):
        recoil = 51h if its counter else 0          (the recoil frames are captured 51h bytes further)
        if its counter: at (x, 7Ah) the flash 24 x 7: white, then muzzle_flash_colour
        at (x, 7Ah) the pieces 8 x 4: edge, then shade
        at (x, 7Fh) the barrel 24 x 9, each + recoil: shade (colour still set), edge, dark
    rows copy without the clamp: bottom 6Bh, dy 37h; view_copy_4
bow, weapon != 0:
    at (B0h, 7Fh) F5DC 8 x 3 dark, F5DF 8 x 3 colour 0; at (98h, 7Fh) F0F4 8 x 4 colour 0, F0F0 dark
    at (98h, 7Bh) the barrel cluster 32 x 18: E9E2 dark, EA2A gun_rail_colour
    if B83A: at (A0h, 7Bh) the flash 16 x 10: F5E8 white, F5FC muzzle_flash_colour
    rows copy without the clamp: bottom 6Ch, dy 38h; view_copy_7
```

The ammunition belt and the grenade rack go from page 1 to page 0 whatever pages are passed (in
VGA both callers pass `dst = 0`). The sight post's rows (61h..79h of page 1) are why the midship and
the stern clamp the sky top to 21h: the rows copy then always covers them. The captured sprites
share buffers between the stations (one holds the sight post at the midship and a barrel at the
bow): `hud_symbols.csv` names them by address (`gun_sprite_e9e2`, ...) with each station's
content.

1. **The view copies** `view_copy_*` (`0919:8a32`, `8ad5`, `8b43`, `8bf9`,
   `8cd3`, `8d45`, `8db7`, `8e47`). Each takes **(source page, destination page)**, loads DS and ES
   from `page_segments` (`DS:D9B6`) and, when the low byte of `DS:EED2` is above 0Dh, runs its VGA
   routine (`8a72`, `8b15`, `8b83`, `8c39`, `8d13`, `8d85`, `8df7`, `8e87`; the EGA / Tandy / CGA
   twins at `0919:4xxx`/`5xxx` are parked). The VGA routines are fixed `REP MOVSW` runs: `8a72` 8
   rows from 9678h to 6480h of a left run (60 words, 8 fewer per row) and a right run (20 words, 8
   fewer, while positive) after a gap (destination 20h, source 10h, both growing by 20h per row),
   then 64 source rows of 80 bytes from 5028h, each as 5 pieces of 16 bytes on 5 successive rows
   going up from the row above 26D8h + 320·row, each piece 16 bytes further right; `8b15` 8 rows of
   two runs (60 words, 8
   fewer) from 9628h to 6418h; `8b83` the mirror of `8a72` (runs 20 and 60 words from 9628h to
   6400h, the gap growing by 20h, 18h, then 10h per row; the 64 × 5 pieces from 50D8h to 2158h
   going down); `8c39` a fixed outline at 8728h/4620h (8, 60h, 8, 60h words, 9 rows of 58h, 4 of
   48h, 4 + 30h + 4, 4 rows of 30h); `8d13`..`8e87` share `view_copy_head_vga` (`0919:8e29`, 9 rows
   of 68h words from 8880h to 4778h) and add rows of 18h words and rows of two short runs (5: 4 + 6
   rows of 4 + 4 words 20h apart; 6: 4 + 6 rows of 8 + 8 words 10h apart; 7: 6 + 4; 8: 2 + 2).
   **Tandy** (ported, `hud/views_tandy.cpp`; render3d §11 for the page layout): `5062`, `5104`,
   `514a`, `51f2`, `52e6`, `5344`, `53a2`, `5436` (5..8 share `view_copy_head_tandy` `5400`: 9 rows
   of 34h words from 3100h to 28DCh), the same outlines in half the bytes, each next row
   `+ 2000h − the bytes done, and 7FFFh, + A0h` back in bank 0 (tested on SI; both offsets moved);
   `5062`/`514a` step their 5 pieces up / down a row across the banks. SI is returned as for VGA.
   Test: `test_modes_tandy.test_view_copies_tandy` (the eight dispatchers in modes 9–0Ch on the
   original's mode 9 pages, and each routine by itself).
2. **`gun_frame_draw`** (`05bd:2cde`, argument: the gun's bearing relative to the hull, + 20h or
   − 60h by the station): b = low byte. Within 30h of 40h (v = 2·(30h − (b − 40h))) the left frame
   piece `F112` at (20h + v, 7Fh), and for v > 8 a second one at (18h + v, 5Fh) with a fill
   (18h+v..1Fh+v, 60h..7Fh), for v > 10h also (28h..18h+v, 40h..7Fh), colour `F109`; within 30h
   above D0h (v = 2·(b − D0h) + 2) the right piece `EA88` mirrored (128h − v, 130h − v, fills to
   127h). Then the edges `F348`/`F21E` at (28h, 6Ch) / (120h, 6Ch) in `F107` and the rails
   `F375`/`F24B` at (28h, 5Dh) / (120h, 5Dh) in `F10A`.

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
| 0919:8e29 | view_copy_head_vga |
| 05bd:1ed0 | gun_sprites_capture |
| 05bd:1fd4 | view_present |
| 05bd:2cde | gun_frame_draw |
| 05bd:2f6a | station_screen_colours |
| 05bd:30ae | gun_panel_copy |
| 05bd:3114 | window_cracks_draw |
| 08e1:016f | crack_table_entry (a table lookup, not LZW) |

`CS:0270` panel switch table; `CS:2173` throttle needle end points and radar sweep; `CS:2171` /
`CS:213B` needle pivot offsets; `CS:213D` fuel needle end points; `08e1:00DF` crack table;
`DS:D6C1` gauge centres by look direction (throttle, fuel, lever columns); `DS:D6BB` jet marker
bitmap; `DS:D385..D41E` damage report and assignment texts and list; `DS:D633..D63E` jet indicator
picture columns.

## 8. Open questions

* Switch 7 (`D527`, bow F2 together with sw 6): its role (a second bow mount switch?) and the
  lamp picture numbers.
* Resolved: `view_present` has no placement arithmetic (fixed sprite positions; the recoil frames lie
  51h bytes further in the capture buffers, §6); the eight view copy shapes are transcribed and
  ported.
* The left look direction's instruments (`F346 = 0`): only the radar (right) is identified.
