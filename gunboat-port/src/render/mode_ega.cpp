// The renderer's EGA (mode 0Dh) routines (render/modes.hpp, render3d.md §1.4): the twins of the VGA
// sky and water, water marks, triangle and line spans, spotlight beam and sprite rows.
//
// In EGA the pages are the card's memory (A000h + 200h per page: page 1, the 3D view's, is A200h),
// 40 bytes per row in each of the four planes; the view's 64 rows start at view_row_table (D74D,
// 0A05h + 28h per row: row 64, column 40). The routines draw through the graphics controller: the
// colour in set/reset (index 0; terrain_frame and object_frame enable it on every plane), the pixels
// by the bit mask (index 8), each byte by a read-modify-write instruction (OR/AND ES:[DI], AL) whose
// read loads the latches, so the pixels outside the mask keep their colour. Every access through ES
// or a far segment goes through vmem_read/vmem_write (the card on an EGA machine), every OUT through
// card_out16.
#include "render/modes.hpp"

#include "mem.hpp"
#include "platform/card.hpp"
#include "render/render.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// The sprite row copier's first screen row (B7E2) and its current bit (B7E3).
constexpr u16 BLIT_ROW = DS_scratch_b7e2;
constexpr u16 BLIT_BIT = DS_scratch_b7e3;
// B7F8: the source row width of the sprite being placed (blit_place).
constexpr u16 SPRITE_WIDTH = DS_shot_elevation;
// B7E2 counts the rows left while a span routine runs.
constexpr u16 SPAN_ROWS = DS_scratch_b7e2;
// The spotlight beam's row (B7E2) and rows left (B7E3).
constexpr u16 BEAM_ROW = DS_scratch_b7e2;
constexpr u16 BEAM_ROWS = DS_scratch_b7e3;

constexpr u16 GC = 0x3CE;   // graphics controller: index, data
constexpr u16 SEQ = 0x3C4;  // sequencer: index, data

u8 ror8(u8 v, u8 n)
{
    n &= 7;
    return n ? u8(v >> n | v << (8 - n)) : v;
}

// OR ES:[DI], AL: the read loads the latches, the write goes through the graphics controller.
void or_byte(u16 es, u16 di, u8 al) { vmem_write(es, di, u8(vmem_read(es, di) | al)); }

// REP STOSB (DF = 0): CX bytes AL from ES:DI on.
void stosb(u16 es, u16 &di, u8 al, u16 cx)
{
    for (; cx; cx--) {
        vmem_write(es, di, al);
        di = u16(di + 1);
    }
}

// A span edge's column (0..1FFh): bits 7-15 of the bearing (xchg ah, al / rol ax, 1 / and ah, 1).
u16 span_column(u16 v) { return u16(v >> 7 & 0x1FF); }

// The pixels of one span row (0919:4e42-4e97, 4f12-4f67): from view column AX (0..FFh) CX pixels,
// clipped at column 100h, into the row at DI with the bit mask: the first byte's pixels by
// mask_low_bits (a span inside that byte by the bits between its ends), whole bytes by REP STOSB,
// the last byte's by mask_high_bits. AL = 8 is the bit mask's index and the byte written.
void span_pixels(u16 es, u16 di, u16 ax, u16 cx)
{
    u16 bx = ax;
    di = u16(di + (ax >> 3));
    if (u8(bx) + u8(cx) > 0xFF) cx = u16(0x100 - bx);  // mov ah, bl / add ah, cl / jae
    if (bx & 7) {
        bx = u16(8 - (bx & 7));  // the pixels of the first byte
        u8 ah = ds_u8(u16(DS_mask_low_bits + bx));
        cx = u16(cx - bx);
        if (cx == 0 || (cx & 0x8000)) {
            if (cx & 0x8000) {  // the span ends in the first byte: its last -CX pixels off
                cx = u16(-cx);
                ah ^= ds_u8(u16(DS_mask_low_bits + cx));
            }
            card_out16(GC, u16(ah << 8 | 8));
            or_byte(es, di, 8);
            return;
        }
        card_out16(GC, u16(ah << 8 | 8));
        or_byte(es, di, 8);
        di = u16(di + 1);
    }
    card_out16(GC, 0xFF08);
    bx = cx;
    stosb(es, di, 8, u16(cx >> 3));
    bx &= 7;
    if (bx == 0) return;
    card_out16(GC, u16(ds_u8(u16(DS_mask_high_bits + bx)) << 8 | 8));
    or_byte(es, di, 8);
}

