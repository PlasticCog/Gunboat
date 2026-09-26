// The renderer's Tandy (mode 9) routines (render/modes.hpp, render3d.md §11): the twins of the VGA
// sprite row copier, spotlight beam, sky and water, water marks and span fillers.
//
// Tandy pages (the screen at B800h and the RAM pages alike) hold 320 x 200 pixels of 4 bits, two to
// a byte (the left pixel in the high nibble), 160 bytes a row, in four banks 2000h apart: row r is at
// 2000h * (r & 3) + A0h * (r >> 2). The view's rows start at view_row_table (D74D, video_mode_setup:
// row 64, column 40 = 0A14h); the next row is + 2000h, masked to 7FFFh, + A0h when that wrapped
// back to bank 0. The routines never touch the video ports.
#include "render/modes.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// B7E2/B7E3: the sprite's screen row and pixel parity (blit_rows_tandy), the beam's row and rows
// left (spotlight_beam_tandy), the rows left (the span routines).
constexpr u16 BLIT_ROW = DS_scratch_b7e2;
constexpr u16 BLIT_PARITY = DS_scratch_b7e3;
constexpr u16 BEAM_ROW = DS_scratch_b7e2;
constexpr u16 BEAM_ROWS = DS_scratch_b7e3;
constexpr u16 SPAN_ROWS = DS_scratch_b7e2;
// B7F8: the width of the sprite's cache record (blit_place).
constexpr u16 SPRITE_WIDTH = DS_shot_elevation;

u8 rol8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v << n | v >> (8 - n)) : v;
}

// DI + n in the next bank (n = 2000h: the row below DI), and 7FFFh, + A0h back in bank 0.
u16 bank_step(u16 di, u16 n)
{
    di = u16((di + n) & 0x7FFF);
    if (!(di & 0x6000)) di = u16(di + 0xA0);
    return di;
}

// A row of the view (128 bytes) filled with the word AX (REP STOSW, 40h words); returns the start
// of the next row (+ 1F80h).
u16 fill_view_row(u16 es, u16 di, u16 ax)
{
    for (u16 cx = 0x40; cx; cx--) {
        mem_u8(es, di) = u8(ax);
        mem_u8(es, u16(di + 1)) = u8(ax >> 8);
        di = u16(di + 2);
    }
    return bank_step(di, 0x1F80);
}

// The column of a span edge bearing (0..1FFh): xchg ah, al / rol ax, 1 / and ah, 1.
u16 span_column(u16 v) { return u16(v >> 7 & 0x1FF); }

// The span of both span routines (0919:56cf-5739, 57a6-5810): column AX (0..1FFh; a wrapped left
// part from 180h) and the pixel count CX (0..FFh) in the colour pair DH, on the row at DI. A start in
// the right half of a byte draws that nibble first (and nothing for a count of 0), then whole bytes
// (words, then a byte), then a last left nibble for an odd rest.
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
    di = u16(di + (ax >> 1));
    if (u8(bx) + u8(cx) > 0xFF) cx = u16(0x100 - bx);
    if (bx & 1) {  // shr bl, 1: the right nibble first
        cx = u16(cx - 1);
        if (cx & 0x8000) return;
        mem_u8(es, di) = u8((mem_u8(es, di) & 0xF0) | (0x0F & dh));
        di = u16(di + 1);
        if (cx == 0) return;
    }
    bx = cx;
    cx >>= 1;
    if (cx != 0) {
        const bool byte = cx & 1;
        cx >>= 1;
        for (; cx; cx--) {
            mem_u8(es, di) = dh;
            mem_u8(es, u16(di + 1)) = dh;
            di = u16(di + 2);
        }
        if (byte) {
            mem_u8(es, di) = dh;
            di = u16(di + 1);
        }
    }
    if (bx & 1) mem_u8(es, di) = u8((mem_u8(es, di) & 0x0F) | (0xF0 & dh));
}

} // namespace

