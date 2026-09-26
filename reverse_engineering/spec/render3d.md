# 3D renderer (GB.EXE `0919` drawing tree)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Addresses as in `RE_GUIDE.md`.
Read with `tools/fn.py`; the disassembly is authoritative. Symbols: `spec/render3d_symbols.csv`.
Confidence tags as in `simulation.md`. Several kernels here were translated and differential-tested
by the Codex sessions; they are marked **verified (Codex)** with the test that covers them.

The renderer is also part of the game logic: the simulation reads its **visible-object list**
(gunners, hit tests, spotting, identification), its **shore-contact candidates**, and its
**ramming test** (§5.4). A port must reproduce the list contents and order exactly.

## 1. Overview

### 1.1 Where the renderer runs (`game_frame`, simulation §1.2) **verified**

```
game_frame
  screen_shake_step()        0919:2e57   §7.2
  terrain_cells_update()     0919:8408   §2   (before the world group; may set D8BC)
  … world group, boat group, crew gunners, cockpit (hud) …
  DS:007A = 1; gfx_set_draw_page(1)                  (draw into the RAM page)
  palette_flash()            0919:2c35   §7.1
  terrain_frame()            0919:7158   §3   (sky, water, terrain group B; shore contact)
  object_frame()             0919:6e5c   §5   (visible list, ramming, terrain group A + sprites, spotlights)
  projectile_tick(); muzzle_flash_tick()   (simulation)
  DS:007A = 0; gfx_set_draw_page(0)
mission_run: 05bd:1fd4 copies the view to the screen and composes the cockpit (hud spec)
```

Everything is drawn into **page 1** (a 64000-byte RAM page, segment `DS:D9B8`) and then
presented. The 3D view is the rectangle **X 40..295, Y 64..127** (256 × 64 pixels) of the
320 × 200 page; `DI = 5028h` is its top-left corner (row 64, column 40).

### 1.2 Video modes

