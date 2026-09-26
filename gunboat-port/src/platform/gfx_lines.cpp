// Lines of the graphics library (video.md §2): gfx_line_to (13d5) and gfx_fill_rect_clipped (15d9),
// Test Drive III's routines (gb_td3_matches.csv). Only the pilot's needles and the radar sweep use
// them. Both draw through primitives that dispatch on the mode (gfx_put_pixel, gfx_fill_rect), so
// they have no mode path of their own.
#include "platform/gfx.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

// 13d5:000d gfx_line_to: a line from the pen to (x, y) in the current colour; the pen moves to
// (x, y). A horizontal or vertical line is one clipped rectangle; any other is drawn pixel by
// pixel (Bresenham, both end points included), each pixel clipped by gfx_put_pixel. Signed
// comparisons as the original's JGE/JLE; the steps and error terms are kept in DGROUP.
void gfx_line_to(s16 x, s16 y)
{
    u16 ax = u16(x), bx = ds_u16(DS_gfx_pen_x);
    u16 cx = u16(y), dx = ds_u16(DS_gfx_pen_y);
    if (ax == bx) {  // vertical (or a point)
        ds_u16(DS_gfx_pen_y) = cx;
        if (s16(dx) < s16(cx)) {
            const u16 t = cx;
            cx = dx;
            dx = t;
        }
        gfx_fill_rect_clipped(s16(bx), s16(ax), s16(cx), s16(dx));
        return;
    }
    if (cx == dx) {  // horizontal
        ds_u16(DS_gfx_pen_x) = ax;
        if (s16(bx) > s16(ax)) {
            const u16 t = bx;
            bx = ax;
            ax = t;
        }
        gfx_fill_rect_clipped(s16(bx), s16(ax), s16(cx), s16(dx));
        return;
    }
    // 0049: Bresenham
    u16 si = ax;
    ax = cx;
    cx = 1;
    dx = 1;
    const u16 pen_y = ds_u16(DS_gfx_pen_y);
    const bool y_up = s16(ax) < s16(pen_y);  // SUB / JGE: the signed comparison
    ax = u16(ax - pen_y);
    if (y_up) {
        dx = u16(-dx);
        ax = u16(-ax);
    }
    ds_u16(DS_line_step_y) = dx;
    const bool x_left = s16(si) < s16(bx);
    si = u16(si - bx);
    if (x_left) {
        cx = u16(-cx);
        si = u16(-si);
    }
    ds_u16(DS_line_step_x) = cx;
    if (s16(si) >= s16(ax)) {  // flat: the major axis is x
        dx = 0;
    } else {                   // steep
        cx = 0;
        const u16 t = si;
        si = ax;
        ax = t;
    }
    ds_u16(DS_line_axis_x) = cx;
    ds_u16(DS_line_axis_y) = dx;
    ax = u16(ax << 1);
    ds_u16(DS_line_err_axis) = ax;
    ax = u16(ax - si);
    u16 di = ax;
    ax = u16(ax - si);
    ds_u16(DS_line_err_diag) = ax;
    cx = ds_u16(DS_gfx_pen_x);
    dx = ds_u16(DS_gfx_pen_y);
    si = u16(si + 1);
    for (;;) {
        gfx_put_pixel(s16(cx), s16(dx));
        if (--si == 0) break;
        if (s16(di) >= 0) {
            cx = u16(cx + ds_u16(DS_line_step_x));
            dx = u16(dx + ds_u16(DS_line_step_y));
            di = u16(di + ds_u16(DS_line_err_diag));
        } else {
            cx = u16(cx + ds_u16(DS_line_axis_x));
            dx = u16(dx + ds_u16(DS_line_axis_y));
            di = u16(di + ds_u16(DS_line_err_axis));
        }
    }
    ds_u16(DS_gfx_pen_x) = cx;
    ds_u16(DS_gfx_pen_y) = dx;
}

// 15d9:0003 gfx_fill_rect_clipped: gfx_fill_rect of x0..x1, y0..y1 cut to the clip box; nothing
// if it lies outside (signed comparisons).
void gfx_fill_rect_clipped(s16 x0, s16 x1, s16 y0, s16 y1)
{
    if (x0 > ds_s16(DS_clip_x_max)) return;
    if (x0 < ds_s16(DS_clip_x_min)) x0 = ds_s16(DS_clip_x_min);
    if (x1 < ds_s16(DS_clip_x_min)) return;
    if (x1 > ds_s16(DS_clip_x_max)) x1 = ds_s16(DS_clip_x_max);
    if (y0 > ds_s16(DS_clip_y_max)) return;
    if (y0 < ds_s16(DS_clip_y_min)) y0 = ds_s16(DS_clip_y_min);
    if (y1 < ds_s16(DS_clip_y_min)) return;
    if (y1 > ds_s16(DS_clip_y_max)) y1 = ds_s16(DS_clip_y_max);
    gfx_fill_rect(u16(x0), u16(x1), u16(y0), u16(y1));
}

} // namespace gb