// 0919:4f96 blit_rows_tandy (render3d.md §11.1): the Tandy twin of blit_rows_vga. Screen row B7E2 =
// AL + 30h of the drawing page D9B8 (the first address: AL * 28h, + 1FD8h for an odd row, + 3FB0h
// for rows 2 and 3 of a bank group), pixel column 2 x B7F7 + D873 (40 when clipped at the left, the
// source then skipping D875 columns; its parity in B7E3); D862 pixels a row from the cache record in
// D887 at SI, a byte each: zero is transparent, else its low nibble replaces the pixel's nibble
// (the shift and the kept nibble from D848/D84A by the parity). Each source row is repeated by its
// count (D86F counts down in memory, the next count from D87B). It stops at screen row 128.
void blit_rows_tandy(u8 al, u16 si)
{
    al = u8(al + 0x30);
    ds_u8(BLIT_ROW) = al;
    u16 ax = u16(al * 0x28);
    if (ds_u8(BLIT_ROW) & 1) ax = u16(ax + 0x1FD8);
    if (ds_u8(BLIT_ROW) & 2) ax = u16(ax + 0x3FB0);
    u16 bx = u16(u16(ds_u8(DS_sprite_column) << 1) + ds_u16(DS_sprite_half_column));
    if (ds_u16(DS_sprite_clip_left) != 0) {
        bx = 0x28;
        si = u16(si + ds_u16(DS_sprite_clip_left));
    }
    ds_u8(BLIT_PARITY) = u8(bx & 1);
    u16 di = u16(ax + (bx >> 1));
    if (ds_u8(BLIT_ROW) >= 0x80) return;
    do {
        const u16 es = ds_u16(DS_draw_page_segment);
        u16 d = di, s = si;
        u8 parity = ds_u8(BLIT_PARITY);
        u8 ch = ds_u8(DS_sprite_blit_width);
        do {
            u8 px = mem_u8(ds_u16(DS_sprite_segment), s);
            s = u16(s + 1);
            if (px != 0) {
                px &= 0x0F;
                const u8 kept = u8(mem_u8(es, d) & ds_u8(u16(DS_tandy_nibble_keep + parity)));
                px = rol8(px, ds_u8(u16(DS_tandy_nibble_shift + parity)));
                mem_u8(es, d) = u8(kept | px);
            }
            parity ^= 1;
            if (parity == 0) d = u16(d + 1);
        } while (--ch != 0);
        di = u16(di + 0x2000);
        ds_u8(BLIT_ROW)++;
        if ((ds_u8(BLIT_ROW) & 3) == 0) di = u16(di - 0x7F60);
        const u16 record = ds_u16(DS_sprite_segment);
        if (--ds_u8(DS_sprite_row_repeat) == 0) {
            ds_u16(DS_sprite_repeat_ptr)++;
            ds_u8(DS_sprite_row_repeat) = mem_u8(record, ds_u16(DS_sprite_repeat_ptr));
            si = u16(si + ds_u8(SPRITE_WIDTH));
        }
        if (ds_u8(BLIT_ROW) >= 0x80) return;
    } while (--ds_u8(DS_sprite_rows_left) != 0);
}

// 0919:5494 spotlight_beam_tandy (render3d.md §11.2): the Tandy twin of spotlight_beam_vga: ten rows
// (B7E3) from view row B7E2 (it ends at row 40h), each 2 * (A0h - width byte) pixels around column
// DX + width - 20h from column 8 (the widths from CS:BX), clipped to the view as the VGA beam: the
// covered pixels get bit 3 set (OR 88h a byte; a start on a right nibble ORs D84C = 08h, else D84D
// = 88h, and the end ORs D84E/D84F = 00h/80h by the rest's parity).
void spotlight_beam_tandy(u16 es, u16 bx, u16 dx)
{
    do {
        const u8 r = ds_u8(BEAM_ROW);
        if (r >= 0x40) return;
        u16 ax = seg_u8(CSSEG_spotlight_widths, bx);
        u16 di = u16(ds_u16(u16(DS_view_row_table + 2 * r)) - 0x10);
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
                di = u16(di + (ax >> 1));
                const u16 first = u16(u8(-u8(ax & 1) + 2));  // 2 on a left nibble, 1 on a right one
                if (cx > first) {
                    mem_u8(es, di) |= ds_u8(u16(DS_tandy_beam_start - 1 + first));
                    di = u16(di + 1);
                    cx = u16(cx - first);
                    while (u8(cx) >= 2) {
                        mem_u8(es, di) |= 0x88;
                        di = u16(di + 1);
                        cx = u16((cx & 0xFF00) | u8(u8(cx) - 2));
                    }
                    mem_u8(es, di) |= ds_u8(u16(DS_tandy_beam_end + u8(cx)));
                }
            }
        }
        bx = u16(bx + 1);
        ds_u8(BEAM_ROW)++;
    } while (--ds_u8(BEAM_ROWS) != 0);
}

// 0919:5529 sky_water_tandy (render3d.md §11.3): the Tandy twin of sky_water_vga: from view row CL
// to BL - 1 the sky (the colour pair D852[AL & 0Fh]), two rows of colour 8 (the horizon line), then
// the water D852[D950 & 0Fh] (EEh during a flash D9B5) to row 63; D953 is decremented after use.
// Returns DI = the address of the horizon line's first row.
u16 sky_water_tandy(u16 es, u16 ax, u8 bl, u8 cl)
{
    u16 di = ds_u16(u16(DS_view_row_table + 2 * cl));
    u8 c = ds_u8(u16(DS_tandy_colour_pairs + (ax & 0x0F)));
    ax = u16(c << 8 | c);
    bl = u8(bl - cl);
    if (bl != 0) {
        do di = fill_view_row(es, di, ax);
        while (--bl != 0);
    }
    bl = u8(0x40 - ds_u8(DS_horizon_row));
    ds_u8(DS_horizon_row)--;
    bl--;
    const u16 horizon_di = di;
    di = fill_view_row(es, di, 0x8888);
    di = fill_view_row(es, di, 0x8888);
    c = ds_u8(u16(DS_tandy_colour_pairs + (ds_u8(u16(DS_scene_colours + 1)) & 0x0F)));
    if (ds_u8(DS_screen_shake) != 0) c = 0xEE;
    ax = u16(c << 8 | c);
    bl--;
    do di = fill_view_row(es, di, ax);
    while (--bl != 0);
    return horizon_di;
}

