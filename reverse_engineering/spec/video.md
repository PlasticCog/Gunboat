# Video: graphics library, pages, palette (GB.EXE `137e`–`15ea`, `121b` palette, `00f2` wrappers)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Symbols: `spec/video_symbols.csv`.

The graphics library is **the same code as Test Drive III's** (23 of its 35 routines match, the
rest are the same library's mode-specific helpers). `test-drive-3-sdl3/port/spec/platform.md`
§2.1, §3.1, §4.1–4.3 is its specification; the port implements the **mode 13h (VGA) path only**,
exactly as TD3 does (TD3's `platform/vga.*` is converted to C++ as
`gunboat-port/src/platform/vga.cpp`).

## 1. Modes and pages **verified**

`DS:EED2` = video mode: 13h VGA (the port), 0Dh EGA, 09h Tandy, 04h CGA (0Ch = CGA composite,
parked). `gfx_set_mode` `1555:0005`; `gfx_detect` `1386:0004`; library routines dispatch on the
mode through per-mode tables (TD3 platform §4.2). Pages: 0 = screen (A000h), 1 = RAM page
(`gfx_alloc_page` `137e:0000`, segment `DS:D9B8`), 2 outside VGA only (hud §1). Draw page
`gfx_set_draw_page` `157d:000e` (`DS:007A` mirrors it in the game), copy page `gfx_set_copy_page`
`154e:000b`, visible page `gfx_set_visible_page` `1592:0004`, display start
`gfx_set_display_offset` `149f:0004` (screen shake, not used in VGA; render3d §7.2).

## 2. Library routines used by Gunboat **verified** (TD3 match)

`gfx_set_colour` `1543:000b`, `gfx_move_to` `147b:000c`, `gfx_line_to` `13d5:000d`,
`gfx_put_pixel` `14b5:000d`, `gfx_fill_rect` `14cf:0008`, `gfx_fill_rect_clipped` `15d9:0003`,
`gfx_clear_page` `15e2:0001`, `gfx_copy_rect` `15a4:0006`, `gfx_copy_rect_from_copy_page`
`1502:0001` (copy page → draw page) and `gfx_copy_rect_to_copy_page` `1522:000e` (draw page →
copy page; both copy the rows from y1 up to y0), `gfx_draw_bitmap`
`13e2:0002`, `gfx_read_bitmap` `1432:000c`, `gfx_set_pal_reg` `157d:0078`,
`gfx_set_ega_palette` `148c:000d`, `gfx_free_page` `142c:0009`, `text_exit_clear` `14ff:0001`.
Gunboat-only helpers in the same range: `1390:0000` (picture draw, all modes; platform §6),
`1432:0008` (returns `DS:DD2D`), `147c:000f` (EGA/CGA palette register set via INT 10h), `14ae:0005` (EGA palette
entry, wrapped by `00f2:0f24`), `1469:0008` (returns the mode saved at start-up, `DS:DCFC`).

Used by the cockpit (hud §3), ported (`platform/gfx_lines.cpp`): `gfx_line_to(x, y)` draws from
the pen to (x, y) and moves the pen there: a horizontal or vertical line is one
`gfx_fill_rect_clipped` rectangle (ends sorted, signed), any other a Bresenham line of
`gfx_put_pixel` calls (both ends, its steps and error terms in `DS:E065..E06F`);
`gfx_fill_rect_clipped(x0, x1, y0, y1)` cuts the rectangle to the clip box `DD03..DD09` (signed;
nothing if outside) and calls `gfx_fill_rect`.

