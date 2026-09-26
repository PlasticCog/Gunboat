// The renderer's CGA routines (render3d.md §1.2, render/modes.hpp): mode 4, two bits a pixel with
// pixel 0 of a byte in bits 7-6, 80 bytes a row, the even rows at 0000h and the odd rows at 2000h
// of the page (the screen B800h, or a RAM page of the same layout). The 64 rows of the view start
// at the offsets of view_row_table (video_mode_setup: row 64, byte 10 = 0A0Ah; view column 0 is
// screen column 40, as in VGA). Hercules draws through these too: the game runs mode 4 and
// hercules_setup shows its memory (GB.EXE never reads hercules_mode, DS:0076).
//
// The colours are the 2-bit values: the spans and the sky/water take the low two bits of their
// colour through cga_colour_byte, the sprites the low two bits of each source pixel; the spotlight
// beams OR colour 2 into the pixels and the water marks OR colour 3.
#include "render/modes.hpp"

#include "mem.hpp"
#include "render/render.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Shared scratch bytes: B7E2 the row (sprite rows: the screen row; beams: the view row; spans: the
// rows left), B7E3 the beam rows left / the first pixel of a sprite row within its byte; B7F8
// (shot_elevation) the width of the sprite part (as in sprites.cpp).
constexpr u16 BLIT_ROW = DS_scratch_b7e2;
constexpr u16 BLIT_PIXEL = DS_scratch_b7e3;
constexpr u16 BEAM_ROW = DS_scratch_b7e2;
constexpr u16 BEAM_ROWS = DS_scratch_b7e3;
constexpr u16 SPAN_ROWS = DS_scratch_b7e2;
constexpr u16 SPRITE_WIDTH = DS_shot_elevation;

u8 rol8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v << n | v >> (8 - n)) : v;
}

// The next row down from a row start that has just been advanced by 2000h: back in the even bank
// (bit 13 clear) it is one row pair further (add di, 2000h / test di, 2000h / jne / sub di, 3FB0h).
u16 bank_down(u16 di)
{
    di = u16(di + 0x2000);
    if (!(di & 0x2000)) di = u16(di - 0x3FB0);
    return di;
}

// The row start of view row `row` (view_row_table).
u16 view_row(u8 row) { return ds_u16(u16(DS_view_row_table + 2 * row)); }

// A span edge's column (0..1FFh): bits 7-15 of the bearing (xchg ah, al / rol ax, 1 / and ah, 1).
u16 span_column(u16 v) { return u16(v >> 7 & 0x1FF); }

// Pixels `mask` of the byte at ES:DI in the colour byte DH, the others kept (mov al, es:[di] /
// mov bh, al / and bh, ah / xor al, bh / and ah, dh / or al, ah / stosb).
void merge(u16 es, u16 &di, u8 mask, u8 dh)
{
    const u8 old = mem_u8(es, di);
    mem_u8(es, di) = u8((old ^ (old & mask)) | (mask & dh));
    di = u16(di + 1);
}

// One span of the two span routines (0919:4725-47b2, 4820-48ad): from column AX (0..FFh, or a
// wrapped left part: AX >= 180h), CX pixels (0..FFh), clipped to the 256 columns, in the colour
// byte DH: the partial first byte, the whole bytes (words, then a last byte), the partial last
// byte. A width of 0 draws nothing.
void span_fill(u16 es, u16 di, u16 ax, u16 cx, u8 dh)
{
    if (ax >> 8) {
        if (ax < 0x180) return;
        ax = u16(0xFF00 | u8(ax));
        cx = u16(cx + ax);
        if (cx & 0x8000) return;
        ax = 0;
    }
    u16 bx = ax;
    di = u16(di + (ax >> 2));
    if (u8(bx) + u8(cx) > 0xFF) cx = u16(0x100 - bx);
    bx = u16((bx & 0xFF00) | (bx & 3));
    if (u8(bx) != 0) {
        bx = u16((bx & 0xFF00) | u8(4 - u8(bx)));
        u8 mask = ds_u8(u16(DS_cga_mask_last + bx));
        cx = u16(cx - bx);
        if (cx == 0 || (cx & 0x8000)) {  // the span ends in its first byte
            if (cx != 0) {
                cx = u16(-cx);
                mask = u8(mask ^ ds_u8(u16(DS_cga_mask_last + cx)));
            }
            merge(es, di, mask, dh);
            return;
        }
        merge(es, di, mask, dh);
    }
    // The whole bytes: CX / 4 of them as words and a last byte (their net effect, byte by byte).
    bx = cx;
    for (cx >>= 2; cx; cx--) {
        mem_u8(es, di) = dh;
        di = u16(di + 1);
    }
    bx &= 3;
    if (bx != 0) merge(es, di, ds_u8(u16(DS_cga_mask_first + bx)), dh);
}

