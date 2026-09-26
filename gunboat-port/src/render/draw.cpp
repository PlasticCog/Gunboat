// Terrain primitives and their VGA span fillers, spotlight beams, the explosion flash and the
// screen shake (render3d.md §3.4, §6, §7).
#include "render/render.hpp"
#include "render/modes.hpp"

#include <utility>

#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// B7E2 counts the rows left while a span routine runs.
constexpr u16 SPAN_ROWS = DS_scratch_b7e2;
// The spotlight beam's row (B7E2) and rows left (B7E3).
constexpr u16 BEAM_ROW = DS_scratch_b7e2;
constexpr u16 BEAM_ROWS = DS_scratch_b7e3;
// The screen shake counter (set to 2 or 8 by boat_hit).
constexpr u16 SHAKE_COUNT = DS_shake_counter;

u16 bearing(u16 o) { return ds_u16(u16(DS_vertex_bearing + o)); }
u16 row(u16 o) { return ds_u16(u16(DS_vertex_row + o)); }
u8 row8(u16 o) { return ds_u8(u16(DS_vertex_row + o)); }

// A span edge's column (0..1FFh): bits 7-15 of the bearing (xchg ah, al / rol ax, 1 / and ah, 1).
u16 span_column(u16 v) { return u16(v >> 7 & 0x1FF); }

// The address of view row `row` + 28h as the span routines form it (bh = row: + row*256 + row*64).
u16 span_row_address(u8 row)
{
    const u16 bx = u16(row << 8);
    return u16(0x28 + bx + (bx >> 2));
}

// One span: x (0..FFh, or a wrapped left part: AX >= 180h) and the width byte CX, clipped to the
// 256 columns, filled with the colour word D954 (words, then a last byte; widths 0 and 1 fill one
// byte). Returns false if the span is off the view (0919:78c6-78ef, 798e-79b7).
void span_fill(u16 es, u16 di, u16 ax, u16 cx)
{
    if (ax >> 8) {
        if (ax < 0x180) return;
        ax = u16(0xFF00 | u8(ax));
        cx = u16(cx + ax);
        if (cx & 0x8000) return;
        ax = 0;
    }
    di = u16(di + ax);
    if (u8(ax) + u8(cx) > 0xFF) cx = u16(0x100 - ax);
    const u16 colour = ds_u16(DS_span_colour);
    const bool odd = cx & 1;
    cx >>= 1;
    if (cx != 0) {
        for (; cx; cx--) {
            mem_u8(es, di) = u8(colour);
            mem_u8(es, u16(di + 1)) = u8(colour >> 8);
            di = u16(di + 2);
        }
        if (!odd) return;
    }
    mem_u8(es, di) = u8(colour);
}

// The per-row step from an edge end to the apex over `rows` rows: the difference divided by the
// row count as a magnitude, the sign kept; no division for one row (0919:7823-785e, 791e-7939).
u16 edge_step(u16 diff, u16 rows)
{
    if (!(diff & 0x8000)) return rows ? div32_16(diff, rows) : diff;
    u16 a = u16(-diff);
    if (rows) a = div32_16(a, rows);
    return u16(-a);
}

} // namespace

// 0919:788e span_vga_a (render3d.md §3.4; [DS:D8F8] in VGA): the rows of a triangle half from row
// D8FC, B7E2 rows, left edge D956 and right edge D958 (bearings, 128 per column) stepped by
// D95A/D95C per row. Rows above the view (address < 5000h) are skipped; the first row past the
// page (A000h) ends it.
void span_vga_a(u16 es)
{
    u16 si = span_row_address(ds_u8(DS_span_first_row));
    do {
        const u16 di = si;
        if (di >= 0x5000) {
            if (di >= 0xA000) return;
            const u16 ax = span_column(ds_u16(DS_span_left));
            const u16 cx = u16((span_column(ds_u16(DS_span_right)) - ax) & 0xFF);
            span_fill(es, di, ax, cx);
        }
        si = u16(si + 0x140);
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - ds_u16(DS_span_step_left));
        ds_u16(DS_span_right) = u16(ds_u16(DS_span_right) - ds_u16(DS_span_step_right));
    } while (--ds_u8(SPAN_ROWS) != 0);
}

