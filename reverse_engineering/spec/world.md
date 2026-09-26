# World: missions, world data and the mission loop (GB.EXE `05bd`, `0919:3d78`)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Addresses as in `RE_GUIDE.md`.
Symbols: `spec/world_symbols.csv`. Confidence tags as in `simulation.md`. This spec covers how a
mission is loaded and set up, the world data files, and the structure of the mission loop. The
per-station screens (cockpit art, sights, map, damage report, assignment) are in the hud spec;
the front end that chooses region, mission and outfit is in the game_flow spec.

## 1. Mission lifecycle

```
main (0000:0000) → front end (game_flow) chooses DS:B503 region, B505 mission, B509 mission type,
                   F110 practice mode, B804/B806/B807 weapons, B805 engines, B803 practice targets,
                   station DS:0086 (1, 2 or 4)
  mission_run (05bd:000a)                       §3
     mission_init: block, colours, scores, fuel
     mission_load (05bd:14d6)                   §4
        mission_setup (0919:3d78)               §5
     loop until station 9 (simulation §1.1)
  → back in main: debrief, roster (game_flow)
```

## 2. Regions and world files **verified**

| `B503` | Region | Mission record | Sprites and kinds (A / B) | Tactical map sheets |
|---|---|---|---|---|
| 0 | Vietnam | `DAT1.DAT` | `DAT2A.DAT` / `DAT2B.DAT` | `MP2A.LZ` / `MP2B.LZ` |
| 1 | Colombia | `DAT2.DAT` | `DAT3A.DAT` / `DAT3B.DAT` | `MP3A/B` |
| 2 | Panama | `DAT3.DAT` | `DAT4A.DAT` / `DAT4B.DAT` | `MP4A/B` |
| 3 | Mare Island (practice; the kind list mentions "7.1 earthquake rubble") | `DAT4.DAT` | `DAT1A.DAT` / `DAT1B.DAT` | `MP5A/B` |

The region names follow from the kind names in each B file (§6.3): VC and NVA (Vietnam), drug docks,
lab barracks, DEA agents, "La Gloria", "Mompos" (Colombia), insurgents, SAM and anti-ship missile
launchers (Panama). Region 3 has no mission table (§6.1) and is used by the practice missions.

## 3. `mission_run` (05bd:000a) **verified**

### 3.1 Initialisation

```
F5CC:F5CE = F5C6:F5C8 + 0B54h; F63C:F63E = F5C6:F5C8 + 170Ch     (sub-buffers of one far block)
12ed:0063                                                       (sound spec)
copy 480 bytes to DS:ECB4 (mission text, §6.2) from
     DS:6E54 + word [DS:B2DB + 10h·region + 2·mission]           (region tables B2DB/B2EB/B2FB/B30B)
screen colours by video mode: ECAE ECAF ECB2 F106 F107 F108 F109 F10A F146
     VGA: 08 0C 0E 14 15 16 17 17 10;  EGA/Tandy: 08 0C 0E 0F 07 08 08 (F10A 0 at night, 8 by day) 07;
     CGA: 02 03 03 03 00 02 02 0 02                                (hud spec)
EED6 / F0E4 = boat X / Y (start marker for the map)
window cracks DS:0B4A..0B4D = 0; score words B52A..B541 = 0
B545 = 3 (in progress); B544 = B546 = B542 = B549 = B800 = B828 = 0; fuel B80A = B80C = C544h
mission_load()                                                  §4
D69F = 0; chase view D96B = 0; F5CA = 0
05bd:2efc screen_clear (instrument caches, message strip: hud §5); F21C = station; F286 = look
direction; EEA0 = F107
draw the starting station: 1 → 05bd:048c, 2 → 05bd:08be, 4 → 05bd:0e9e      (3 is not handled:
                                                                 the front end never starts there)
12ed:0000                                                       (engine sound on)
```

### 3.2 Loop (simulation §1.1 for the frame part)