// The colour byte of the span colour D954's low two bits.
u8 span_colour()
{
    return ds_u8(u16(DS_cga_colour_byte + (ds_u8(DS_span_colour) & 3)));
}

} // namespace

// 0919:4056 blit_rows_cga (render3d.md §5.7): the rows of blit_rows_vga in mode 4. The screen row
// B7E2 = AL + 30h (the view starts at row 64 = view row 10h + 30h); the column 2 x B7F7 + D873 (40
// when clipped at the left, the source then skipping D875 columns) gives the byte and the first
// pixel B7E3. Each of the D862 source bytes of a row that is not zero puts its low two bits into
// its pixel; each source row is repeated by its count at D87B. Rows from 80h on are not drawn.
void blit_rows_cga(u8 al, u16 si)
{
    al = u8(al + 0x30);
    ds_u8(BLIT_ROW) = al;
    u16 ax = u16(al * 0x28);
    if (ds_u8(BLIT_ROW) & 1) ax = u16(ax + 0x1FD8);  // odd rows: bank 2000h, one row pair less
    u16 bx = u16(u16(ds_u8(DS_sprite_column) << 1) + ds_u16(DS_sprite_half_column));
    if (ds_u16(DS_sprite_clip_left) != 0) {
        bx = 0x28;
        si = u16(si + ds_u16(DS_sprite_clip_left));
    }
    ds_u8(BLIT_PIXEL) = u8(bx & 3);
    u16 di = u16(ax + (bx >> 2));
    if (ds_u8(BLIT_ROW) >= 0x80) return;
    do {
        const u16 page = ds_u16(DS_draw_page_segment);
        u16 d = di, s = si;
        u16 pixel = ds_u8(BLIT_PIXEL);
        u8 count = ds_u8(DS_sprite_blit_width);
        do {
            u8 px = mem_u8(ds_u16(DS_sprite_segment), s);
            s = u16(s + 1);
            if (px != 0) {
                px &= 3;
                u8 v = mem_u8(page, d);
                v = u8(v & ds_u8(u16(DS_cga_pixel_keep + pixel)));
                px = rol8(px, ds_u8(u16(DS_cga_pixel_shift + pixel)));
                mem_u8(page, d) = u8(v | px);
            }
            pixel = (pixel + 1) & 3;
            if (pixel == 0) d = u16(d + 1);
        } while (--count != 0);
        di = u16(di + 0x2000);
        ds_u8(BLIT_ROW)++;
        if (!(ds_u8(BLIT_ROW) & 1)) di = u16(di - 0x3FB0);
        const u16 record = ds_u16(DS_sprite_segment);
        if (--ds_u8(DS_sprite_row_repeat) == 0) {
            ds_u16(DS_sprite_repeat_ptr)++;
            ds_u8(DS_sprite_row_repeat) = mem_u8(record, ds_u16(DS_sprite_repeat_ptr));
            si = u16(si + ds_u8(SPRITE_WIDTH));
        }
        if (ds_u8(BLIT_ROW) >= 0x80) return;
    } while (--ds_u8(DS_sprite_rows_left) != 0);
}