// 0919:7943 span_vga_b (render3d.md §3.4; [DS:D8FA] in VGA): a line from row D8FC, B7E2 rows,
// drawn as one span per row between the edge D956 (+40h) and where it will be on the next row;
// the step D95A becomes +-80h for the last row.
void span_vga_b(u16 es)
{
    u16 si = span_row_address(ds_u8(DS_span_first_row));
    do {
        const u16 di = si;
        if (di >= 0x5000) {
            if (di >= 0xA000) return;
            const u16 edge = u16(ds_u16(DS_span_left) + 0x40);
            u16 ax = span_column(edge);
            u16 cx = u16((span_column(u16(edge - ds_u16(DS_span_step_left))) - ax) & 0x1FF);
            if (cx >> 8) {
                ax = u16((ax + cx) & 0x1FF);
                cx = u16(-cx & 0xFF);
            }
            span_fill(es, di, ax, cx);
        }
        si = u16(si + 0x140);
        const u16 step = ds_u16(DS_span_step_left);
        ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) - step);
        if (ds_u8(SPAN_ROWS) == 2 && step != 0) ds_u16(DS_span_step_left) = (step & 0x8000) ? 0xFF80 : 0x0080;
    } while (--ds_u8(SPAN_ROWS) != 0);
}

// 0919:7802 fill_triangle (render3d.md §3.4): a triangle half from row AL to row AH between the
// bearings CX and DX (the flat side) and the apex D95E; D964 = 1 starts the half at the apex. Sets
// up D956/D958 (+40h/+BFh) and the steps, then calls the mode's span routine [D8F8].
void fill_triangle(u16 es, u16 ax, u16 cx, u16 dx)
{
    const u8 first = u8(ax), rows = u8(u8(ax >> 8) - first + 1);
    ds_u8(SPAN_ROWS) = rows;
    ds_u8(DS_span_first_row) = first;
    const u16 bx = u8(rows - 1);
    if (!(u16(dx - cx) & 0x8000)) std::swap(dx, cx);
    ds_u16(DS_span_right) = cx;
    ds_u16(DS_span_left) = dx;
    ds_u16(DS_span_step_left) = edge_step(u16(dx - ds_u16(DS_span_apex)), bx);
    ds_u16(DS_span_step_right) = edge_step(u16(cx - ds_u16(DS_span_apex)), bx);
    if (ds_u8(DS_span_from_apex) == 1) {
        ds_u16(DS_span_left) = ds_u16(DS_span_apex);
        ds_u16(DS_span_right) = ds_u16(DS_span_apex);
        ds_u16(DS_span_step_left) = u16(-ds_u16(DS_span_step_left));
        ds_u16(DS_span_step_right) = u16(-ds_u16(DS_span_step_right));
    }
    ds_u16(DS_span_left) = u16(ds_u16(DS_span_left) + 0x40);
    ds_u16(DS_span_right) = u16(ds_u16(DS_span_right) + 0xBF);
    // The mode's span routine [D8F8] (video_mode_setup).
    switch (ds_u16(DS_span_routine_a)) {
    case 0x788E: span_vga_a(es); break;
    case 0x4DFA: span_ega_a(es); break;
    case 0x5693: span_tandy_a(es); break;
    case 0x46E8: span_cga_a(es); break;
    default: render_parked("the triangle span routine [D8F8]");
    }
}

// 0919:7909 edge_setup (render3d.md §3.4): a line from row AL to row AH from bearing CX towards DX;
// D956 = CX, the step D95A, then the mode's line routine [D8FA].
void edge_setup(u16 es, u16 ax, u16 cx, u16 dx)
{
    const u8 first = u8(ax), rows = u8(u8(ax >> 8) - first + 1);
    ds_u8(SPAN_ROWS) = rows;
    ds_u8(DS_span_first_row) = first;
    ds_u16(DS_span_left) = cx;
    ds_u16(DS_span_step_left) = edge_step(u16(cx - dx), u8(rows - 1));
    // The mode's line routine [D8FA] (video_mode_setup).
    switch (ds_u16(DS_span_routine_b)) {
    case 0x7943: span_vga_b(es); break;
    case 0x4EB6: span_ega_b(es); break;
    case 0x5756: span_tandy_b(es); break;
    case 0x47CF: span_cga_b(es); break;
    default: render_parked("the line span routine [D8FA]");
    }
}

