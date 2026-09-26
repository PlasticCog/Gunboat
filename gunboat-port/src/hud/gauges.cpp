// The pilot's instruments (hud.md §3): the throttle and fuel needles, the throttle levers, the jet
// angle marker, and the radar scope with its blips. All are drawn only at the pilot's station into
// the current draw page; the needles and the radar sweep are lines from a pivot (needle_draw).
//
// Scratch bytes these routines share with the rest of segment 0919 (simulation.md names them for
// their other use): the needle pivot offset is kept in vec_factor (x, DS:B7E5) and B7E6 (y), the
// erase and draw colours in vec_product_lo/hi (DS:B7E9/B7EA), the pivot's y in scratch_b7e3.
#include "hud/hud.hpp"

#include "mem.hpp"
#include "platform/gfx.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 PIVOT_X = DS_vec_factor;           // DS:B7E5
constexpr u16 PIVOT_Y = u16(DS_vec_factor + 1);  // DS:B7E6
constexpr u16 ERASE_COLOUR = DS_vec_product_lo;  // DS:B7E9
constexpr u16 DRAW_COLOUR = DS_vec_product_hi;   // DS:B7EA

u16 cs_word(u16 file_seg, u16 off) { return seg_u16(file_seg, off); }

// One throttle or fuel needle (0919:2355 and its three copies): if the end point points[index] (a
// table of segment 0919) differs from the last one drawn, the old one is erased and the new one drawn
// from the gauge centre for the look direction (gauge_centres + gauge_row + centre); a centre of 0
// means the gauge is not in view. AX as the original leaves it.
void needle(u16 points, u16 index, u16 last, u16 centre, u8 centre_y, u16 &ax)
{
    const u16 cx = cs_word(CSSEG_throttle_needle_points, u16(points + 2 * index));
    if (cx == ds_u16(last)) return;
    ds_u16(DS_scratch_b7dc) = ds_u16(last);
    ds_u16(last) = cx;
    ax = ds_u16(u16(ds_u16(DS_gauge_row) + centre));
    if (ax == 0) return;
    ds_u16(DS_scratch_b7e0) = ax;
    ds_u8(DS_scratch_b7e3) = centre_y;
    needle_draw(cx);
    ax = 0;  // gfx_line_to's AX
}

// The lever position of a throttle byte (0919:2470 / 24db): DL = 24 * (5 - ((throttle - 8) >> 4)) in
// 8 bits, the throttle halved and the step negated in reverse (jet_angle bit 7), a negative
// throttle - 8 taken as 0. Returns DL (the column offset) and the AL the original leaves (16x).
struct Lever {
    u8 dl, al;
};
Lever lever(u8 throttle)
{
    const bool reverse = (ds_u8(DS_jet_angle) & 0x80) != 0;
    u8 al = throttle;
    if (reverse) al >>= 1;
    al = u8(al - 8);
    if (al & 0x80) al = 0;  // JNS on the 8-bit result
    al >>= 4;
    if (reverse) al = u8(-al);
    al = u8(-al);
    al = u8(al + 5);
    al = u8(al << 3);
    u8 dl = al;
    al = u8(al << 1);
    dl = u8(dl + al);
    return {dl, al};
}

// One step of the radar sweep (0919:2855 / 2893): the line at radar_sweep becomes the one to erase,
// the sweep advances (mod 128), and the new line's end is returned (CX).
u16 sweep_step()
{
    const u8 at = ds_u8(DS_radar_sweep);
    const u16 dx = cs_word(CSSEG_throttle_needle_points, u16(CS_throttle_needle_points + 2 * at));
    const u8 next = u8(at + 1) & 0x7F;
    ds_u8(DS_radar_sweep) = next;
    const u16 cx = cs_word(CSSEG_throttle_needle_points, u16(CS_throttle_needle_points + 2 * next));
    ds_u16(DS_scratch_b7dc) = dx;
    return cx;
}

// SAR DX,CL as the emulator and a 386 execute it: the count masked to 5 bits.
u16 sar16(u16 v, u8 cl)
{
    const u8 n = cl & 0x1F;
    if (n >= 16) return (v & 0x8000) ? 0xFFFF : 0;
    return u16(s16(v) >> n);
}

} // namespace