```
loop_start_ticks = DS:08C0
if station == 9: display offset 0; B7F3 += 14h; sfx_play(12); 12ed:0063; return
if chase view, station or look direction changed since the last pass (F5CA, F21C, F286):
    EEA0 = F107; clear the screen (05bd:2efc)
    chase view: 05bd:07da; else by station (table 05bd:027E):
       1 pilot 048c | 2 bow 08be | 3 midship 10f4 | 4 stern 0e9e | 5 map 19cc | 6 chase 07da
       7 damage report 1ba4 | 8 assignment 1dba
    F5CA = D96B; F21C = station unless D96A (then D96A = 0); F286 = F346; draw page 0
if station == 5 (map): blink colour F148++ & 3; objective arrow at DS:B3C0[2·(8·region + mission)]
    (not in region 3); boat marker at ((X >> 6) + 18h, ((Y >> 7) << 1) ^ FFh + B5h) (hud spec).
    A branch for region 4 (which does not exist) would use the start position EED6/F0E4.
input_read_key; flash latches; game_frame; view_present(1, 0) (stations 1-4, not in chase view);
    key_dispatch; …   (simulation §1.1)
```

### 3.3 Returning to a 3D station (`view_restore`, 05bd:144a) **verified**

The full-screen stations use DGROUP memory from `DS:53A8` upward (the world B data) as scratch.
Every 3D station screen therefore ends with `view_restore`:

```
far_to_near_copy(F5D4:F5D6 → DS:53A8, F396 bytes)             0000:072c  (the world B data)
draw page = DS:0074; text colours 0Fh/0
sprite_cache_invalidate()                                      0919:5a9f
panel_redraw_all() 0919:0000 (indicators 40h–5Dh; Ghidra shows it as 08e1:0380, the same
address); 0919:016a (panel switches); window_cracks_draw() 05bd:3114 (a far call: push cs)
   (the two panel routines get AX as sprite_cache_invalidate leaves it: its AH is stored in
   scratch_b7e3, hud §2.1)
EA86 = F10B; ECA8 = F132; F10B = B83C; F132 = B839   (the flash latches for view_present: stern and
                                                       midship, and the frame before's; hud §6)
game_frame()
if not chase view: view_present(1, DS:0074) (05bd:1fd4, hud §6); draw page 0
```

`far_to_near_copy` / `near_to_far_copy` (`0000:072c` / `0000:0756`) use the **global**
`DS:F13A` as their loop counter (left equal to the byte count). The front end also uses
`F13A`; keep the side effect.

`sprite_cache_invalidate` clears the first byte (the cached kind) of the sprite cache slot
records (pointers `CS:583D`; slots 1..68h are in segment `D885`, slots from 69h in `D883`,
render3d §5.6) and resets the slot bitmaps (`sprite_slots_reset` `0919:6f3d`), so all sprites are
rebuilt after a full-screen station. Port-verified quirk (kept): it walks from slot B8h down and
switches to `D885` at slot 6Ah, which it skips, so slot 69h's byte is cleared at `D885:D000`
instead of its record at `D883:D000` (slot 69h is reserved anyway) and slot 6Ah (`D883:0000`) is
not cleared.
The readers `hit_test` and `line_of_sight` (simulation §7) take `D883` for sprite indexes ≥ 69h.

## 4. `mission_load` (05bd:14d6) **verified** (port, differential test: every region, the practice modes, weapon fits, day and night)