// 0919:767a draw_primitive (render3d.md §3.4): primitive DL at vertex BX (mode 0: triangle i, i+1,
// i+2; 2: i-1, i+1, i+2; 3 reads from the odd byte offset 2i-3, as the original; 1: the line i,
// i+1), culled when all its bearings have bit 15 set. A triangle is sorted by row and drawn as one
// or two halves (fill_triangle); the split bearing on the middle row is interpolated.
void draw_primitive(u16 es, u8 dl, u16 bx)
{
    bx = u16(bx << 1);
    u16 si = u16(bx + 2);
    if (dl == 1) {
        if ((bearing(bx) & bearing(si)) & 0x8000) return;
        if (row(bx) >= row(si)) std::swap(si, bx);
        edge_setup(es, u16(row8(si) << 8 | row8(bx)), bearing(bx), bearing(si));
        return;
    }
    bx = u16(bx - dl);
    u16 di = u16(si + 2);
    if (bearing(bx) & bearing(si) & bearing(di) & 0x8000) return;
    if (row(bx) >= row(si)) std::swap(si, bx);
    if (row(si) >= row(di)) std::swap(di, si);
    if (row(bx) >= row(si)) std::swap(si, bx);
    if (row(di) == row(bx)) {  // all on one row: the widest pair of bearings
        ds_u8(DS_span_from_apex) = 0;
        u16 cx = bearing(bx), dx = bearing(si);
        const u16 third = bearing(di);
        if (!(u16(third - dx) & 0x8000)) {
            if (!(u16(third - cx) & 0x8000)) {
                if (u16(cx - dx) & 0x8000) dx = third;
                else cx = third;
            }
        } else if (u16(third - cx) & 0x8000) {
            if (u16(cx - dx) & 0x8000) cx = third;
            else dx = third;
        }
        ds_u16(DS_span_apex) = dx;
        const u8 r = row8(bx);
        fill_triangle(es, u16(r << 8 | r), cx, dx);
        return;
    }
    if (row(di) == row(si)) {  // flat bottom: from the apex bx down
        ds_u8(DS_span_from_apex) = 1;
        ds_u16(DS_span_apex) = bearing(bx);
        fill_triangle(es, u16(row8(di) << 8 | row8(bx)), bearing(si), bearing(di));
        return;
    }
    if (row(si) == row(bx)) {  // flat top: down to the apex di
        ds_u8(DS_span_from_apex) = 0;
        ds_u16(DS_span_apex) = bearing(di);
        fill_triangle(es, u16(row8(di) << 8 | row8(si)), bearing(bx), bearing(si));
        return;
    }
    ds_u8(DS_span_from_apex) = 1;
    const u16 rows = u16(row(di) - row(bx));
    const u16 part = u16(row(si) - row(bx));
    u16 diff = u16(bearing(di) - bearing(bx));
    ds_u16(DS_span_apex) = diff;
    if (diff & 0x8000) diff = u16(-diff);
    u16 split = div32_16(u32(diff) * part, rows);
    if (ds_u8(u16(DS_span_apex + 1)) & 0x80) split = u16(-split);
    ds_u16(DS_span_apex) = bearing(bx);
    split = u16(split + bearing(bx));
    const u16 cx = bearing(si);
    fill_triangle(es, u16(row8(si) << 8 | row8(bx)), cx, split);
    ds_u8(DS_span_from_apex) = 0;
    ds_u16(DS_span_apex) = bearing(di);
    fill_triangle(es, u16(row8(di) << 8 | row8(si)), cx, split);
}