// 0919:2324 throttle_needles (hud.md §3): at the pilot's station, the two throttle needles
// (colour 7Dh), the two fuel needles (75h), each redrawn only when its end point changes, then the
// two throttle levers copied from page 1 where their gauge is in view and their column changed.
// The lever arguments are pushed as words with DH = the high byte of draw_page. Returns AX.
u16 throttle_needles(u16 ax)
{
    if (ds_u16(DS_station) != 1) return ax;
    ds_u8(ERASE_COLOUR) = 0;
    ds_u8(DRAW_COLOUR) = 0x0F;
    ds_u16(DS_gauge_row) = u16(ds_u16(DS_look_direction) * 12);
    ax = cs_word(CSSEG_needle_pivot, CS_needle_pivot);
    ds_u8(PIVOT_X) = u8(ax);
    ds_u8(PIVOT_Y) = u8(ax >> 8);
    needle(CS_throttle_needle_points, ds_u8(DS_throttle), DS_throttle_needle_ends, DS_gauge_centres, 0x7D, ax);
    needle(CS_throttle_needle_points, ds_u8(u16(DS_throttle + 1)), u16(DS_throttle_needle_ends + 2),
           u16(DS_gauge_centres + 2), 0x7D, ax);
    ax = cs_word(CSSEG_fuel_needle_pivot, CS_fuel_needle_pivot);
    ds_u8(PIVOT_X) = u8(ax);
    ds_u8(PIVOT_Y) = u8(ax >> 8);
    needle(CS_fuel_needle_points, u16(ds_u16(DS_fuel) >> 11), DS_fuel_needle_ends, u16(DS_gauge_centres + 4), 0x75, ax);
    needle(CS_fuel_needle_points, u16(ds_u16(u16(DS_fuel + 2)) >> 11), u16(DS_fuel_needle_ends + 2),
           u16(DS_gauge_centres + 6), 0x75, ax);

    // 2452: the levers
    u16 cx = ds_u16(u16(ds_u16(DS_gauge_row) + DS_gauge_centres + 8));
    if (cx != 0) {
        const u16 dst = ds_u16(DS_draw_page);
        const u16 dh = dst & 0xFF00;
        const Lever l = lever(ds_u8(DS_throttle));
        ax = u16((ax & 0xFF00) | l.al);
        const u16 x1 = u16(dh | u8(l.dl + 0x17));
        const u16 x0 = u16(x1 - 0x17);
        if (u8(x0) != ds_u8(DS_lever_last_x)) {
            ds_u8(DS_lever_last_x) = u8(x0);
            gfx_copy_rect(x0, x1, u16(dh | 0x9D), u16(dh | 0xC7), cx, u16(dh | 0xA1), u16(dh | 1), dst);
            ax = 0;
        }
    }
    cx = ds_u16(u16(ds_u16(DS_gauge_row) + DS_gauge_centres + 10));
    if (cx != 0) {
        const u16 dst = ds_u16(DS_draw_page);
        const u16 dh = dst & 0xFF00;
        const Lever l = lever(ds_u8(u16(DS_throttle + 1)));
        ax = u16((ax & 0xFF00) | l.al);
        const u16 x1 = u16((dh | l.dl) + 0x57);
        const u16 x0 = u16(x1 - 0x17);
        if (u8(x0) != ds_u8(u16(DS_lever_last_x + 1))) {
            ds_u8(u16(DS_lever_last_x + 1)) = u8(x0);
            gfx_copy_rect(x0, x1, dh, u16(dh | 0x2A), cx, u16(dh | 0xA1), u16(dh | 1), dst);
            ax = 0;
        }
    }
    return ax;
}