`DS:EED2` is the video mode: 13h VGA (the port's only target), 0Dh EGA, 09h Tandy, 04h CGA.
Routines that touch pixels dispatch on it (`> 0Dh` VGA, `== 0Dh` EGA, `== 9` Tandy, else CGA).
Per-mode helpers: sky/water fill (`74c0` VGA / `4ce2` EGA / `5529` Tandy / `4587` CGA),
water shimmer (`7458` / `4d64` / `55da` / `4639`), spotlight beam (`7bbd` / `4c18` / `5494` /
`44f0`), and the row-drawer pair `DS:D8F8`/`D8FA` set at `0919:3fc6..4036` (VGA `788e`/`7943`,
RE_GUIDE). Only the VGA routines are specified in detail; EGA (plane set-up at `7268`, `6e77`)
and CGA paths are parked. The Tandy routines and the exact dispatch conditions: §11 (ported).

### 1.3 The scene rebuild flag `D8BC` **verified**

Set by `terrain_cells_update` when the terrain window changes, and by view jumps (simulation
§11.2). While set: the terrain is always re-projected, the terrain draw order is reset (§3.5),
the visible-object list is rebuilt (§5.1) and fully sorted (§5.3), the enemy AI and projectiles
skip the frame. `object_frame` clears it after the object update.

## 2. Terrain window (`terrain_cells_update`, 0919:8408) **verified**

The world is the 17 × 11 grid of 1024-unit cells at `DS:B84E` (simulation §2.1). The renderer
keeps the vertices and scenery of the cells around the camera in fixed arrays.

```
terrain_cells_update()
  q = quadrant: bit 1 = ((Y_high − 4) >> 2) carry, bit 0 = ((X_high − 4) >> 2) carry
  cell = (9 − ((Y_high − 4) >> 2)) * 17 + ((X_high − 4) >> 2) + 1      (the cell whose
         centre area holds the boat: the window is offset by half a cell)
  if cell == D9AD and (not overflowed D9A6, or q == D9A5, or the hysteresis test at
     8443..848b keeps the old quadrant): return
  D8BC = 1; D9A5 = q; D9AD = cell; D960 = D962 = 0 (vertex counts); D9A4 = 0 (scenery count);
  D9A6 = 0 (overflow)
  mask = ~rol(CS:83C6[tile of cell], 2·rotation)       neighbour-visibility bits → D9B2
  for k in 0..8:                                        order CS:83E4[9·q + k]
     n = neighbour record CS:8399 + 5·that value: (mask bit, dX, dY, cell offset word)
     if n.bit & D9B2: skip                              (hidden by the centre tile)
     origin D9A9/D9AB = cell corner (X_high & FCh + dX, Y_high & FCh + dY) << 8
     tile_load(grid[cell + n.offset])                   0919:858f
     for each terrain structure s (DS:D0CD list, simulation §7.3) inside this cell:
        origin = structure position − 200h; tile_load((piece − 43h) | rotation from its high byte)
  D9B2 = ~D9B2
```

The hysteresis (`0919:8443..848b`) compares the boat's position inside the cell, `(X >> 2) − 70h`
and `(Y >> 2) − 70h`, with 20h and 90h, and keeps the old quadrant while the boat is near the
middle, so that crossing a half-cell line back and forth does not rebuild every frame.

`tile_load` (`0919:858f`, AL = cell byte): tile id = low 6 bits, rotation = bits 6–7 (`B7E2`).
The TILE.BIN record (far segment `DS:F282`, base `DS:F280`, ORIGINAL_WORLD_FORMAT.md) gives
counts A, B, objects, waypoints. Group A vertices go to indices 0..511 and group B to
200h..3FFh through `vertex_load` (`0919:8665`); scenery objects are appended to the object
arrays at `B959 + D9A4` (at most **60h** scenery objects), with the type byte, the low 3 bits of
the entry's **offset** in TILE.BIN as the facing (flags byte), and rotated/translated position
(`route_rotate`). Overflow of either group (512) or of the scenery (96) sets `D9A6`. Quirks
(port-verified, kept): the vertex count A + B is summed in 8 bits (a record with 256 vertices
loads none); `vertex_load` returns SI where it stopped, so when group A loads nothing (group
full, or A = 0) group B reads group A's vertices; the scenery count is summed in 8 bits too
(D9A4 + objects can wrap below 60h and load more than the limit).

`vertex_load` (`0919:8665`) per vertex **verified (Codex:** `verify_original_tile_code.py`,
25,576 + 12,788 cases, and the daytime colours**)**: control byte → `DS:1096 + i` with the
colour substituted: 0Eh → `D951`, 0Ah → `D952`, 02h → `D952 ^ (alt & D9B0)` (port-verified:
the base is D952, which is 02h at night), 06h →
`06h ^ ((alt & D9B1) ^ 4)` where `alt` toggles 00h/1Fh per vertex (`D9AF`); other colours unchanged;
bit 7 (primitive mode) kept. Height → word `DS:1496 + 2i`; X and Y (rotated, ×8 + origin, then
×4) → words `DS:1C96 + 2i` and `DS:2496 + 2i`. `D94F..D952` and `D9B0/D9B1` come from the time
of day (simulation §9.2).

## 3. Terrain frame (`terrain_frame`, 0919:7158) **verified** (port, differential test)

```
terrain_frame()
  terrain_setup()                                          0919:736c   §3.1, §3.2
  if not D8BC and camera (D972, D974), view heading (D191:D192) and horizon D953 equal the
     values saved last time (D906, D908, D90A, D90C): skip to draw
  terrain_save_view()                                      0919:728c   (saves them)
  D900 = D8FF; D8FF = 0; D901 = D903 = FFFFh               (shore contact, simulation §10)
  project(group A: 0 .. D960); project(group B: 200h .. 200h + D962)     0919:7523  §3.3
  if not chase view: shore contact test and response      (simulation §10)
draw:
  if D8BC: order_reset()                                   0919:72a9   §3.5
  order_sort()                                             0919:72c0   §3.5
  EGA (byte EED2 == 0Dh): map mask 0Fh, graphics mode 0, enable set/reset 0Fh, rotate 0
     (OUT 3C4h/3CEh at 7268; parked in the port)
  draw_group_b()                                           0919:763f   §3.4
```

The skip test compares the view heading as the word `D191:D192` (heading high, fraction low)
with `D90A`, and the horizon after `terrain_setup` has recomputed (and decremented) it. The
contact of the last projection survives a skipped frame (D8FF, D900 unchanged).

### 3.1 Horizon, sky and water (`terrain_setup`) **verified**

```
horizon = (D193 − 42h + 15h) − sar((B82D − 80h), 3); if > 3Dh: 3Dh      → D953
top = min(D965, horizon); D965 = horizon                 (rows already covered by the cockpit art)
sky = D94F; if D9B5 (explosion flash): D9B5−−; sky = 0Fh on the first frames, 0Eh on the last
VGA (74c0): rows top..horizon−1 of the view window = sky (256 bytes per row);
            2 rows = colour 08h (horizon line);
            rows to row 63 = water D950 (0Eh during a flash)
```

`sky_water_vga` also decrements `D953` after using it; the saved view (`D90C`) and the
projection (`+ 1`) use the decremented value consistently. Keep the order.

The pitch `D193` and pitch reference `B82D` come from `camera_pitch_bob` (simulation §5.1).

### 3.2 Water shimmer (`0919:73e0..7457`) **verified**

32 wave marks: horizontal offset `DS:D90D[k]`, age `DS:D92D[k]`. Each frame every offset moves by
`−2 × (D191 − D94E)` (the view's turn since the last frame; `D94E` = last heading) and every age
grows by 2; at 20h the mark respawns at a new offset taken from the **RNG state word DS:0088**
(bytes alternate between marks; `random()` is not called) with age 0. Only with detail high
(`D6BF == 0`): the 32 rows from the horizon line down show one mark each, row r using mark
`((D94D >> 2) + r) & 1Fh`, drawn by `7458` (VGA) at column `D90D[mark]`: the farthest rows (row
counter > 16h) get a single pixel, nearer rows (≤ 16h, ≤ 0Eh) larger marks that also grow with
the age (≥ 0Bh, 11h, 17h, 1Bh), in colours `D950 | 7` and `D950`. A row past the page
(`DI >= 9EE8h`) stops the loop.

### 3.3 Projection (`project`, 0919:7523) **verified (Codex:** `verify_original_renderer.py`,
3,000 cases**)**

For each vertex i of a group (SI = first index, AX = end):

```
dx = X[i] − D972; dy = Y[i] − D974                        (quarter units)
octant fold (B7F5) and ratio small/large → angle byte via atan table CS:3606 (D730 = raw)
bearing word = ±(angle << 5 masked 1FE0h) + quadrant base (DS:D741[octant]) − view word D190
                                                              → DS:2C96[i]
scale = (CS:3502[…] >> 1) / large, or FFh when very close   → DS:4496[i] (word, 0..FFh)
if scale == FFh and height(i) or height(i+1) is 0:            shore-contact candidate:
   rel = high(bearing) + D191 − B81E − 14h; forward (speed ≥ 0): rel in 0..57h;
   reverse: rel in 80h..D7h  → first candidate D901, later ones overwrite D903
   (port-verified: the reverse window is 80h..D7h; D901/D903 hold the byte offset 2i)
row = ((200h | scale) − ((scale × height) >> 5)) clamped ≥ 0, >> 3, + horizon + 1  → DS:3496[i]
```

Angles: 65536 per turn; 128 angle units are one pixel column (Codex). Heights are unsigned
bytes; `height × scale >> 5` is the vertical offset above the water line.

### 3.4 Primitives and drawing **verified (Codex:** dispatcher and VGA fill, 1,800 pixel
cases**)**