Used by the front end (game_flow §6), ported: `gfx_copy_rect(x0, x1, y0, y1, dx, dy_bottom, src,
dst)` copies a rectangle between two explicit pages, the destination given by its **bottom** row
(rows copied from y1 up to y0; `DD25` = the destination's bottom row, `DD27` = dx; no clipping);
`gfx_put_pixel(x, y)` sets one pixel in the current colour on the draw page, clipped to the window
`DD03..DD09`. `ega_pal_set` (`14ae:0005`) dispatches on the library mode through the table at
`14ae:0055`; the mode-13h entry (`14ae:0050`) only returns 0, so the EGA colour calls that the spec
sheets make in every mode do nothing in VGA.

## 3. Palette **verified**

The 3D view and most screens use one 225-byte palette file (`TACTCOLR.BIN`, `TITLCOLR.BIN`,
`TIT1/2/3COLR.BIN`, `QUIZCOLR.BIN`, `GENCOLR.BIN`) loaded at `DS:08C4` (6-bit RGB triples). The
VGA-only wrappers act on **its first 32 colours** (96 bytes, DAC 0..31) through INT 10h AX=1012h,
using a 96-byte buffer at `121b:000E`:

| Wrapper (VGA only) | Routine | Action |
|---|---|---|
| `00f2:0eec` | `121b:087f` pal_apply | DAC 0..31 = palette |
| `00f2:0efa` | `121b:085d` pal_black | DAC 0..31 = 0 |
| `00f2:0f08` | `121b:0804` pal_fade_in | levels 1..16: `(c · level + 8) >> 4`, one step per `DS:08C0` tick |
| `00f2:0f16` | `121b:07ae` pal_fade_out | levels 15..0, same |

`ega_pal_init` (`00f2:0fea`, TD3) and `ega_pal_apply` (`00f2:0f46`) set the EGA/Tandy palette
registers and the colour patterns (ported, §8.4).
The time-of-day colours of the 3D view are palette **indices** chosen by the renderer
(`D94F..D952`, render3d §2), not DAC changes; at night `mission_load` darkens palette bytes
15h..1Ah and 39h..47h by 8 before applying (world §4). The explosion flash reprograms DAC register
8 (render3d §7.1).

## 4. Screen transitions

`screen_present` (`00f2:0ece`, TD3): the page 1 → page 0 dissolve. The title and front end use
it; missions copy directly (hud §1). Every mode's dissolve is ported (§8.3).

## 5. PORT

Mode 13h only: a 320 × 200 byte frame in the emulated memory at A000:0000 plus the RAM pages,
the DAC model and presentation from TD3's `platform/vga.c`. EGA, Tandy, CGA and CGA composite
paths are not ported (`// PORT:` at each mode dispatch).

## 8. Palettes, display start and the game side of the other modes **verified**, **ported**

Ported for every mode with its branch (package pkg-game); tests `test_modes_game.py` on the EGA,
CGA, Tandy, Hercules and VGA machines, all memory and the whole card state compared.

### 8.1 The library's palette routines (`platform/gfx_palette.cpp`)

The library keeps a **colour pattern pair** per colour 0..1Fh (`gfx_dither`, DS:DDC1, the
fill bytes of even and odd rows that `picture_hline` uses; the defaults are copied by
`gfx_set_mode`). Dispatch on the library mode (`DCF8`):

| Routine | CGA 4/5 | CGA 6 | Tandy 8-0Ah | Hercules 0Bh/0Ch | EGA 0Dh-12h | 13h |
|---|---|---|---|---|---|---|
| `ega_pal_set` `14ae:0005` (index & 1Fh, value) | byte, byte rol high byte | same | 9 only: same; 8/0Ah nothing | same as CGA | low / high nibble (11h: bits 0 / 7) | nothing |
| `ega_pal_register` `147c:000f` (reg, value) | INT 10h 0Bh BH=0 `cga_background_bits[reg]` \| value, BH=1 `cga_palette_select[reg]`; mode register = (0040:0065 & FBh) \| `cga_mode_bits[reg]` to 3D8h and 0040:0065 | 0Bh BH=0 value | gate array register 10h + reg (IN 3DAh, OUT 3DAh, OUT 3DEh) | nothing | INT 10h AX=1000h | DAC reg = rgbRGB levels (AX=1010h) |
| `gfx_set_ega_palette` `148c:000d` (16 words) | nothing | nothing | the 16 gate array registers | nothing | 16 bytes to `ega_palette_regs` (148c:00F3, 17th byte never written), AX=1002h | DAC 0..15 through `ega_palette_rgb` (148c:0104), AX=1012h |

The rgbRGB levels: red = bit 2 | bit 5 << 1, green = bit 1 | bit 4 << 1, blue = bit 0 | bit 3 << 1,
each through `ega_level_rgb` = {0, 2Ah, 15h, 3Fh}. The CGA tables by reg (0..5):
`cga_background_bits` 10 10 10 00 00 00, `cga_mode_bits` 00 00 04 00 00 04, `cga_palette_select`
00 01 01 00 01 01. `gfx_set_pal_reg` (`157d:0078`) has no caller in GB.EXE and its jump table
(CS:00BC) only makes sense for CS = 1584h (the table at 157d:012c); ported as called at 1584:0008
(Tandy / EGA 0Dh, 0Eh: RGB bits with the intensity from a negative index; 0Fh/10h: 2-bit levels
as rgbRGB; 11h-13h: the DAC).

### 8.2 The display start (`gfx_set_display_offset` `149f:0004`, `platform/gfx_display.cpp`)

Start address: CGA 4-6 `(y >> 1)·40 + (x >> 3)`; Tandy 9 `(y >> 2)·80 + (x >> 2)`; 0Bh
`(y >> 2)·45 + (x >> 3)`, 0Ch the same of (x·2, y + y/2); EGA 0Dh-12h `y·row_bytes + (x >> 3) +
visible page · page_bytes` with the pel panning `x & 7`; 13h `y·80 + (x >> 2)`; the others return.
Then 0040:004E = start, a wait for the retrace to start and to end on the BIOS's CRTC port + 6
(3DAh; 3BAh on a Hercules machine, where bit 3 is the video dot signal), CRTC registers 0Ch/0Dh
by `OUT DX, AX`, and with a pel panning a wait for the next retrace and the attribute controller's
register 13h (index 33h). PORT: outside 13h the status port is read as the original reads it
(each read reaches the card), after one `host_wait_vretrace` for the pace of a real retrace.

### 8.3 Text and the dissolve (`platform/text.cpp`, `platform/pal.cpp`)

`text_draw_char`'s EGA path (`121b:049c`, EED2 = 0Dh) writes straight into the game's draw page
(`page_segments[DS:007A]`, in the card's memory): map mask 0Fh, write mode 0, set/reset on all
planes; per glyph row (bottom first, `text_y·40 + text_col + 118h`, 40 bytes up) the foreground
word goes to the graphics controller (its low byte, 0 as `text_set_colours` stores it, is the
index: set/reset), the glyph is the bit mask of a write, then the inverted glyph is the bit mask
and, unless `text_transparent`, the background word and a read-and-write fill the rest. The
registers are left so.

`dissolve_page1_to_0` (`121b:0581`) by the game mode's low byte: 4 and 0Ch the CGA path (2-bit
pixels, `dissolve_cga_masks`, odd rows moved to the second bank), 0Dh the EGA path (page 1 read
plane by plane through the read map into a colour, written with set/reset and the pixel's bit
mask `dissolve_ega_masks`; **no timer wait**: the EGA dissolve runs as fast as the machine),
above 0Dh VGA, the others (9) the Tandy path (4-bit pixels, `dissolve_tandy_masks`, the row
`dissolve_rows / 40` by DIV into its bank). CGA, Tandy and VGA wait for a `DS:08C0` tick per step.

### 8.4 The game's palette code

`ega_pal_entry` (`00f2:0f24`) calls `ega_pal_set` with the low byte only in Tandy mode 9.
`ega_pal_apply` (`00f2:0f46`): CGA the patterns 0..1Fh from `cga_patterns` (the palette file's
words at DS:0964), Tandy the patterns 10h..1Fh from `ega_patterns` (DS:0944) as `(c & 0Fh)·11h`,
EGA 10h..1Fh from `ega_patterns`. `ega_pal_init` (`00f2:0fea`): Tandy converts `ega_palette`
(DS:0924, the palette file after the 96 DAC bytes) in place to IRGB (`(v & 10h) >> 1 | v & 7`, the
high byte cleared: a second call loses the intensity, kept), Tandy and EGA load it with
`gfx_set_ega_palette`, then `ega_pal_apply`. `config_load` sets `ega_pal_register(1, 0)` in CGA,
`front_end` restores palette register 8 (`ega_pal_register(8, ega_palette[8])`) in EGA and Tandy,
`menu_cursor_init` sets seven CGA patterns for the pencil, `palette_flash` (render3d §7.1) the flash
register. The screens' `ega_pal_entry` calls and the picture drawing per mode (`picture_draw`
outside VGA, the per-mode title pictures `FOOTIT1E/F.LZ`) were checked against the code.

On the EGA the pages 1 and 2 are the card's memory at A200h and A400h (`gfx_set_draw_page`'s EGA
path: video segment + page · page_bytes / 16; `gfx_alloc_page` allocates nothing there); on the
CGA, Tandy and Hercules they are RAM.

Tests: the routines above on each machine (3,111 cases); and, with the original's calls of the
graphics library running the port's library (`PortLibrary`, so the game code is compared on its own
whatever state the library's other-mode paths are in), `config_load`, `title_menu` (the menu and the
whole intro), `menu_cursor_init`, the front end's screens (office, folders, spec sheets, maps,
outfitting), a whole front end, `hq_quiz`, the map, damage report and assignment stations,
`station_screen_colours` and `screen_clear`, on states the original reaches on each machine.