// 0919:2704 needle_draw (hud.md §3): the needle from the pivot (scratch_b7e0, the needle centre x, + pivot x,
// scratch_b7e3 + pivot y, 8-bit) to the end point `cx` (dx, dy from the centre): first the old end
// (needle_old_end, unless its dx is FFh) in the erase colour, then the new one in the draw colour.
// The pen is moved back to the pivot with the first gfx_move_to's arguments, still on the stack.
void needle_draw(u16 cx)
{
    const u16 x = u16(ds_u16(DS_scratch_b7e0) + ds_u8(PIVOT_X));
    const u16 y = u8(ds_u8(DS_scratch_b7e3) + ds_u8(PIVOT_Y));
    gfx_move_to(s16(x), s16(y));
    gfx_set_colour(ds_u8(ERASE_COLOUR));
    const u16 old = ds_u16(DS_scratch_b7dc);
    if (u8(old) != 0xFF) {
        gfx_line_to(s16(u16(ds_u16(DS_scratch_b7e0) + u8(old))), u8(ds_u8(DS_scratch_b7e3) + (old >> 8)));
        gfx_move_to(s16(x), s16(y));
    }
    gfx_set_colour(ds_u8(DRAW_COLOUR));
    gfx_line_to(s16(u16(ds_u16(DS_scratch_b7e0) + u8(cx))), u8(ds_u8(DS_scratch_b7e3) + (cx >> 8)));
}

// 0919:2786 jet_marker (hud.md §3): at the pilot's station, when the jet angle (jet_angle & 7Fh)
// changed, the marker row 6Fh..72h under the jet gauge of the look direction is cleared and the
// 4-row marker bitmap drawn at the angle's column (colour 4; 2 in CGA). The bottom row is pushed as
// CX with CL = 72h; CH is 0 there because gfx_set_colour(0) leaves its argument in CX. Returns AX.
u16 jet_marker(u16 ax)
{
    if (ds_u16(DS_station) != 1) return ax;
    const u8 al = ds_u8(DS_jet_angle) & 0x7F;
    if (al == ds_u8(DS_jet_marker_last)) return u16((ax & 0xFF00) | al);
    ds_u8(DS_jet_marker_last) = al;
    gfx_set_colour(0);
    const u16 look = ds_u16(DS_look_direction);
    const u16 right = look < 1 ? 0x128 : look == 1 ? 0xC0 : 0x58;
    const u16 left = u16(right - 0x44);
    gfx_fill_rect(left, right, 0x6F, 0x72);
    gfx_set_colour(u8(ds_u16(DS_video_mode)) == 4 ? 2 : 4);
    gfx_move_to(s16(u16(((ds_u8(DS_jet_angle) & 0x7F) >> 1) + left)), 0x72);
    gfx_draw_bitmap(DS_jet_marker_bitmap, 1, 4);
    return 0;
}

// 0919:2810 radar_scope (hud.md §3): when the pilot looks right. The sweep advances 4 of its 128
// steps per frame, each erasing the last line (colour 6) and drawing the next (0Ch damaged, 0Eh
// intact, 3 in CGA; 6, i.e. invisible, when the main switch or the radar switch is off or the radar
// destroyed). Then, unless the scene is being rebuilt or the radar is off, each visible authored
// object (index >= 36, kind not 0) within range (4000h >> (sw 3 & 3)) is plotted; the list is by
// distance, so the first one out of range ends it. With no visible object the loop starts at
// index 7FFFh (DEC / SHL / SHR of 0), as in the original. Returns AX.
u16 radar_scope(u16 ax)
{
    if (ds_u16(DS_look_direction) != 2) return ax;
    u8 colour = 6;
    ds_u8(ERASE_COLOUR) = colour;
    if (!(ds_u8(DS_panel_switches) & 1) && !(ds_u8(u16(DS_panel_switches + 4)) & 1)) {
        const u8 c = ds_u8(DS_radar_condition) & 3;
        if (c != 2) colour = c < 2 ? 0x0C : (u8(ds_u16(DS_video_mode)) == 4 ? 3 : 0x0E);
    }
    ds_u8(DRAW_COLOUR) = colour;
    const u16 pivot = cs_word(CSSEG_needle_pivot, CS_needle_pivot);
    ds_u8(PIVOT_X) = u8(pivot);
    ds_u8(PIVOT_Y) = u8(pivot >> 8);
    ds_u16(DS_scratch_b7e0) = 0xEE;  // (the original stores these after the first step's ends)
    ds_u8(DS_scratch_b7e3) = 0x77;
    needle_draw(sweep_step());
    ds_u8(DS_radar_steps) = 3;
    do {
        needle_draw(sweep_step());
    } while (--ds_u8(DS_radar_steps) != 0);
    ax = 0;
    if (ds_u8(DS_scene_rebuild) != 0) return ax;
    if (ds_u8(DS_panel_switches) & 1) return ax;
    if (ds_u8(u16(DS_panel_switches + 4)) & 1) return ax;
    if ((ds_u8(DS_radar_condition) & 3) == 2) return 2;
    ds_u8(DS_scratch_b7e3) = u8(u8(ds_u8(DS_radar_sweep) << 1) - 0x6C);
    const u8 range = ds_u8(u16(DS_panel_switches + 3)) & 3;
    ds_u8(DS_scratch_b7e2) = range;
    ds_u16(DS_scratch_b7dc) = u16(0x4000 >> range);  // the range
    ax = ds_u16(DS_scratch_b7dc);
    u16 bx = u16(ds_u16(DS_visible_count) - 1);
    do {
        const u16 b2 = u16(bx << 1);
        const u16 distance = ds_u16(u16(DS_visible_distance_word + b2));
        ax = ds_u16(u16(DS_visible_object + b2));
        bx = u16(b2 >> 1);
        if (ax >= 0x48 && ds_u8(u16(ax + DS_object_word)) != 0) {
            if (distance > ds_u16(DS_scratch_b7dc)) return ax;
            radar_plot(ds_u8(u16(bx + DS_visible_bearing)), distance);
            ax = 0;
        }
        bx--;
    } while (s16(bx) >= 0);
    return ax;
}