Control byte `DS:1096[i]`: colour = low 6 bits (0 = no primitive), mode = bits 6–7: 00
triangle (i, i+1, i+2), 01 line (i, i+1; none occur in the shipped worlds), 10 triangle (i−1,
i+1, i+2). `draw_primitive` (`0919:767a`, AX = colour, DL = mode, BX = vertex) culls a
triangle whose three bearings all have bit 15 set, then fills it with `fill_triangle`
(`0919:7802`, which calls the per-mode span routine `[DS:D8F8]`, VGA `0919:788e`) and the
edge set-up `0919:7909` (calls `[DS:D8FA]`, VGA `0919:7943`, with the edge slope in `D95A`). Rows are inclusive, columns
exclusive; bearings wrap as signed 16-bit values; addresses wrap at 16 bits inside the page.

Port-verified details: a triangle's vertices are sorted by row word; all three on one row →
one row between the widest pair of bearings; a flat bottom or top → one half (`D964` = 1 when
the half starts at the apex `D95E`); else two halves split at the bearing interpolated on the
middle row. Mode 3 (bits 6-7 = 11) reads its first vertex from the odd byte offset 2i − 3
(kept). `fill_triangle` (AL = first row, AH = last row, CX/DX = the flat side's bearings): the
edge steps are (edge − apex) / (rows − 1) as a signed magnitude, `D956` += 40h and `D958` += BFh
(so the left column rounds up and the right one down). A span's columns are bits 7-15 of the
bearings (0..1FFh): a left column of 180h-1FFh is a span wrapped from the left of the view
(clipped at column 0), 100h-17Fh is off the view; the width is cut at column 256; widths 0
and 1 both fill one pixel. The line routine (`span_vga_b`) draws from the edge to where it
will be on the next row, and its step becomes ±80h (one column) on the last row.

`draw_group_b` (`0919:763f`): group B primitives from index `200h + min(D962, 1FEh) − 1` **down**
to 200h, in stream order. Group B is drawn entirely before objects (the far/flat terrain).
Group A is drawn later, sorted and interleaved with sprites (§5.5).

### 3.5 Draw order of group A **verified**

`order_reset` (`0919:72a9`, on `D8BC`): the order array `DS:3C96[0..511]` = 0, 1, 2, …

`order_sort` (`0919:72c0`): key for each entry (`DS:4096[k]`, word): 0 for no primitive; else
the scales of the primitive's vertices (i, i+1, and i+2, or i−1 for mode 2; two for a line)
with **high byte = the largest, low byte = the middle one of the three** (port-verified: the
code keeps the second largest, not the smallest; a line's key is its larger and smaller scale).
Then a bubble sort of `3C96`/`4096`
by key, **descending** (nearest first): each pass swaps adjacent entries while the left key is
smaller, and the next pass ends at the last swap position; sorting stops when that position is
≤ 2, so the first entries can stay unsorted (**quirk: keep**). The order persists between
frames, so the sort is incremental.

## 4. Camera and view

| DS | Meaning |
|---|---|
| D190 / D191 | View heading word (low byte = `ror3(fraction)`, high = heading); simulation §1.2 |
| D192 | Raw heading fraction |
| D193 | View pitch (from `camera_pitch_bob`) |
| D953 | Horizon row in the view (≤ 3Dh) |
| D96E / D970 | Camera position (map units) |
| D972 / D974 | Camera position ×4 + fraction bits (renderer units) |
| D96B / D96C / D96D | Chase view, its heading and distance |
| D9B8 | Segment of the drawing page (page 1) |

`terrain_rect_test` (`0919:7c38`, CX/DX = a point in 1/4 units; simulation §5.3, the chase camera)
sets `B7E2` = 1 when the point counts as land: when the camera's cell (from `D96E`/`D970`) is
neither the window's centre cell `D9AD` nor one of its loaded neighbours (the visibility bits of
the centre tile, records `CS:839C + 5k`), or when it lies in the box of a group A primitive (in
draw order, all entries but the last two) or of a group B vertex from 200h (all but the last
three): on each axis between the first vertex's coordinate and one of the next two (bit 7: the
first vertex is i − 1; line primitives and empty entries are skipped). Port-verified.

## 5. Objects (`object_frame`, 0919:6e5c)

```
object_frame()
  if D8BC: visible_list_rebuild()                          0919:69a9   §5.1
  visible_project()                                        0919:6d92   §5.2
  object_update()                                          0919:6af9   §5.3, §5.4
  D8BC = 0
  sprite_lod_update()                                      0919:6cad   §5.6
  (EGA plane set-up)
  draw_group_a_and_sprites()                               0919:6e94   §5.5
  spotlights()                                             0919:7a89   §6
```