```
B839 = B83C = EA86 = ECA8 = F10B = F132 = 0; look F346 = 1 (ahead)
if F110 == 0: file_load_near("DAT10.DAT", DS:D502)                  boat record, simulation §2.2
if F110 != 0: file_load_near("DAT11.DAT", DS:D502)                  (two tests: F110 read again)
file_load_near(region A file, DS:6E54)                                §6.3; word B503: 0 DAT2A/B,
                                                                      1 DAT3, 2 DAT4, any other DAT1
file_load_far(region B file, F5D4:F5D6); F396 = size
file_load_near("DATn.DAT" (DS:0B26 + 9·region), DS:B84C)              mission record §6.1
far_to_near_copy(F5D4:F5D6 → DS:53A8, F396)                           B data into DGROUP
file_load_far("TILE.BIN", F280:F282); F142 = size
mission_setup()                                                       0919:3d78 §5
near_to_far_copy(DS:53A8 → F5D4:F5D6, F396)                           keep the colour-remapped B data
cockpit art (compressed pictures, decoded later by the station screens; hud spec):
   BD1 → F0FC (F136 = 0FA8h), BD2 → F102 (F138 = 1177h), BD3 → F63C (F100 = 0FADh),
   BD4 → F622 (F0FA = 10C7h), BD5 → F61E (F0F8 = 1081h), CLIP → F5E4 (EEA2 = 11C7h)
   bow B804 == 0: BG1 → F5C6 (EEA4 = 0C43h), BG2 → F5BE (EE9E = 2518h)
              else BF1 → F5C6 (097Ch), BF2 → F5BE (2865h)
   stern B806 == 1: B61 → F616 (EE9A = 0BEEh), B62 → F5D0 (EED8 = 2035h)
              else BGR1 → F616 (0CCBh), BGR2 → F5D0 (1EB2h)
   midship B807 == 0: BM1 → F5CC (EED0 = 0CA2h), BM2 → F5C2 (EED4 = 29C4h)
                  1: BR1 → F5CC (0D17h), BR2 → F5C2 (26EAh)
                else: B61 → F5CC (0BEEh), B62 → F5C2 (2035h)
12ed:0000; 00f2:0f16; 12ed:0063                                        (sound / palette fade)
file_load_near("TACTCOLR.BIN", DS:08C4)                               palette, 225 bytes
if night (B7FC == 0): palette bytes 15h..1Ah and 39h..47h −= 8; word DS:0952 = 0200h
    (palette byte 8Eh: colour 2Fh's green 0, blue 2; port-verified, not DS:0951). B7FC is
    time_of_day's result in mission_setup (forced by B7FC = FFh), i.e. the mission type's start time
ega_pal_init(); draw page 1
LIGHTS.LZ → F5BA (F0DE = 2EB5h), decoded (08e1:01bd) into DS:1094 and drawn to page 1
   (VGA: 121b:08a8, else 1390:0000); in EGA modes 9/0Dh 00f2:0f24(14h, 277h) first
00f2:0f46
MAP.LZ → F5BA (F0DE = 0F09h)
"MPnA.LZ" (DS:0AC6 + 10h·region) → F612 (F0E6 = DS:B444[2·region]);
"MPnB.LZ" (DS:0ACE + 10h·region) → F61A (F0EC = DS:B446[2·region])
```

The sizes stored next to each far pointer are the decoded sizes the station screens pass to the
decoder. `LIGHTS.LZ` is decoded straight into DGROUP at `DS:1094`, the terrain vertex arrays
(render3d §8), before the terrain is built.

## 5. `mission_setup` (0919:3d78) **verified** (port, differential test)

```
repeat r = random() until popcount(r) is 4..8; D8BD = r        far-object thinning (render3d §5.1)
demo mode (byte DS:0070 != 0): boat X/Y = 0B00h / 0FF0h
sprite segments D883 = F0DC + (F0DA >> 4) + 1; D885 = F0E2 + (F0E0 >> 4) + 1
video_mode_setup()                                             0919:3fba (row tables, D8F8/D8FA)
gunner sweep memories D67D/D67E/D67F = 1
D8FD = B7F2 = DA39 = B7FE = B7FF = B7F1 = 0; engines B808/B809 = 0; throttles 0
headings: hull 0, bow 0, midship 80h, stern 80h (fractions 0); bob velocity 0
crew pilot state 0; fire at will 0; visible count B83D = 0; D9B5 = 0; D70D = 0
D649 = FFh; B7FC = FFh (forces time_of_day to update); B82D = 80h; B82C = 7Fh; jet 40h
reloads B830 = 8, B831 = 30h; B7F3 −= 6; elevations B836/B837/B838 = C0h; D967 = D968 = 8
crew throttle D680 = 8; practice (low byte of F110 != 0): engines running (B808/B809 = 1) at
    throttle 46h (1), 37h (2), 08h (≥ 3); D680 = that throttle
projectile timers D1BC[32] = 0; incoming timers D71F[0..7] = 0
missile speed D6AD = DS:D6AE[region] (3, 7, 9, 7)
crew pilot: D681 = 2 (search), D687 = D688 = 0, branch D685 = 1 in region 0 else 2, D684 = 1
    (the region's low byte)
objective objects B841..B849 = mission table row (B909 + 2·(5·mission in 8 bits))
practice targets (B803): the object word of every authored object from B959−1 down to 36 becomes
    0016h (kind 16h, flags cleared); the test (unsigned) follows the store, so one word is
    written even when B959 < 37 (B959 = 0 would run through all of DGROUP)
    ("sleezy lawyer" in every region's kind list)
throttle maximum B81C = B81D = 67h (103) with the upgraded engines B805, else 3Bh (59)
start time and deadline from DS:B354 + 4·mission type: B54B hours, B54A minutes,
    B548 deadline hours, B547 deadline minutes
time_of_day()                                                  simulation §9.2
colour remap (0919:3f8d) of the A data range [6E56]..[6E58] and the B data range [53A8]..[53AA]
    for CGA (whole bytes through the table) and EGA/Tandy (bytes ≥ 10h through table + 10h);
    VGA unchanged. Both ranges go through the table that follows the A range (SI = DS:6E54 +
    [6E58] is not reloaded for the B data; port-verified, kept)
```