// 0919:55da water_marks_tandy (render3d.md §11.4): one water mark per row from DI down, CL rows,
// mark BX (wrapping at 20h): four pixels at the column D90D[mark] plus the offsets of the pattern
// by its size (D816 + 10 x size, in pixels, +-140h a row; 0: one pixel four times; the size grows
// with the age as the VGA marks do: 1 from age 0Bh when CL <= 16h, 2..4 from ages 11h / 17h / 1Bh
// when CL <= 0Eh), each ORed with 70h / 07h by its parity (D850). A row past the view (bank offset
// from 13DDh) ends it. AX and DX are not used.
void water_marks_tandy(u16 es, u16, u16 bx, u16 cx, u16, u16 di)
{
    do {
        if ((di & 0x1FFF) >= 0x13DD) return;
        u8 size = 0;
        const u8 cl = u8(cx);
        if (cl <= 0x16) {
            const u8 age = ds_u8(u16(DS_water_mark_age + bx));
            if (age >= 0x0B) {
                size++;
                if (cl <= 0x0E && age >= 0x11) {
                    if (age >= 0x17) {
                        size++;
                        if (age >= 0x1B) size++;
                    }
                    size++;
                }
            }
        }
        const u16 x = ds_u8(u16(DS_water_mark_x + bx));
        u16 si = u16(((bx & 0xFF00) | u8(size * 5)) << 1);
        for (u8 n = 4; n; n--) {
            const u16 offset = ds_u16(u16(DS_water_mark_patterns + si));
            u16 ax = u16(x + offset);
            u16 d = di;
            if (offset & 0x8000) {  // a row up
                if (d & 0x6000) ax = u16(ax + 0x140);
                d = u16((d - 0x2000) & 0x7FFF);
            } else if (offset >= 0x140) {  // a row down
                d = u16((d + 0x2000) & 0x7FFF);
                if (d & 0x6000) ax = u16(ax - 0x140);
            }
            const u8 parity = u8(ax & 1);
            d = u16(d + u16(s16(ax) >> 1));
            mem_u8(es, d) |= ds_u8(u16(DS_tandy_mark_pixels + parity));
            si = u16(si + 2);
        }
        di = bank_step(di, 0x2000);
        bx = u16(bx + 1);
        bx = u16((bx & 0xFF00) | (bx & 0x1F));
        cx = u16((cx & 0xFF00) | u8(cl - 1));
    } while (u8(cx) != 0);
}

// 0919:5693 span_tandy_a (render3d.md §11.5; [DS:D8F8] in Tandy): the twin of span_vga_a: the rows of
// a triangle half from row D8FC (counted up in memory), B7E2 rows, between the edges D956 and D958
// stepped by D95A/D95C per row, in the colour pair D852[D954 & 0Fh]. Rows above the view (D8FC <
// 40h) are skipped; the first row past it (D8FC from 80h up to BFh) ends it.
void span_tandy_a(u16 es)
{
    const u8 dh = ds_u8(u16(DS_tandy_colour_pairs + (ds_u8(DS_span_colour) & 0x0F)));
    do {
        const u8 row = u8(ds_u8(DS_span_first_row) - 0x40);
        if (!(row & 0x80)) {
            if (row >= 0x40) return;
            const u16 di = ds_u16(u16(DS_view_row_table + 2 * row));
            const u16 ax = span_column(ds_u16(DS_span_left));
            const u16 cx = u16((span_column(ds_u16(DS_span_right)) - ax) & 0xFF);
            span_fill(es, di, ax, cx, dh);
        }
        ds_u8(DS_span_first_row)++;
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - ds_u16(DS_span_step_left));
        ds_u16(DS_span_right) = u16(ds_u16(DS_span_right) - ds_u16(DS_span_step_right));
    } while (--ds_u8(SPAN_ROWS) != 0);
}

// 0919:5756 span_tandy_b (render3d.md §11.5; [DS:D8FA] in Tandy): the twin of span_vga_b: a line from
// row D8FC, B7E2 rows, one span per row between the edge D956 (+40h) and where it will be on the
// next row (at least one pixel), in the colour pair D852[D954 & 0Fh]; the step D95A becomes +-80h
// for the last row.
void span_tandy_b(u16 es)
{
    const u8 dh = ds_u8(u16(DS_tandy_colour_pairs + (ds_u8(DS_span_colour) & 0x0F)));
    do {
        const u8 row = u8(ds_u8(DS_span_first_row) - 0x40);
        if (!(row & 0x80)) {
            if (row >= 0x40) return;
            const u16 di = ds_u16(u16(DS_view_row_table + 2 * row));
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