### 5.1 Visible-object list (`visible_list_rebuild`, 0919:69a9) **verified**

Rebuilt only on scene rebuilds. Entries (`DS:523E[2k]` = object offset):

1. entries 0..34 = objects 0..34 (boat and temporary objects), always;
2. the `D9A4` scenery objects loaded with the terrain window;
3. authored objects 36 .. `B959` − 1 whose cell (`X & FC00h`, `Y & FC00h`) lies in the window
   around the camera cell (`D96E`/`D970`): per axis the camera cell and the next one, plus the
   previous one when the visibility bits allow it and minus the next one when they do not
   (X: bits C1h / 1Ch of `D9B2`; Y: 70h / 07h), so 1–3 cells per axis;
4. with detail high only: objects from 296 to 296 + `B95B` that pass the same window test **and**
   whose bit in the rotating word `DS:D8BD` is set (a thinning pattern).

At most B5h (181) entries; `B83D` = count. Sprite cache slots of entries beyond the new count are
freed (`D8BF` = previous count).

**Bug (port-verified, not reproducible):** the scenery entries are copied with `LOOP` on
CX = `D9A4`; a terrain window without scenery (D9A4 = 0) runs it 65536 times, writing entry
offsets over all of DGROUP including the stack, and the routine returns to a garbage address.
The shipped data has such windows: Mare Island (region 3, the practice missions), grid cells
137 and 154 (open water, all nine window cells scenery-free), i.e. the boat at X 05xx-07xx,
Y 05xx-0Bxx, in the bay west of the practice start cell. The port ends with a fatal error
there (PORT); to confirm in DOSBox.

### 5.2 Projection (`visible_project`, 0919:6d92) **verified**

For each entry i with a nonzero kind: `dx = X·4 − D972`, `dy = Y·4 − D974`; `atan`
(`0919:3712`) gives the bearing and `D730`; the distance is `dx / cos` using the sine table
`CS:3502` interpolated with the two low bits of `D730` (shifted out of D730, which is left
>> 2), divided into `max × 65536`, doubled (`7FFFh` when bit 15 gets set); an entry is skipped
when its object's kind byte equals the entry index's high byte (0: no object):

| Array | Content |
|---|---|
| `DS:4C96[2i]` (word) | distance; its **high byte `4C97[2i]` is the "distance class"** used by the AI |
| `DS:4E00[i]` | screen bearing: `bearing − D191 + 4Ch` (−1 when the fine part borrows) |
| `DS:4EB5[i]` | fine bearing: `(B7E2 − D192) & 7` |
| `DS:4F6A[i]` | inverse distance: `FFFFh / distance` (FFh when distance < 256); used as depth and as the "elevation" in hit tests |
| `DS:50D4[i]` | `−bearing` (for the sprite view angle, §5.7) |
| `DS:5189[i]` | sprite cache slot + 1 (0 = none), §5.6 |
| `DS:501F[i]` | apparent size (scale) of the last drawn sprite |

### 5.3 Sorting **verified**

Object 35 (the overflow slot of `free_temp_object`, simulation §4.3) is never in the list, so
an object written there is neither drawn nor hit.