// 0919:2932 radar_plot (hud.md §3): one blip for bearing AH and distance CX. Its colour fades with
// the angle behind the sweep (scratch_b7e3): 0Fh, 13h, 7, then the erase colour; its position is
// the centre (FFh, 87h) plus distance * sine_table >> 24, shifted right by 2 - range (a range of 3
// shifts by 31: 0 or -1). At bearing 20h both terms read sine_table[0] (NEG of 0 is 0), as in the
// original. The bearing less 20h stays in vec_product_hi.
void radar_plot(u8 ah, u16 cx)
{
    const u8 a = u8(ah - 0x20);
    ds_u8(DRAW_COLOUR) = a;
    const u8 behind = u8(u8(-a) + ds_u8(DS_scratch_b7e3));
    const u8 mode = u8(ds_u16(DS_video_mode));
    u16 colour = ds_u8(ERASE_COLOUR);
    if (behind < 0xF0) {
        colour = mode == 0x13 ? 7 : mode == 4 ? 1 : 8;
        if (behind < 0xB0) {
            colour = mode == 0x13 ? 0x13 : 7;
            if (behind < 0x70) colour = 0x0F;
        }
    }
    gfx_set_colour(s16(colour));
    const u16 distance = cx;
    const u8 angle = ds_u8(DRAW_COLOUR);
    const u8 i4 = u8(angle << 2);
    u16 t_cx = cs_word(CSSEG_sine_table, u16(CS_sine_table + i4));
    u16 t_bx = cs_word(CSSEG_sine_table, u16(CS_sine_table + u8(-i4)));
    if (angle & 0x40) {
        const u16 t = t_cx;
        t_cx = t_bx;
        t_bx = t;
    }
    u16 dy = u16(u8((u32(distance) * t_bx) >> 24));
    const u8 quadrant = ds_u8(DRAW_COLOUR) & 0xC0;
    if (!(quadrant == 0x40 || quadrant == 0x80)) dy = u16(-dy);
    const u8 shift = u8(2 - ds_u8(DS_scratch_b7e2));
    // PORT: SAR with the count masked to 5 bits (386, the emulator); for the counts radar_scope
    // gives (0, 1, 2, FFh) an 8086 gives the same result.
    dy = u16(sar16(dy, shift) + 0x87);
    u16 dx = u16(u8((u32(distance) * t_cx) >> 24));
    if ((ds_u8(DRAW_COLOUR) & 0xC0) >= 0x80) dx = u16(-dx);
    dx = u16(sar16(dx, shift) + 0xFF);
    gfx_put_pixel(s16(dx), s16(dy));
}

} // namespace gb