// The row of a span routine: D8FC less 40h; the rows whose bit 6 is set are skipped (the others
// index view_row_table by twice the row in 8 bits: rows C0h-FFh of D8FC land on the view's rows
// 0-3Fh). Returns false for a skipped row.
bool span_row(u16 &di)
{
    const u8 r2 = u8(ds_u8(DS_span_first_row) << 1);  // shl bl, 1 / js
    if (r2 & 0x80) return false;
    di = ds_u16(u16(DS_view_row_table + r2));
    return true;
}

// The clip of a span's start AX (0..1FFh) with the width CX (0919:4e30-4e40, 4f00-4f10): a start
// past the view's 256 columns is off it unless it is 180h or more, a part left of the view
// wrapped round (AX = FFxxh): the width is cut by it, the span starts at 0 or is off the view.
bool span_clip(u16 &ax, u16 &cx)
{
    if (!(ax >> 8)) return true;
    if (ax < 0x180) return false;
    ax = u16(0xFF00 | u8(ax));
    cx = u16(cx + ax);
    if (cx & 0x8000) return false;
    ax = 0;
    return true;
}

} // namespace

// 0919:48da blit_rows_ega (render3d.md §1.4, §5.7): the sprite rows of blit_place from view row AL
// (screen row AL + 30h, B7E2), at pixel column 2 x B7F7 + D873 (28h when clipped at the left, the
// source then skipping D875 columns), D862 pixels per row (65536 for 0, the LOOP count), from the
// cache record in D887 at SI. Each nonzero pixel is written alone: set/reset = its colour, bit mask
// = its bit (B7E3 = 80h >> (column & 7) at the row's start), AND ES:[DI], 8. Each source row is
// repeated by its count at D87B; screen row 80h or beyond ends it (checked before the first row too).
void blit_rows_ega(u8 al, u16 si)
{
    al = u8(al + 0x30);
    ds_u8(BLIT_ROW) = al;
    const u16 ax = u16(al * 0x28);  // mul ah (AH = 28h)
    u16 bx = u16(u16(ds_u8(DS_sprite_column) << 1) + ds_u16(DS_sprite_half_column));
    if (ds_u16(DS_sprite_clip_left) != 0) {
        bx = 0x28;
        si = u16(si + ds_u16(DS_sprite_clip_left));
    }
    ds_u8(BLIT_BIT) = ror8(1, u8((bx & 7) + 1));
    u16 di = u16(ax + (bx >> 3));
    if (ds_u8(BLIT_ROW) >= 0x80) return;
    do {
        const u16 es = ds_u16(DS_draw_page_segment);
        const u16 record = ds_u16(DS_sprite_segment);
        u8 bit = ds_u8(BLIT_BIT);
        u16 s = si, d = di;
        u16 cx = ds_u8(DS_sprite_blit_width);
        do {
            const u8 px = vmem_read(record, s);
            s = u16(s + 1);
            if (px != 0) {
                card_out16(GC, u16(px << 8));        // set/reset: the pixel's colour
                card_out16(GC, u16(bit << 8 | 8));   // bit mask: its bit
                vmem_write(es, d, u8(vmem_read(es, d) & 8));  // and es:[di], al
            }
            const bool carry = bit & 1;  // ror bl, 1: the next byte after bit 0
            bit = ror8(bit, 1);
            if (carry) d = u16(d + 1);
        } while (--cx != 0);
        di = u16(di + 0x28);
        ds_u8(BLIT_ROW)++;
        if (--ds_u8(DS_sprite_row_repeat) == 0) {
            ds_u16(DS_sprite_repeat_ptr)++;
            ds_u8(DS_sprite_row_repeat) = vmem_read(ds_u16(DS_sprite_segment), ds_u16(DS_sprite_repeat_ptr));
            si = u16(si + ds_u8(SPRITE_WIDTH));
        }
        if (ds_u8(BLIT_ROW) >= 0x80) return;
    } while (--ds_u8(DS_sprite_rows_left) != 0);
}

// 0919:4989 ega_gc_setup (hud.md §6.1): map mask 0Fh, write mode 0, bit mask 0 (every write stores
// the latches the last read loaded: the view copies move all four planes a byte at a time),
// function replace, rotate 0.
void ega_gc_setup()
{
    card_out16(SEQ, 0x0F02);
    card_out16(GC, 0x0005);
    card_out16(GC, 0x0008);
    card_out16(GC, 0x0003);
}