On scene rebuilds `list_quicksort` (`0919:8eb9`, recursive `list_quicksort_range` `8ece` with
the first entry as the pivot, `list_swap` `8f53`; partitions of up to 20 entries go to
`list_bubble_range` `8fa7`, bubble passes until a pass ends at the range start) sorts entries
1..count−1 by distance (ranges as byte offsets on the stack, the stride AX = 2; a range of fewer
than two entries is never passed and would make the recursion run over all of DGROUP);
otherwise `list_bubble` (`0919:6c23`) does
incremental bubble passes, **descending distance** (farthest first), starting at entry 1 (entry
0 too in chase view) and stopping when the last swap is below entry 2. A swap exchanges
`4C96`, `523E`, `4E00`, `4EB5`, `4F6A` and `50D4` but **not `5189`** (the sprite cache slot
stays with the list position; the LOD pass then only checks the slot's scale level). This may
briefly show one object with its neighbour's cached sprite: **quirk: keep, verify against DOS**.

### 5.4 Ramming (`object_update`, 0919:6af9) **verified**

After sorting, the nearest non-temporary object (walking from the end of the list) is tested,
once per frame:

```
o = nearest entry with object offset >= 48h
if distance > E0h or chase view or D8BC: return
if kind in {0, 21h, 17h, 11h}: return
rel = 4E00[i] − D191 + B81E − 20h (− 80h when reversing); if rel >= 50h: return   (not ahead)
D62D = 30h; turn away: rel < 28h → rotate_headings_plus, else rotate_headings_minus
sfx_play(5)
if |speed| >= 15 and (DS:0088 & 1Fh) == 0: hull hit, message 0Ch "Ram damage to " (0919:2a69)
kind 13h (mine): becomes 014Bh (explosion), sfx 8, ram damage to the hull, twice in region ≥ 1
kinds 04h–06h, 38h, 1Eh–1Fh: crushed → 0448h (fire), sfx 8, mission_target_check
```

The boat does not stop; it is turned 6 heading units away per frame of contact. Messages are
shown on page 0 (the routine switches the draw page around them).

### 5.5 Group A and sprites, back to front (`0919:6e94..6f28`) **verified**

```
p = min(D960, 1FEh); e = B83F                    (first entry drawn: 1, or 0 in chase view)
while e < B83D or p > 0:
   if p > 0 and (e == B83D or avg(key[p−1] bytes) <= 4F6A[e]):
        p−−; draw_primitive(order[p])            (terrain primitive, farthest remaining)
   else:
        draw entry e if it has a sprite slot: skipped when off screen (4E00 + 8 > 90h) only for
        temporary objects; sprite_prepare (0919:5acc) + blit_record (0919:5c71); e++
```

`avg` is `(low + high) >> 1` of the primitive's key with the carry (`rcr`).

### 5.6 Sprite LOD and cache (`sprite_lod_update`, 0919:6cad) **verified**

`B83F` = 0 in chase view, else 1. Two passes from the last entry down to `B83F`: first
permanent objects (offset ≥ 48h), then temporary ones. For each (`0919:6cfa`):

* release the slot if the distance word ≤ 10h, the kind is 0, or the distance class > `D6C0`
  (detail: FFh high, 16h low);
* level = 2 if class < 0Ah, 1 if < 0Fh, else 0, capped at `DS:53AC[kind] & 3` (the kind's number
  of scale levels);
* keep the slot if its level `CS:6F5F[slot]` equals the wanted level, else free it (bitmap
  `DS:D8D1`, bit n−1 for slot n; clear masks `DS:D8C1[8]`) and allocate a new one
  (`sprite_slot_alloc` `0919:7017`).

Slots (port-verified): level 0 = slots 1..99h (200h bytes each), level 1 = 9Ah..B2h (400h),
level 2 = B3h..B8h (800h); slot n's record is at `CS:583D[n−1]`, in segment `D885` for slots
1..68h and `D883` from 69h (slot 69h at `D883:D000`, the offsets restart at 0 from slot 6Ah). Slot 69h (`D883:D000`, overlapping slot B2h) is reserved:
`sprite_slots_reset` (`0919:6f3d`) clears the bitmap and sets its bit (`D8DE` = 1), clears every
entry's slot and `D8BF`. Allocation: level 0 scans the bitmap from `D8D1` by words (skipping full
words, 16 slots each; no end test), then bytes and bits; levels 1 and 2 start at their pool
(`D8E9` first slot number, `D8EC` first-byte mask and 8 − first bit, `D8F2` first byte) and go to
the end of the bitmap (`D8E8`); a full pool allocates nothing. `sprite_lod_entry` returns SI
shifted left and back (bit 15 lost), and its caller's loop goes on with it.

### 5.7 Sprite images **verified (Codex:** `verify_assets.py`, 1,056 type/view combinations**)**

`sprite_prepare` (`0919:5acc`): `sprite_view_angle` (`0919:60e0`): facing = object flags
bits 0–2 × 32 (the boat: `B81E + 80h`) + `50D4[i]`; kinds 39h and ≥ 3Fh are not directional;
apparent size from `DS:53AC[kind] & FCh` × 8 against the distance through the atan table →
`D864` → `501F[i]`. Then `sprite_cache_build` (`0919:5aeb`) builds the cached image at that
size and view (ported: `src/render/sprites.cpp`).
`blit_record` (`0919:5c71`) and `0919:5d5a` place it by bearing and inverse distance and call
the per-mode row copier: VGA `0919:5e66` (the TD3 matcher's name `sprite_rows_mirror` was
wrong here), EGA `48da`, CGA `4056`, Tandy `4f96`; zero pixels are transparent; kind 39h is
never drawn.