// 0919:44f0 spotlight_beam_cga (render3d.md §6): the ten rows of spotlight_beam_vga in mode 4, from
// view row B7E2 (the beam ends at row 40h): 2 x (A0h - width byte) pixels around column DX + width
// - 20h (the widths from CS:709A at BX), clipped to the view as in VGA, get colour 2 ORed in (bit
// 1 of each pixel): the first byte's last 1-4 pixels, whole bytes, the last byte's first 0-3.
void spotlight_beam_cga(u16 es, u16 bx, u16 dx)
{
    do {
        if (ds_u8(BEAM_ROW) >= 0x40) return;
        const u8 width = seg_u8(CSSEG_spotlight_widths, bx);
        u16 di = u16(view_row(ds_u8(BEAM_ROW)) - 8);
        u16 ax = width;
        u16 cx = u16(-ax + 0xA0);
        if (cx != 0) {
            cx = u16(cx << 1);
            ax = u16(ax + dx - 0x20);
            bool draw = true;
            if (!(ax & 0x8000)) {
                if (ax >> 8) {
                    draw = false;
                } else {
                    ax = u16(ax + cx);
                    if (ax >> 8) {
                        cx = u16((cx & 0xFF00) | u8(u8(cx) - u8(ax)));
                        ax = 0x100;
                    }
                    ax = u16(ax - cx);
                }
            } else {
                ax = u16(ax + cx);
                if (ax & 0x8000) {
                    draw = false;
                } else {
                    ax = u16(ax - cx);
                    cx = u16(cx + ax);
                    ax = 0;
                }
            }
            if (draw) {
                ax = u16(ax + 0x20);
                const u16 head = u8(4 - (ax & 3));
                di = u16(di + (ax >> 2));
                if (cx > head) {
                    mem_u8(es, di) |= ds_u8(u16(DS_cga_beam_head - 1 + head));
                    di = u16(di + 1);
                    cx = u16(cx - head);
                    while (u8(cx) >= 4) {
                        mem_u8(es, di) |= 0xAA;
                        di = u16(di + 1);
                        cx = u16((cx & 0xFF00) | u8(u8(cx) - 4));
                    }
                    mem_u8(es, di) |= ds_u8(u16(DS_cga_beam_tail + u8(cx)));
                }
            }
        }
        bx = u16(bx + 1);
        ds_u8(BEAM_ROW)++;
    } while (--ds_u8(BEAM_ROWS) != 0);
}

// 0919:4587 sky_water_cga (render3d.md §3.1): sky_water_vga in mode 4. From view row CL to BL - 1
// the sky: the colour byte of D94F's low two bits, or AAh (colour 2) while the flash counter D9B5
// is not zero (after terrain_setup's decrement; AX, the sky colour terrain_setup chose, is not
// used); two rows of colour 2 (the horizon line); then water to row 63: D950's colour byte, or FFh
// during a flash. The horizon D953 is decremented after use. Returns DI = the horizon line's row.
u16 sky_water_cga(u16 es, u16, u8 bl, u8 cl)
{
    u16 di = view_row(cl);
    u8 c = ds_u8(u16(DS_cga_colour_byte + (ds_u8(DS_scene_colours) & 3)));
    u16 ax = u16(c << 8 | c);
    if (ds_u8(DS_screen_shake) != 0) ax = 0xAAAA;
    auto fill = [&](u16 v) {  // rep stosw, 20h words, then the next row
        for (int k = 0; k < 0x20; k++) {
            mem_u8(es, di) = u8(v);
            mem_u8(es, u16(di + 1)) = u8(v >> 8);
            di = u16(di + 2);
        }
        di = bank_down(u16(di - 0x40));
    };
    bl = u8(bl - cl);
    if (bl != 0) {
        do fill(ax);
        while (--bl != 0);
    }
    bl = u8(0x40 - ds_u8(DS_horizon_row));
    ds_u8(DS_horizon_row)--;
    bl--;
    c = ds_u8(u16(DS_cga_colour_byte + (ds_u8(u16(DS_scene_colours + 1)) & 3)));
    ax = u16(c << 8 | c);
    if (ds_u8(DS_screen_shake) != 0) ax = 0xFFFF;
    const u16 horizon_di = di;
    fill(0xAAAA);
    fill(0xAAAA);
    bl--;
    do fill(ax);
    while (--bl != 0);
    return horizon_di;
}