// 0919:763f draw_group_b (render3d.md §3.4): the group B primitives from 200h + min(D962, 1FEh) - 1
// down to 200h on the drawing page, colour D954 = the control byte's low 6 bits twice.
void draw_group_b()
{
    const u16 es = ds_u16(DS_draw_page_segment);
    u16 bx = ds_u16(DS_group_b_count);
    if (bx > 0x1FE) bx = 0x1FE;
    bx = u16(bx + 0x200 - 1);
    for (; bx >= 0x200; bx--) {
        const u8 ctrl = ds_u8(u16(DS_vertex_control + bx));
        const u8 colour = ctrl & 0x3F;
        if (colour == 0) continue;
        ds_u16(DS_span_colour) = u16(colour << 8 | colour);
        draw_primitive(es, u8(ctrl >> 6), bx);
    }
}

// 0919:7bbd spotlight_beam_vga (render3d.md §6): ten rows of a beam from row B7E2 (view rows; the
// beam ends at row 40h), each 2 * (A0h - width byte) pixels wide around column DX + width - 20h
// (the widths from CS:709A at BX), clipped to the view: covered pixels get bit 3 set where bit 4
// is clear.
void spotlight_beam_vga(u16 es, u16 bx, u16 dx)
{
    do {
        const u8 r = ds_u8(BEAM_ROW);
        if (r >= 0x40) return;
        u16 di = u16(u16(u16(r << 8) >> 2) + u16(r << 8) + 0x5008);
        const u16 width = seg_u8(CSSEG_spotlight_widths, bx);
        u16 cx = u16(-width + 0xA0);
        if (cx != 0) {
            cx = u16(cx << 1);
            u16 ax = u16(width + dx - 0x20);
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
                cx >>= 1;
                if (cx != 0) {
                    di = u16(di + ax);
                    for (; cx; cx--) {
                        u16 v = u16(mem_u8(es, di) | mem_u8(es, u16(di + 1)) << 8);
                        v = u16(v | ((u16(v >> 1) & 0x0808) ^ 0x0808));
                        mem_u8(es, di) = u8(v);
                        mem_u8(es, u16(di + 1)) = u8(v >> 8);
                        di = u16(di + 2);
                    }
                }
            }
        }
        bx = u16(bx + 1);
        ds_u8(BEAM_ROW)++;
    } while (--ds_u8(BEAM_ROWS) != 0);
}

// 0919:7b17 spotlight_beam (render3d.md §6): a light aimed with the heading AH and fraction AL at
// the gun elevation BL: D905 = 1 (the enemy spotting bonus); the beam's column from the bearing
// relative to the view, its first row from the elevation against the station's gun elevation (80h
// at the pilot) and the pitch D193 (rows reaching the horizon raise D965), its widths from the
// table row of the elevation difference.
void spotlight_beam(u16 ax, u8 bl)
{
    ds_u8(DS_spotlight_on) = 1;
    const u8 al = u8(ax);
    const bool borrow = al < ds_u8(DS_view_heading_fraction);
    const u8 ah = u8(u8(ax >> 8) - ds_u8(DS_view_heading) - (borrow ? 1 : 0));
    const u16 dx = u16(u16(s16(s8(ah))) << 1);
    const u8 station = u8(ds_u16(DS_station));
    u8 cl = 0x80;
    if (station >= 2) {
        cl = ds_u8(DS_elevation_bow);
        if (station != 2) {
            cl = ds_u8(DS_elevation_midship);
            if (station != 3) cl = ds_u8(DS_elevation_stern);
        }
    }
    cl = u8(cl >> 1);
    cl = u8(cl - u8(bl >> 1));
    cl = u8(s8(u8(-cl)) >> 2);
    const u16 es = ds_u16(DS_draw_page_segment);
    bl = u8(u8(-u8(bl - 0x10)) >> 4);
    const u8 bh = u8(ds_u8(DS_view_pitch) + cl - 0x5E);
    bl = u8(bl - bh);
    if (bl & 0x80) bl = 0;
    if (bl > 0x12) bl = 0x12;
    const u8 first = u8(0x2D - cl);
    if (first > ds_u8(DS_view_sky_top)) ds_u8(DS_view_sky_top) = first;
    ds_u8(BEAM_ROW) = first;
    ds_u8(BEAM_ROWS) = 10;
    const u16 table = u16(u8(bl * 10) + CS_spotlight_widths);
    // The mode's beam: VGA above 0Dh, EGA 0Dh, Tandy 9-0Ch, CGA below.
    // TODO(verify): the dispatch of the other modes (render/modes.hpp).
    const u8 mode = u8(ds_u16(DS_video_mode));
    if (mode > 0x0D) spotlight_beam_vga(es, table, dx);
    else if (mode == 0x0D) spotlight_beam_ega(es, table, dx);  // (7bab: AL == 0Dh; verified)
    else if (mode >= 9) spotlight_beam_tandy(es, table, dx);
    else spotlight_beam_cga(es, table, dx);
}