**The scaled image (port-verified).** `sprite_cache_build`: view = (D86A + 2) >> 2 (kind 30h adds
2 × `D70C` first, kind 31h `D70C`: animation; kinds 39h-3Bh use even sizes); the slot is kept
when its record holds the same kind, size and view (never for kind 17h). The kind's 8-byte record
in the world A data (`DS:6E54 + 8·kind`): width, rows; row pointer table; row type table; side
width and a second part's index (its record at `6E54 + [6E5A] + 8·index`: width, rows, tables, and
a column ratio; the ratio byte `D872` = (ratio << 8) / width). Cache record: +0 kind, +1 size, +2
view, +3/+4 width and rows of the first part, +5/+6 of the second part (0 when none), +7 `D872`,
+8 24 row repeat counts, +20h the pixels. `sprite_scale_patterns` (`0919:6174`): the zoom
`D865` = size / 18h (up to 2; 3 from 48h) and from the scale table `CS:59AF` (10-byte records,
one per size step 0..17h, record 17h − step; step = size mod 18h) a 56-bit column pattern
(repeated over `D889..D8A2`) and a 24-bit row pattern (`D8A4..D8A6`, used from its top bit); from
size 48h on, the column pattern of record 0 (all bits) and the row pattern of size − 48h. Rows (`0919:5f4b`, `5fc8`, `6054` for zoom 0,
1, 2-3): at zoom 0 a source row is kept when its row-pattern bit is set; at zoom ≥ 1 every row,
repeated `D865` times plus once more on a pattern bit. Each row scaler (by the row's type byte)
walks the column pattern (a kept bit adds a column: at zoom 0 only kept columns, at zoom 1 each
pixel once plus once on a kept bit, zoom 2 twice plus one): type 80h (`65eb`/`66a7`/`6764`) a
strip of `type & 7Fh` pixels scrolled by the view (from pixel `n × view / 32`, the pattern from
`width × view / 32`); type 40h (`6824`/`68a4`/`6925`) `type & 3Fh` pixels centred; other types
(`6244`/`6373`/`64ac`) two faces of a box (`type` and the next byte pixels) whose visibility
comes from the face bit patterns `DS:D8A7[view & 7]` and whose order and side width from view
bits 3-4, padded to centre the row. Rows stop when another row of the last width would pass the
slot end (`D87F`); DI then stays past it (the second part starts there).

`blit_place` (`0919:5d5a`): column = bearing − (width / 2 + part offset) / 2 + 8, doubled, clipped
at column 40 on the left and after column 296 on the right (297 with the half column `D873`:
one or two columns past the view's last column 295, kept); first row =
`4F6A[i]` / 8 + (−B82D) / 8 + `D193` − 2Ch − part height; nothing when that is 50h or more; rows
above 10h (the view's top, screen row 48 + row) are skipped (their repeat counts consumed);
`D965` = min(D965, max(row − 10h, 0)). A second
part (`D870` columns) is drawn above the first, shifted by `D870 × D872 / 512` columns.

## 6. Spotlights (`spotlights`, 0919:7a89) **verified**

Only at night (`B7FC == 0`), not in chase view, and with the main switch on (bit 0 of `D520`
clear). `D905 = 0`, then for each light that is switched on (bit 0 clear), not destroyed
(condition & 3 ≠ 2) and whose gun mount is on (bit 0 clear), in the order bow, midship, stern
(the stern tests its mount before its condition); the fraction byte passed is the gun's heading
fraction ORed with the light's condition bits (port-verified, kept):

| Light | Switch | Condition | Mount | Aims with |
|---|---|---|---|---|
| bow | `D528` | `D50E` | `D525` | bow gun `B837`, `B81F`/`B823` |
| midship | `D52D` | `D50F` | `D529` | midship gun `B836`, `B820`/`B824` |
| stern | `D530` | `D50C` | `D52E` | stern gun `B838`, `B821`/`B825` |

`spotlight_beam` (`0919:7b17`) sets `D905 = 1` (the enemy spotting bonus, simulation §8.3),
computes the beam's screen column from the gun bearing relative to the view and its starting row
from the gun elevation against the current station's elevation and `D193`, and draws 10 rows of
beam widths from `CS:709A` (VGA `0919:7bbd`): each covered pixel pair gets bit 3 set where bit 4
of the colour was clear (the lit palette half). Rows reaching the horizon raise `D965`.

The condition bytes differ from the damage-message names (simulation §8.6: `D50F` is called the
rear spotlight, `D50C` the middle one): keep the code's mapping.

## 7. Flash and shake

### 7.1 Explosion flash (`palette_flash`, 0919:2c35) **verified**

Level = `D9B5 & 3` in a 3D station (else 0); when it changes (the **raw** D9B5 is compared with
`D6E5`, which stores the level & 3, so a value ≥ 4 reprograms the DAC every frame; port-verified):
VGA sets DAC register 8 to
the RGB triple `DS:D6EA + 3·level` (INT 10h AX=1012h); EGA/Tandy set palette register 8 from
`DS:D6E6[level]` through `147c:000f`. Together with the sky colour override in `terrain_setup`
this is the whole-screen flash after hits and explosions.

### 7.2 Screen shake (`screen_shake_step`, 0919:2e57) **verified**

`B7F2` counts down (set to 2 or 8 by `boat_hit`); each step sets the display start from the pair
`CS:2E47[2·B7F2]` (`gfx_set_display_offset(x = odd byte, y = even byte)`). **Not in VGA mode 13h**
(`EED2 == 13h` skips it): the VGA version only flashes. `gfx_set_display_offset` (`149f:0004`)
in mode 13h: CRTC start = y × 80 + x / 4, also to the BIOS page offset `0040:004E`, written after
the start and the end of a vertical retrace (port 3DAh at the BIOS's CRTC base + 6); the text
modes and modes 8/0Ah only return 0.

## 8. Arrays (DGROUP) **verified**

| DS | Size | Content |
|---|---|---|
| 1096 | 1024 | Vertex control/colour bytes (group A 0..1FFh, B 200h..3FFh) |
| 1496 | 1024 words | Vertex heights |
| 1C96 / 2496 | 1024 words | Vertex X / Y (renderer units) |
| 2C96 | 1024 words | Projected bearing |
| 3496 | 1024 words | Projected row |
| 3C96 | 512 words | Group A draw order |
| 4096 | 512 words | Group A sort keys |
| 4496 | 1024 words | Vertex scale (inverse distance, 0..FFh) |
| 4C96 | 181 words | Object distance |
| 4E00, 4EB5, 4F6A, 501F, 50D4, 5189 | 181 each | Object bearing, fine bearing, inverse distance, size, −bearing, sprite slot |
| 523E | 181 words | Object offsets (the visible-object list) |
| D960 / D962 | word | Vertex counts of groups A / B |
| D9A4 / D9A5 / D9A6 | byte | Scenery count / quadrant / overflow |
| D9A9 / D9AB | word | Tile origin during loading |
| D9AD | word | Current window cell (FFFFh forces a rebuild) |
| D9B0 / D9B1 | byte | Alternating-shade masks (time of day) |
| D9B2 | byte | Neighbour visibility mask |
| D90D / D92D | 32 each | Water marks: offset / age |
| D906..D90C | | Last projected view (still-frame test) |

## 9. Function table

| Address | Name | § |
|---|---|---|
| 0919:2c35 | palette_flash | 7.1 |
| 0919:2e57 | screen_shake_step | 7.2 |
| 0919:3712 | atan (simulation) | 5.2 |
| 0919:3f8d | colour_remap (world) | world §5 |
| 0919:3fba | video_mode_setup (world) | 1.2 |
| 0919:5a9f | sprite_cache_invalidate (world) | 5.6, world §3.3 |
| 0919:5acc | sprite_prepare | 5.7 |
| 0919:5aeb | sprite_cache_build (Codex) | 5.7 |
| 0919:5c71 | blit_record (Codex) | 5.7 |
| 0919:5d5a | blit_place | 5.7 |
| 0919:5e66 | blit_rows_vga | 5.7 |
| 0919:5f4b | sprite_scale_rows | 5.7 |
| 0919:5fc8 | sprite_scale_rows_up | 5.7 |
| 0919:6054 | sprite_scale_rows_up2 | 5.7 |
| 0919:60e0 | sprite_view_angle | 5.7 |
| 0919:6174 | sprite_scale_patterns | 5.7 |
| 0919:6244 | sprite_row_box | 5.7 |
| 0919:6373 | sprite_row_box_up | 5.7 |
| 0919:64ac | sprite_row_box_up2 | 5.7 |
| 0919:65eb | sprite_row_turn | 5.7 |
| 0919:66a7 | sprite_row_turn_up | 5.7 |
| 0919:6764 | sprite_row_turn_up2 | 5.7 |
| 0919:6824 | sprite_row_flat | 5.7 |
| 0919:68a4 | sprite_row_flat_up | 5.7 |
| 0919:6925 | sprite_row_flat_up2 | 5.7 |
| 0919:69a9 | visible_list_rebuild | 5.1 |
| 0919:6af9 | object_update | 5.4 |
| 0919:6c23 | list_bubble | 5.3 |
| 0919:6cad | sprite_lod_update | 5.6 |
| 0919:6cfa | sprite_lod_entry | 5.6 |
| 0919:6d92 | visible_project | 5.2 |
| 0919:6e5c | object_frame | 5 |
| 0919:6f3d | sprite_slots_reset | 5.6 |
| 0919:7017 | sprite_slot_alloc | 5.6 |
| 0919:7158 | terrain_frame | 3 |
| 0919:728c | terrain_save_view | 3 |
| 0919:72a9 | order_reset | 3.5 |
| 0919:72c0 | order_sort | 3.5 |
| 0919:736c | terrain_setup | 3.1 |
| 0919:7458 | water_marks_vga | 3.2 |
| 0919:74c0 | sky_water_vga | 3.1 |
| 0919:7523 | project | 3.3 |
| 0919:763f | draw_group_b | 3.4 |
| 0919:767a | draw_primitive (Codex) | 3.4 |
| 0919:7802 | fill_triangle (Codex) | 3.4 |
| 0919:788e | span_vga_a | 3.4 |
| 0919:7909 | edge_setup | 3.4 |
| 0919:7943 | span_vga_b | 3.4 |
| 0919:79e1 | shore_contact_test (simulation) | simulation §10 |
| 0919:7a3e | shore_edge_test | simulation §10 |
| 0919:7a89 | spotlights | 6 |
| 0919:7b17 | spotlight_beam | 6 |
| 0919:7bbd | spotlight_beam_vga | 6 |
| 0919:7c38 | terrain_rect_test | 4 |
| 0919:8229 | camera_position (simulation) | simulation §5.3 |
| 0919:82de | chase_view_collision (simulation) | simulation §5.3 |
| 0919:8331 | polar_small | simulation §5.3 |
| 0919:8408 | terrain_cells_update | 2 |
| 0919:858f | tile_load | 2 |
| 0919:8665 | vertex_load (Codex) | 2 |
| 0919:8711 | route_rotate (simulation) | 2 |
| 0919:8eb9 | list_quicksort | 5.3 |
| 0919:8ece | list_quicksort_range | 5.3 |
| 0919:8f53 | list_swap | 5.3 |
| 0919:8fa7 | list_bubble_range | 5.3 |
| 149f:0004 | gfx_set_display_offset (video) | 7.2 |

All of these are ported (`gunboat-port/src/render/`, `src/platform/gfx_display.cpp`) and
differential-tested on all memory (`tests/difftest/test_render.py`), except `object_update`,
`object_frame` and `terrain_frame`, which call simulation routines ported later.

## 10. Open questions

* The hysteresis rule in `terrain_cells_update` (`8443..848b`) and the neighbour tables
  `CS:8399`/`83C6`/`83E4`: ported from the disassembly and differential-tested (the rule only
  applies when the window overflowed, D9A6).
* The empty terrain window of Mare Island (§5.1): confirm the original's crash in DOSBox.
* The thinning word `DS:D8BD` and the objects 296 and up (step 4 of §5.1): what they are
  (far scenery?) and how `D8BD` changes.
* Whether the unswapped sprite slot in `list_bubble` (§5.3) is visible in DOS.
* `05bd:1fd4` (presenting the view and the cockpit) belongs to the hud spec.
* The sprite blitter's placement arithmetic (`5d5a`): ported and differential-tested (§5.7);
  a scene check against DOSBox captures is still to do.

## 11. Tandy (mode 9) **ported** (`gunboat-port/src/render/mode_tandy.cpp`, test `test_modes_tandy.py`)

A Tandy page (the screen at `B800h` and the RAM pages 1 and 2, 32 KB each) holds 320 × 200
pixels of 4 bits, two to a byte (the left pixel in the high nibble), 160 bytes a row in four banks
2000h apart: row r at `2000h·(r & 3) + A0h·(r >> 2)`. The next row is `+ 2000h, and 7FFFh, + A0h`
when that wrapped to bank 0; the view's rows are `view_row_table` (`D74D`, `video_mode_setup`: row
64, column 40 = `0A14h`). The routines touch no port; colour c is the byte `11h·c`
(`tandy_colour_pairs` `D852`). The game's real modes are 13h, 0Dh, 9 and 4 (Hercules stores 4);
the callers' conditions differ for other values (a GUNBOAT.CFG written by hand):

| Caller | Takes the Tandy routine when the mode's low byte is |
|---|---|
| `blit_place` (`5e3f`) | not 13h, 0Dh or 4 (CGA is 4 only) |
| `terrain_setup` sky and water (`73be`) | 9 only (0Ah–0Ch take the CGA routine `4587`) |
| `terrain_setup` water marks (`7438`), `spotlight_beam` (`7ba0`), `view_copy_*` (`8a4c`…) | 9–0Ch |
| `fill_triangle` / `edge_setup` (`[D8F8]` / `[D8FA]`) | set by `video_mode_setup` for 9–0Ch: `5693` / `5756` |

### 11.1 `blit_rows_tandy` (`4f96`, AL = row, SI = source)

`B7E2 = AL + 30h` (the screen row); DI = `AL·28h` (8-bit MUL) `+ 1FD8h` if the row is odd `+ 3FB0h`
if bit 1 is set, plus `(2·B7F7 + D873) >> 1` (or 28h >> 1 with SI += D875 when clipped left); the
column's parity to `B7E3`; nothing from screen row 80h. Per row on page `D9B8`: D862 source bytes
from `D887:SI`, zero transparent, else the low nibble rotated by `D848[parity]` into the byte kept
with `D84A[parity]`; DI + 1 after each right nibble. Then DI += 2000h (− 7F60h after bank 3), `B7E2`
+1; `D86F` counts down **in memory** (the next count from `D87B` + 1, SI += B7F8); it stops at
screen row 80h or when `B7F9` runs out.

### 11.2 `spotlight_beam_tandy` (`5494`, ES, BX = widths in CS, DX = column)

As `spotlight_beam_vga` (§6) with DI = `view_row_table[B7E2] − 10h` and the same clipping; the
covered pixels get bit 3: the first byte ORs `D84C` = 08h (a start on a right nibble) or `D84D` =
88h, then 88h a byte while 2 or more pixels are left, then `D84E[rest]` (00h / 80h). No test of
bit 4 (the VGA beam sets bit 3 only where bit 4 is clear).

### 11.3 `sky_water_tandy` (`5529`, ES, AL = sky, BL = horizon, CL = sky top) → DI

Rows CL..BL−1 of `D852[AL & 0Fh]`, then (DI returned) two rows of 88h, then `D852[D950 & 0Fh]`
(EEh during a flash `D9B5`) for `40h − D953 − 2` rows; D953 is decremented. 128 bytes a row.

### 11.4 `water_marks_tandy` (`55da`, ES, BX = mark, CL = rows, DI = row)

Per row, ended when the row's bank offset `DI & 1FFFh` reaches 13DDh (row 128; VGA stops a row
earlier): the mark's size 0..4 from its age and CL as the VGA marks (1 from 0Bh at CL ≤ 16h; 2/3/4
from 11h/17h/1Bh at CL ≤ 0Eh); four pixels at `D90D[mark]` + the offsets `water_mark_patterns`
(`D816` + 10·size, words: +1 a pixel right, ±140h a row down / up, corrected for the bank), each
ORed with `D850[parity]` (70h / 07h). AX and DX are not used.

### 11.5 `span_tandy_a` (`5693`) and `span_tandy_b` (`5756`)

As `span_vga_a` / `span_vga_b` (§3.4) with the rows from `view_row_table[D8FC − 40h]` (D8FC
counted up in memory; rows before 40h skipped, a row from 80h to BFh ends it), the colour
`D852[D954 & 0Fh]` and 4-bit pixels: a start on a right nibble draws that nibble first (nothing for
a count of 0), then whole bytes, then a last left nibble for an odd rest. The line routine makes a
count of 0 one pixel.

**Tests** (`test_modes_tandy.py`, Tandy machine, all memory and the card state): the original's own
states in mode 9 (the game folder copied with a Tandy `GUNBOAT.CFG`; missions stopped where
game_frame enters each routine) plus targeted random cases: `blit_rows_tandy` 315 cases,
`spotlight_beam_tandy` 256, `sky_water_tandy` 164, `water_marks_tandy` 214, `span_tandy_a` 370,
`span_tandy_b` 314; the dispatch points and whole `terrain_frame` / `object_frame` passes in mode 9
449; the view copies (hud §6) 168. Planted bugs (a bank step, a nibble order, the row limit, a beam
mask, a clip limit, a flash colour, the horizon decrement, the end test 13DDh, a bank correction, an
age limit, a nibble mask, the one-pixel line, the last step, the view copies' steps) are each
reported.
