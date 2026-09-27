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
paths are not ported (`// PORT:` at each mode dispatch). (The graphics library's own EGA, CGA and
Tandy paths are ported since: §7; the Hercules picture: §6.)

## 6. Hercules: the picture of the game's CGA memory **verified** (card.cpp)

The Hercules choice (video mode 0Ch in GUNBOAT.CFG) runs the game in CGA mode 4 (`main`, game_flow
§2; `hercules_mode` DS:0076 = 1, which nothing reads) and `hercules_setup` (`121b:0902`) programs
the card to show that memory: configuration 3BFh = 3 (graphics allowed, page 1 enabled), mode 3B8h
= 0 (off), B800:0000-7FFF cleared, the CRTC R0-R11 from `DS:DA2D` = 38 28 2D 0A 7F 06 64 70 02 02
06 07, then mode 8Ah (graphics, video on, page 1 = B800h). R1 = 28h characters of 16 pixels (640
pixels, 80 bytes), R6 = 64h character rows of R9 + 1 = 3 scan lines (300 lines). The card reads
scan line RA of character row r at ((RA & 3) << 13) | ((MA & 0FFFh) << 1) | byte, MA = start
(R12/R13) + 28h · r + character: line 3r shows CGA row 2r (the even bank), 3r + 1 row 2r + 1 (the
odd bank) and 3r + 2 bank 2 (B800:4000, cleared and never drawn: black), each CGA byte as eight
monochrome pixels (colour 0: both dark, 1: the right one lit, 2: the left one, 3: both). `card_compose` builds that 640 × 300 frame.
The start address is 0 unless the display offset (the screen shake, `gfx_set_display_offset` on the
BIOS's CRTC port 0040:0063 = 3B4h) moves it.

## 7. The library in the EGA, CGA and Tandy modes **ported**

Every primitive jumps through a table in its own segment (`lea bx, [table]; add bx, [DCF8];
jmp cs:[bx]`, one word per mode 0–13h). Ported (`src/platform/gfx.cpp`): the handlers of the modes
Gunboat sets, **04h** CGA (the Hercules choice draws in it and `hercules_setup` shows B800h), **09h**
Tandy, **0Dh** EGA (the same handlers serve 0Eh–12h: only the row bytes differ) and 13h, plus the
text-mode handlers. Not ported (`// PORT:`, never set by Gunboat): mode 6 and the Hercules modes
0Bh/0Ch; modes 8 and 0Ah draw nothing in the original either. Layouts: CGA 2 bits a pixel, row y at
`2000h·(y & 1) + 80·(y >> 1)`; Tandy 4 bits (the left pixel in the high nibble), row y at
`2000h·(y & 3) + 160·(y >> 2)`; EGA planar, 40 bytes a row, page p at `A000h + p·200h`. The
addresses are computed as the code does (`XCHG AL, AH; SHR AX, 1; ADD BH, AL …`, `RCR` for the
Tandy's bank bits), in 16 bits.

### 7.1 Mode set, detection, pages

* `gfx_set_mode`: the per-mode colour tables are copied into `gfx_dither` (DDC1, 64 bytes: per
  colour 0–1Fh its fill on even and odd rows) and `gfx_colour_map` (DD41): 4/5 from DE41/DD81, 6/11h
  from DE01/DD61, 8–0Ah and 0Dh–10h/12h from DE81/DDA1; INT 43h = F000:FA6E for 4–6 and 8–0Ah. EGA
  0Dh–12h: graphics controller register 1 (enable set/reset) = 0Fh, so every EGA drawing writes the
  set/reset colour that `gfx_set_colour` put in register 0. 11h: DAC 1 = white, attribute palette
  (INT 10h AX=1002h) from DD61; 12h: DAC 0–15 from DEC1, palette from DDA1. Hercules 0Bh/0Ch: BIOS
  data from E0F4, configuration 3BFh = 3, CRTC 3B4h from E112 (9 words), B000h–BFFFh cleared, mode
  3B8h = 0Ah; no BIOS mode set.
* `gfx_detect`: INT 10h AX=1A00h (a VGA BIOS: AL = 1Ah, BL = 8 → 12h; 0Ch → 13h; 0Bh/07h → 11h);
  else INT 10h AH=12h BL=10h: an EGA (BL ≠ 10h) active per 0040:0087 bit 3 → 0Fh with a monochrome
  display (bit 1), 10h with switches 9, else 0Dh; else the equipment word 0040:0010 bits 4–5 = 30h
  (monochrome): bit 7 of 3BAh changing within 32768 reads → 0Bh Hercules, else 07h MDA; else
  FC00:0000 = 21h → 09h Tandy, else 04h CGA. The BIOS model answers per machine (bios.cpp,
  biosmodel.py): 1A00h only on the VGA; AH=12h BL=10h on the EGA (and VGA) from 0040:0087/0088,
  which `bios_init` sets on the EGA machine to 60h / 08h (256 KB, active, colour, switches 1000:
  detected as 0Dh); the Hercules machine's 3BAh toggles (card model), so it is detected as 0Bh.
* `gfx_set_draw_page` / `gfx_set_copy_page` (page & 7): modes 4–0Ah, 7 and 13h take the page
  table `gfx_page_seg`; the text modes (draw page only; the copy page does nothing in them) and EGA
  0Dh–10h compute the page in the video memory (`video seg + (page·page bytes) >> 4`); Hercules:
  pages 0–1 computed, 2–7 from the table; 11h/12h do nothing.
* `gfx_set_visible_page`: nothing if already visible; 4–0Ah, 7, 13h exchange the two pages'
  segments and contents (one screen); text modes: INT 10h AH=05h (modelled as the BIOS data
  0040:0062/004E, `bios_set_active_page`); EGA: 0040:0062 = page, 0040:004E = page·page bytes, a wait
  for the start and then the end of a vertical retrace (3DAh bit 3), CRTC 0Ch/0Dh = that offset,
  `gfx_visible_seg` follows; Hercules: page & 1, the same BIOS data, 3B8h = 0Ah | page·80h; 11h/12h:
  nothing (the page is not stored). Gunboat calls it once, with 0 after the mode set: a no-op.
* `gfx_alloc_page`: RAM pages (DOS 48h, cleared) in 4–0Ah and 13h; Hercules only for DL > 1
  (signed); the text and EGA modes return 1, so on the EGA Gunboat's pages 1 and 2 are the card's
  (A200h, A400h: hud §1). `gfx_free_page`: 4–0Ch and 13h.
* `gfx_clear_page`: text modes 0720h, the packed modes zeros; EGA: write mode 2, bit mask FFh, each
  byte read (latches) and written 0, then write mode 0.

### 7.2 Rectangles and pixels

`gfx_fill_rect` (rows y1 up to y0; no clipping): per row a left byte through the mask of the pixels
from x0, whole bytes, a right byte through the mask up to x1 (one byte: both masks ANDed; a span
reversed by one inside a byte draws nothing, across a byte boundary a whole byte). CGA/Tandy
(14cf:01a8): the colour repeated (c·55h, c·11h) through the masks; the row above: bank − 2000h, or
from bank 0 `(off | interleave) − row bytes`. EGA: the masks go to the bit mask register and the
byte is ANDed (read: latches; write: set/reset colour under the mask); the bit mask is left at the
right byte's. `gfx_put_pixel` (clip box, signed): CGA/Tandy clear the pixel's bits and OR the colour
in unmasked (a colour above the mode's bits spills into the next pixels); EGA: bit mask 80h >> (x & 7)
(left set), AND. `gfx_line_to` and `gfx_fill_rect_clipped` have no mode code of their own.

### 7.3 Rectangle copies

`gfx_copy_rect_from_copy_page` / `gfx_copy_rect_to_copy_page`: in CGA and Tandy the screen side is
**B800h itself**, not the draw page (`mov ax, 0B800h; mov es, ax`); whole bytes x0/4..x1/4 (x0/2..);
rows by the interleave (6000h: four banks, else two). EGA: write mode 1 (the latches copy all four
planes), draw page ↔ copy page, then write mode 0. `gfx_copy_rect`: CGA/Tandy/13h take the pages
from the page table (interleave 2000h: `si − width`, `− row` unless in bank 1, `^ 2000h`; 6000h:
`− width`, from bank 0 `| 8000h − row`, then `− 2000h`); EGA computes both pages as `video seg +
page·(page bytes >> 4)`, write mode 1.

### 7.4 Bitmaps

`gfx_draw_bitmap`: each source byte, shifted to the pen's pixel within the byte (`SHL AX, CL` of
the previous and current byte), becomes pixels: CGA a word of 2-bit pixels (bits doubled) masked
into the memory with the colour pattern `gfx_bitmap_pattern` (E126, written by the routine: c·55h
both bytes); Tandy two words of 4-bit pixels (c·11h); a row not starting on a byte gets one more
word (pair) of the leftover bits. Quirk kept: the Tandy tests "no leftover" with `or cl, cl` while
CL is 7 or 8, so the pair is always written (with CL = 8 unchanged). EGA: each byte through the bit
mask register, the leftover byte when CL ≠ 8; the bit mask is left at the last byte's. Rows upward:
CGA `^ 2000h` (− 80 from bank 0), Tandy − 2000h or `| 6000h − A0h`, EGA − row bytes.
`gfx_read_bitmap`: CGA/Tandy read 3 bytes per result byte (a big-endian word and the next byte,
shifted to the pen's pixel), XOR the pattern (re-read from E126 for each byte), NOT, and keep a bit
per pixel whose bits all match; EGA: read mode 1 (colour compare against register 2, set by
`gfx_set_colour`) with word reads (two bytes, low address first: the latches keep the second),
then read mode 0.

### 7.5 Pictures

`picture_hline` (1390:006e, near: AX = x0, BX = x1, **ES = the draw page**, loaded by
`picture_draw`) fills the pen row with the colour's row pattern `gfx_dither[(s8)(colour·2) + (y &
1)]` (`SHL AL, 1; CBW`: colours from 40h index below the table). CGA/Tandy: masks as in
`gfx_fill_rect`. EGA: the entries are colours; this row's goes to set/reset; if the other row's is
the same, one pass (bit masks as in `gfx_fill_rect`), else the even pixels (bit mask 55h) in this
row's colour and the odd ones (AAh) in the other's, set/reset left at the second. The text modes and
8/0Ah jump into `picture_draw`'s exit (1390:005f), which would unwind the wrong stack (a crash; never
reached: `// PORT:` draws nothing). `picture_draw` has no mode code of its own.

### 7.6 Verification

`tests/difftest/test_modes_lib.py`, each test on its machine (EGA, CGA, Tandy, Hercules), all memory
and the whole card state (planes, latches, every register, and here also the status toggle of 3DAh
/ 3BAh) compared: `gfx_set_mode` (every mode, from start-up and from a drawing state), `gfx_detect`
(the BIOS bytes varied) and `gfx_saved_mode`, the page routines in every mode 0–13h, fills, pixels,
lines and clears, the three rectangle copies, both bitmaps, `picture_hline` and `picture_draw`, with
random coordinates at byte edges, one-byte and reversed spans, odd rows, random pages, colours above
the mode's bits, random dither tables and (EGA) random graphics controller and map mask registers:
26 tests, 6957 cases, 0 mismatches. Planted bugs (a missing bank bit, a wrong mask register, the
retrace wait one read short, swapped dither masks, a missing latch read, the Tandy tail skipped …)
are each reported. The Unicorn side needed one fix in `cardmodel.py`: Unicorn splits an unaligned
word read of MMIO into two aligned word reads, which loaded the latches from a byte the CPU never
reads; a memory hook now reads the CPU's bytes and the MMIO callbacks serve them.

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

Tests: the routines above on each machine (3,111 cases, all memory and the card state; a port
`host_pump` where the original has no poll point shows as a timer tick); and the game on states the
original reaches on each machine: `config_load`, `title_menu` (the menu and the whole intro),
`menu_cursor_init`, the front end's screens (office, folders, spec sheets, maps, outfitting), a whole
front end, `hq_quiz`, `mission_load`, the map, damage report and assignment stations,
`station_screen_colours`, `screen_clear`, `jet_marker`, `radar_scope`, `message_line_draw`, the 3D
stations' screens in tours of the stations, and whole missions (the demo, a tour ended by Ctrl+Q).
By default the original's calls of the graphics library run the port's library there
(`PortLibrary`), so the game code is compared whatever state the library's other-mode paths are in;
`GB_MODES_HYBRID=0` runs the original on its own library. A check whose port run reaches a routine
still a stub on the branch is skipped and reported. On a trial merge with the library and renderer
packages every one of these runs and passes in both ways (74 tests, 3,479 cases with the original
on its own library).