// 0919:7a89 spotlights (render3d.md §6): at night, not in chase view and with the main switch on
// (bit 0 of D520 clear), D905 = 0 and each light whose switch is on, which is not destroyed
// (condition 2) and whose gun mount is on, aimed with its gun: bow D528/D50E/D525, midship
// D52D/D50F/D529, stern D530/D52E/D50C. The fraction byte passed is the gun's heading fraction
// ORed with the light's condition bits (as the original).
void spotlights()
{
    ds_u8(DS_spotlight_on) = 0;
    if (ds_u8(DS_chase_view) != 0 || ds_u8(DS_daylight) != 0 || (ds_u8(DS_panel_switches) & 1)) return;
    auto aim = [](u8 condition, u8 gun) {
        const u8 al = u8(condition | ds_u8(u16(DS_heading_fraction + gun)));
        const u8 elevation = gun == 1 ? ds_u8(DS_elevation_bow) : gun == 2 ? ds_u8(DS_elevation_midship)
                                                                             : ds_u8(DS_elevation_stern);
        spotlight_beam(u16(ds_u8(u16(DS_heading + gun)) << 8 | al), elevation);
    };
    if (!(ds_u8(u16(DS_panel_switches + 8)) & 1)) {
        const u8 c = ds_u8(DS_spotlight_front_condition) & 3;
        if (c != 2 && !(ds_u8(DS_bow_mount) & 1)) aim(c, 1);
    }
    if (!(ds_u8(u16(DS_panel_switches + 0x0D)) & 1)) {
        const u8 c = ds_u8(DS_spotlight_rear_condition) & 3;
        if (c != 2 && !(ds_u8(DS_midship_mount) & 1)) aim(c, 2);
    }
    if (!(ds_u8(u16(DS_panel_switches + 0x10)) & 1) && !(ds_u8(DS_stern_mount) & 1)) {
        const u8 c = ds_u8(DS_spotlight_middle_condition) & 3;
        if (c != 2) aim(c, 3);
    }
}

// 0919:2c35 palette_flash (render3d.md §7.1): the flash level (D9B5 in a 3D station, else 0; the
// raw value is compared with the last level D6E5, its low 2 bits stored): VGA sets DAC register 8
// to the RGB triple D6EA + 3 * level (INT 10h AX=1012h).
void palette_flash()
{
    u8 al = 0;
    if (ds_u16(DS_station) < 5) al = ds_u8(DS_screen_shake);
    if (al == ds_u8(DS_flash_level)) return;
    al &= 3;
    ds_u8(DS_flash_level) = al;
    const u8 mode = u8(ds_u16(DS_video_mode));
    // PORT: EGA and Tandy set palette register 8 through ega_pal_register (147c:000f): parked.
    if (mode == 0x0D || mode == 9) render_parked("the flash palette register (ega_pal_register)");
    if (mode < 0x0D) return;
    bios_dac_set_block(8, 1, {u16(DS_flash_colours + u8(al * 3)), DGROUP});
}

// 0919:2e57 screen_shake_step (render3d.md §7.2): B7F2 counts down; outside VGA each step sets the
// display start from the pair CS:2E47[2 * count].
void screen_shake_step()
{
    u8 count = ds_u8(SHAKE_COUNT);
    if (count == 0) return;
    ds_u8(SHAKE_COUNT) = --count;
    if (u8(ds_u16(DS_video_mode)) == 0x13) return;
    const u16 at = u16(CS_shake_offsets + (count << 1));
    gfx_set_display_offset(seg_u8(CSSEG_shake_offsets, u16(at + 1)), seg_u8(CSSEG_shake_offsets, at));
}

} // namespace gb