// 0919:4639 water_marks_cga (render3d.md §3.2): water_marks_vga in mode 4: one mark per row from
// DI down, CL rows, mark BX (wrapping at 20h) at column D90D[mark]. Its size: 0 (one pixel) for
// the far rows (CL > 16h) and marks younger than 0Bh, else 1, and for CL <= 0Eh with age >= 11h 2,
// 3 (age >= 17h) or 4 (age >= 1Bh); the size's four pixel offsets (cga_mark_offsets: x + 140h per
// row, the row above or below found in the other bank) are ORed with colour 3. AX and DX are not
// used (the colours are fixed); SI is changed (not used by the callers). A row at or past row pair
// 3Fh + the column (DI & 1FFFh >= 13DDh) ends it.
void water_marks_cga(u16 es, u16, u16 bx, u16 cx, u16, u16 di)
{
    u8 cl = u8(cx);
    do {
        if ((di & 0x1FFF) >= 0x13DD) return;
        u8 size = 0;
        if (cl <= 0x16) {
            const u8 age = ds_u8(u16(DS_water_mark_age + bx));
            if (age >= 0x0B) {
                size = 1;
                if (cl <= 0x0E && age >= 0x11) size = age < 0x17 ? 2 : age < 0x1B ? 3 : 4;
            }
        }
        const u16 x = ds_u8(u16(DS_water_mark_x + bx));
        u16 si = u16(u16((bx & 0xFF00) | u8(size * 5)) << 1);
        for (int k = 0; k < 4; k++) {
            const u16 off = ds_u16(u16(DS_cga_mark_offsets + si));
            u16 a = u16(x + off), d = di;
            if (off & 0x8000) {  // the row above
                d ^= 0x2000;
                if (!(d & 0x2000)) a = u16(a + 0x140);
            } else if (off >= 0x140) {  // the row below
                d ^= 0x2000;
                if (d & 0x2000) a = u16(a - 0x140);
            }
            const u8 pixel = u8(a) & 3;
            d = u16(d + u16(s16(a) >> 2));
            mem_u8(es, d) |= ds_u8(u16(DS_cga_pixel_mask + pixel));
            si = u16(si + 2);
        }
        di = bank_down(di);
        bx = u16(bx + 1);
        bx = u16((bx & 0xFF00) | (bx & 0x1F));
    } while (--cl != 0);
}

// 0919:46e8 span_cga_a (render3d.md §3.4; [DS:D8F8] in CGA): span_vga_a in mode 4: the rows of a
// triangle half from row D8FC (screen rows; the view's 64 from 40h through view_row_table), B7E2
// rows, left edge D956 and right edge D958 stepped by D95A/D95C per row, in the colour byte of
// D954's low two bits. Rows whose D8FC - 40h has bit 7 set are skipped (above the view, and rows
// C0h-FFh); the first row from 80h to BFh ends it. D8FC is advanced per row (VGA leaves it).
void span_cga_a(u16 es)
{
    const u8 dh = span_colour();
    do {
        const u8 row = u8(ds_u8(DS_span_first_row) - 0x40);
        if (!(row & 0x80)) {
            if (row >= 0x40) return;
            const u16 di = view_row(row);
            const u16 ax = span_column(ds_u16(DS_span_left));
            const u16 cx = u16((span_column(ds_u16(DS_span_right)) - ax) & 0xFF);
            span_fill(es, di, ax, cx, dh);
        }
        ds_u8(DS_span_first_row)++;
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - ds_u16(DS_span_step_left));
        ds_u16(DS_span_right) = u16(ds_u16(DS_span_right) - ds_u16(DS_span_step_right));
    } while (--ds_u8(SPAN_ROWS) != 0);
}

// 0919:47cf span_cga_b (render3d.md §3.4; [DS:D8FA] in CGA): span_vga_b in mode 4: a line from row
// D8FC, B7E2 rows, one span per row between the edge D956 (+40h) and where it will be on the next
// row (a width of 0 becomes 1), rows as span_cga_a; the step D95A becomes +-80h for the last row.
void span_cga_b(u16 es)
{
    const u8 dh = span_colour();
    do {
        const u8 row = u8(ds_u8(DS_span_first_row) - 0x40);
        if (!(row & 0x80)) {
            if (row >= 0x40) return;
            const u16 di = view_row(row);
            const u16 edge = u16(ds_u16(DS_span_left) + 0x40);
            u16 ax = span_column(edge);
            u16 cx = u16(span_column(u16(edge - ds_u16(DS_span_step_left))) - ax);
            if (cx == 0) cx = 1;
            cx &= 0x1FF;
            if (cx >> 8) {
                ax = u16((ax + cx) & 0x1FF);
                cx = u16(-cx & 0xFF);
            }
            span_fill(es, di, ax, cx, dh);
        }
        ds_u8(DS_span_first_row)++;
        const u16 step = ds_u16(DS_span_step_left);
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - step);
        if (ds_u8(SPAN_ROWS) == 2 && step != 0) ds_u16(DS_span_step_left) = (step & 0x8000) ? 0xFF80 : 0x0080;
    } while (--ds_u8(SPAN_ROWS) != 0);
}

} // namespace gb