// 0919:4c18 spotlight_beam_ega (render3d.md §1.4, §6): the beam of spotlight_beam_vga on plane 3
// only (map mask 8, set/reset 8 on every plane): the covered pixels get bit 3 set (VGA: only where
// bit 4 is clear). Ten rows from B7E2 (ending at view row 40h), each 2 x (A0h - width) pixels at
// view column DX + width - 20h (the widths from CS:709A at BX), clipped to the view as in VGA; a row
// that fits in its first byte is not drawn (CMP CX, BX / JBE). The map mask is 0Fh again at the end.
void spotlight_beam_ega(u16 es, u16 bx, u16 dx)
{
    card_out16(SEQ, 0x0802);
    card_out16(GC, 0x0005);
    card_out16(GC, 0x0F01);
    card_out16(GC, 0x0800);
    while (ds_u8(BEAM_ROW) < 0x40) {
        u16 ax = seg_u8(CSSEG_spotlight_widths, bx);
        u16 di = u16(ds_u16(u16(DS_view_row_table + 2 * ds_u8(BEAM_ROW))) - 4);
        u16 cx = u16(-ax + 0xA0);
        if (cx != 0) {
            cx = u16(cx << 1);
            ax = u16(ax + dx - 0x20);
            bool draw = true;
            if (ax & 0x8000) {
                if (u16(ax + cx) & 0x8000) {
                    draw = false;
                } else {
                    cx = u16(cx + ax);
                    ax = 0;
                }
            } else if (ax >> 8) {
                draw = false;
            } else {
                ax = u16(ax + cx);
                if (ax >> 8) {
                    cx = u16((cx & 0xFF00) | u8(u8(cx) - u8(ax)));
                    ax = 0x100;
                }
                ax = u16(ax - cx);
            }
            if (draw) {
                ax = u16(ax + 0x20);
                const u16 first = u16(8 - (ax & 7));  // the pixels of the first byte
                if (cx > first) {
                    cx = u16(cx - first);
                    di = u16(di + (ax >> 3));
                    card_out16(GC, u16(ds_u8(u16(DS_mask_low_bits + first)) << 8 | 8));
                    or_byte(es, di, 8);
                    di = u16(di + 1);
                    card_out16(GC, 0xFF08);
                    for (; cx >= 8; cx = u16(cx - 8)) {
                        or_byte(es, di, 8);
                        di = u16(di + 1);
                    }
                    if (cx != 0) {
                        card_out16(GC, u16(ds_u8(u16(DS_mask_high_bits_ff + cx)) << 8 | 8));
                        or_byte(es, di, 8);
                    }
                }
            }
        }
        bx = u16(bx + 1);
        ds_u8(BEAM_ROW)++;
        if (--ds_u8(BEAM_ROWS) == 0) break;
    }
    card_out16(SEQ, 0x0F02);
}

// 0919:4ce2 sky_water_ega (render3d.md §1.4, §3.1): sky_water_vga on the card: map mask 0Fh, write
// mode 0, set/reset on every plane, bit mask FFh; rows CL to BL - 1 of the view (from
// view_row_table[CL], 32 bytes a row) in the sky colour AH, two rows of colour 8, then the water D950
// (0Eh during a flash D9B5) to row 63, each set by set/reset and written by REP STOSB. D953 is
// decremented after use. Returns DI = the address of the horizon line's first row.
u16 sky_water_ega(u16 es, u16 ax, u8 bl, u8 cl)
{
    card_out16(SEQ, 0x0F02);
    card_out16(GC, 0x0005);
    card_out16(GC, 0x0F01);
    card_out16(GC, 0xFF08);
    card_out16(GC, u16(ax & 0xFF00));
    u16 di = ds_u16(u16(DS_view_row_table + 2 * cl));
    bl = u8(bl - cl);
    if (bl != 0) {
        do {
            stosb(es, di, 0, 0x20);
            di = u16(di + 8);
        } while (--bl != 0);
    }
    bl = u8(0x40 - ds_u8(DS_horizon_row));
    ds_u8(DS_horizon_row)--;
    bl--;
    const u16 horizon = di;
    card_out16(GC, 0x0800);
    stosb(es, di, 0, 0x20);
    di = u16(di + 8);
    stosb(es, di, 0, 0x20);
    di = u16(di + 8);
    u8 water = ds_u8(u16(DS_scene_colours + 1));
    if (ds_u8(DS_screen_shake) != 0) water = 0x0E;
    card_out16(GC, u16(water << 8));
    bl--;
    do {
        stosb(es, di, 0, 0x20);
        di = u16(di + 8);
    } while (--bl != 0);
    return horizon;
}