Start times by mission type (hours BCD : minutes; deadline):

| Type | Start | Deadline | | Type | Start | Deadline |
|---|---|---|---|---|---|---|
| 0 | 12:00 | — | | 8 | 12:00 | — |
| 1 | 21:50 | — | | 9 | 04:00 | — |
| 2 | 19:50 | — | | 10 | 21:00 | — |
| 3 | 09:30 | — | | 11 | 08:55 | — |
| 4 | 05:30 | — | | 12 | 23:25 | — |
| 5 | 21:50 | — | | 13 | 10:00 | — |
| 6 | 14:00 | 14:45 | | 14 | 13:10 | — |
| 7 | 11:15 | 12:30 | | 15 | 04:00 | — |

(Minutes are binary in the table: `32h` = 50, `1Eh` = 30, `0Fh` = 15, `2Dh` = 45, `37h` = 55,
`19h` = 25, `0Ah` = 10.) Night and dawn missions follow directly from these times.

## 6. World data formats

### 6.1 Mission record `DATn.DAT` (6468 bytes → DS:B84C) **verified**

| File offset | DS | Content |
|---|---|---|
| 0 | B84C | word, unused (0) |
| 2 | B84E | world grid 17 × 11 (render3d §2) |
| BDh | B909 | **mission table**: 8 missions × 5 words = offsets of the objective objects (mission 0 empty; region 3 all empty) |
| 10Dh | B959 | number of authored-object slots: authored objects are 36 .. B959−1 |
| 10Fh | B95B | number of **far objects**, stored from object 296 |
| 111h | B95D | object words (kind \| flags << 8) × 1000 |
| 8E1h | C12D | object X × 1000 |
| 10B1h | C8FD | object Y × 1000 |
| 1881h | D0CD | terrain structures: count, pieces, X, Y (simulation §7.3) |

Object slots: 0 boat; 1..34 temporary; 35 overflow (never listed); 36..B959−1 authored;
B959..B959+95 tile scenery (loaded with the terrain window); 296..296+B95B−1 far objects (listed
only at high detail and through the thinning word, render3d §5.1). `B959 + 96 ≤ 296` holds for all
four worlds (B959 = 199, 199, 199, 191). The earlier OBJECT_FORMAT.md did not know the far
objects.

### 6.2 Mission text block (480 bytes → DS:ECB4) **likely**

Copied by `mission_run` from the world A data and printed with `print_records` (`{col, row,
text}` records, game_flow) on the assignment screen (station 8, `05bd:1dba`). In the region 0
blocks the first record starts at column 20, row 20 and the "text" is font glyphs 13h–17h: most
likely a small picture (the mission-area map) drawn with special glyphs. Missions 1 and 5 share a
block in regions 0–2.

### 6.3 World B data (`DATnB.DAT` → far buffer, copied to DS:53A8) **verified**

