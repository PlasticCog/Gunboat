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

`ega_pal_init` (`00f2:0fea`, TD3) and `00f2:0f46` set the EGA/Tandy palette registers (parked).
The time-of-day colours of the 3D view are palette **indices** chosen by the renderer
(`D94F..D952`, render3d §2), not DAC changes; at night `mission_load` darkens palette bytes
15h..1Ah and 39h..47h by 8 before applying (world §4). The explosion flash reprograms DAC register
8 (render3d §7.1).

## 4. Screen transitions

`screen_present` (`00f2:0ece`, TD3): the page 1 → page 0 dissolve. The title and front end use
it; missions copy directly (hud §1).

## 5. PORT

Mode 13h only: a 320 × 200 byte frame in the emulated memory at A000:0000 plus the RAM pages,
the DAC model and presentation from TD3's `platform/vga.c`. EGA, Tandy, CGA and CGA composite
paths are not ported (`// PORT:` at each mode dispatch).