// 0919:4d64 water_marks_ega (render3d.md §1.4, §3.2): the marks of water_marks_vga in one colour, AL
// (set/reset; AH and DX are not used), each of five pixels OR'ed through the bit mask: mark BX
// (wrapping at 20h) at column D90D[mark] plus the offsets water_mark_patterns[size] (320 per row,
// divided by 8 into bytes and rows of 40, the bit from the low 3 bits); size 0 (the farthest rows,
// CL > 16h, or younger than 0Bh: all five on one pixel), 1 from age 0Bh, and for CL <= 0Eh 2, 3, 4
// from ages 11h, 17h, 1Bh. CX rows from DI (CL counts down); a row from 13DDh (view row 63) on ends
// it.
void water_marks_ega(u16 es, u16 ax, u16 bx, u16 cx, u16, u16 di)
{
    card_out16(GC, u16(u8(ax) << 8));
    card_out16(GC, 0x0005);
    card_out16(GC, 0x0F01);
    card_out16(SEQ, 0x0F02);
    for (;;) {
        if (di >= 0x13DD) return;
        u8 size = 0;
        const u8 cl = u8(cx);
        if (cl <= 0x16) {
            const u8 age = ds_u8(u16(DS_water_mark_age + bx));
            if (age >= 0x0B) {
                size = 1;
                if (cl <= 0x0E && age >= 0x11) {
                    if (age >= 0x17) {
                        size = 2;
                        if (age >= 0x1B) size = 3;
                    }
                    size++;
                }
            }
        }
        const u16 x = ds_u8(u16(DS_water_mark_x + bx));
        // mov bl, ch / shl ch, 1 (twice) / add bl, ch / shl bx, 1: BH is the caller's (0)
        u16 b = u16(u16((bx & 0xFF00) | u8(size + u8(size << 2))) << 1);
        u16 si = b;
        for (u8 ch = 5; ch; ch--) {
            const u16 a = u16(x + ds_u16(u16(DS_water_mark_patterns + si)));
            b = u16((b & 0xFF00) | (a & 7));
            const u16 d = u16(di + u16(s16(a) >> 3));
            card_out16(GC, u16(ds_u8(u16(DS_pixel_bit + b)) << 8 | 8));
            or_byte(es, d, 8);
            si = u16(si + 2);
        }
        di = u16(di + 0x28);
        bx = u16(bx + 1);
        bx = u16((bx & 0xFF00) | (bx & 0x1F));
        cx = u16(u8(cx) - 1);
        if (cx == 0) return;
    }
}

// 0919:4dfa span_ega_a (render3d.md §1.4, §3.4; [DS:D8F8] in EGA): span_vga_a on the card:
// set/reset = the colour D954, then per row (D8FC, B7E2 rows) the span between the edges D956 and
// D958 (their columns, the width in 8 bits) clipped like VGA's, drawn by span_pixels. D8FC is
// lowered by 40h and advanced per row; its rows with bit 6 set (after the 40h) are skipped, the
// rows past the view are not an end as in VGA (see span_row).
void span_ega_a(u16 es)
{
    card_out16(GC, u16(ds_u8(DS_span_colour) << 8));
    ds_u8(DS_span_first_row) = u8(ds_u8(DS_span_first_row) - 0x40);
    do {
        u16 di;
        if (span_row(di)) {
            u16 ax = span_column(ds_u16(DS_span_left));
            u16 cx = u16((span_column(ds_u16(DS_span_right)) - ax) & 0xFF);
            if (span_clip(ax, cx)) span_pixels(es, di, ax, cx);
        }
        ds_u8(DS_span_first_row)++;
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - ds_u16(DS_span_step_left));
        ds_u16(DS_span_right) = u16(ds_u16(DS_span_right) - ds_u16(DS_span_step_right));
    } while (--ds_u8(SPAN_ROWS) != 0);
}

// 0919:4eb6 span_ega_b (render3d.md §1.4, §3.4; [DS:D8FA] in EGA): span_vga_b on the card: a line
// drawn as one span per row between the edge D956 (+40h) and where it will be on the next row (at
// least one pixel: a zero width becomes 1), the rows as in span_ega_a; the step D95A becomes +-80h
// for the last row.
void span_ega_b(u16 es)
{
    card_out16(GC, u16(ds_u8(DS_span_colour) << 8));
    ds_u8(DS_span_first_row) = u8(ds_u8(DS_span_first_row) - 0x40);
    do {
        u16 di;
        if (span_row(di)) {
            const u16 edge = u16(ds_u16(DS_span_left) + 0x40);
            u16 ax = span_column(edge);
            u16 cx = u16(span_column(u16(edge - ds_u16(DS_span_step_left))) - ax);
            if (cx == 0) cx = 1;
            cx &= 0x1FF;
            if (cx >> 8) {
                ax = u16((ax + cx) & 0x1FF);
                cx = u16(-cx & 0xFF);
            }
            if (span_clip(ax, cx)) span_pixels(es, di, ax, cx);
        }
        ds_u8(DS_span_first_row)++;
        const u16 step = ds_u16(DS_span_step_left);
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - step);
        if (ds_u8(SPAN_ROWS) == 2 && step != 0) ds_u16(DS_span_step_left) = (step & 0x8000) ? 0xFF80 : 0x0080;
    } while (--ds_u8(SPAN_ROWS) != 0);
}

} // namespace gb