| File offset | DS | Content |
|---|---|---|
| 0, 2 | 53A8, 53AA | colour-remap range (offsets from 53A8) |
| 4 | 53AC + kind | sprite info per kind: bits 0–1 scale levels, bits 2–7 size (render3d §5.6, §5.7) |
| 59h | 5401 + kind | score word index per kind 0..18h (simulation §7.2) |
| 72h | 541A + 2·kind | behaviour byte, class byte per kind 0..2Fh (simulation §8.1) |
| D2h | 547A + 2·kind | name text offset per kind, relative to DS:6E54 with 16-bit wrap (points into the B data) |
| … | | name texts; sprite data B (NATIVE_PORT.md; the A data's row pointers can reach into it) |

Kind names (region 0 / 1 / 2 / 3 where they differ):

| Kind | Name |
|---|---|
| 00 | "no target in front of you" |
| 01 | T55 heavy tank / M48 enemy tank / T62a heavy tank / T62a |
| 02 | PT-76 light tank |
| 03 | BTR-60 / BTR-70 APC |
| 04–06 | VC sampan, VC sampan, VC junk / enemy skiff, powerboats / bomb barge, skiff, powerboat |
| 07 | machine gun |
| 08 | mortar nest |
| 09 | enemy fortification |
| 0A–0C | infantry (NVA regular, VC infantry / mercenary / insurgent) |
| 0D | ammo cache / camp |
| 0E–0F | VC huts / lab barracks / SAM launcher / enemy camp |
| 10 | dock (VC, drug dock, enemy) |
| 11 | bridge base |
| 12 | "RPG-7! Duck!" / "TOW missile!" / "anti-ship missile!" / "missile! duck!" (the homing missile) |
| 13 | mine |
| 14 | VC infantry / enemy truck / ASM launcher / infantry (can launch the missile) |
| 16 | "sleezy lawyer" (practice target) |
| 17 | Huey gunship (Colombia) / Mi-24 Hind (can launch the missile) |
| 18 | wreck of 17 ("downed Hind") |
| 19–1A | civilians |
| 1B–1D | civilian huts / houses |
| 1E–1F | civilian sampan / junk / skiff / powerboat |
| 20 | civilian dock |
| 21 | civilian bridge base |
| 22 | car |
| 23 | PBR |
| 24–26 | ex-POW, SEAL unit, US infantry / DEA agent, civilian barge / … |
| 28–29 | buoys; 2A–2D trees; 2E stump; 2F water buffalo / "La Gloria" / monument / statue |
| 30 | burning rubble; 31 scorched tree; 32 dead beast / "Mompos" / statue / earthquake rubble |
| 33–35 | bodies; 37 broken buoy; 38 capsized boat; 39 a rock |

Enemy weapons and movement (behaviour byte, simulation §8.1): in **Vietnam nothing moves**; tanks
(01) and fortifications (09) fire weapon class 5 (**salvos**). In Colombia, Panama and the practice
world the enemy boats circle (behaviour 4–7) and the helicopter (17) patrols (3); later regions
give the tanks weapon class 6 and higher accuracy.

### 6.4 World A data (`DATnA.DAT` → DS:6E54) **verified** (header), **likely** (roles)

Header words: `[0]` offset of the animation byte whose bit 20h `enemy_update` toggles once per
frame and time compression holds (simulation §8.3); `[2]`, `[4]` colour-remap range; `[6]` the
composite-parts table for sprites (NATIVE_PORT.md); further words: sprite descriptors. The mission
text blocks (§6.2) are inside it. Sprite decoding: `gunboat-port/legacy/src/assets.cpp`
(`SpriteBank`), verified.

### 6.5 Other files loaded for a mission

| File | Destination | Content |
|---|---|---|
| `DAT10.DAT` / `DAT11.DAT` | DS:D502 (189) | Boat record: components, crew, panel switches (simulation §2.2); bytes D531..D5BE hold cockpit indicator and damage-report layout data (hud spec) |
| `TILE.BIN` | far F280:F282 | Tiles: vertices, scenery, route waypoints (ORIGINAL_WORLD_FORMAT.md, simulation §4.6) |
| `TACTCOLR.BIN` | DS:08C4 (225) | Palette for the 3D view; darkened at night |
| `LIGHTS.LZ`, `MAP.LZ`, `MPnA/B.LZ`, `BD1..5`, `CLIP`, weapon art | far buffers | Pictures (hud spec) |

## 7. Tables in DGROUP

| DS | Content |
|---|---|
| B2DB, B2EB, B2FB, B30B | Mission text offsets per region (8 words each, relative to DS:6E54) |
| B354 | Start time and deadline per mission type (16 × 4 bytes) |
| B3C0 | Map objective arrow (x, y) per region and mission (not region 3) |
| B444 | Sizes of the tactical map sheets per region (2 words each) |
| D6AE | Missile speed per region (3, 7, 9, 7) |
| 0B26 | Mission record names (`DATn.DAT`, 9-byte stride) |
| 0AC6 | Tactical map sheet names (`MPnA.LZ`, `MPnB.LZ`, 10h stride per region) |

## 8. Open questions

* `B7F3` (`−= 6` at set-up, `+= 14h` on mission end) and `D649`: roster or game_flow counters.
* `B803` practice targets: which practice mode sets it (game_flow).
* The per-mission objective **type** semantics beyond the three passenger types (simulation §5.2)
  and the strike objectives (three of five objects): the assignment text and debrief decide.
