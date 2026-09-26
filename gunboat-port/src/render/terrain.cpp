// The terrain: its window of loaded cells, the vertex arrays, the projection with its shore
// contact candidates, the draw order of group A, the sky, the water and its marks
// (render3d.md §2-§3, simulation.md §10, world.md §5).
#include "render/render.hpp"

#include <utility>

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u8 rol8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v << n | v >> (8 - n)) : v;
}

// B7E2 holds the tile's rotation while it loads (route_rotate), B7E3 the vertex or object count
// left, B7E5 (vec_factor) the stride between the vertex columns of the TILE.BIN record.
constexpr u16 TILE_ROTATION = DS_scratch_b7e2;
constexpr u16 LOAD_COUNT = DS_scratch_b7e3;
constexpr u16 TILE_STRIDE = DS_vec_factor;

// The 17-wide grid index of the cell whose centre area holds (x_high, y_high), and the quadrant
// bits (bit 1: Y, bit 0: X) of the half-cell the point is in (0919:8409-842f, 7c3e-7c5d).
u16 grid_cell(u8 x_high, u8 y_high, u8 *quadrant)
{
    const u8 y = u8(y_high - 4), x = u8(x_high - 4);
    if (quadrant) *quadrant = u8(((y >> 1) & 1) << 1 | ((x >> 1) & 1));
    const u16 row = u16(u8(-u8(y >> 2) + 9) * 0x11);
    return u16(row + u16(x >> 2) + 1);
}

// The loaded-neighbour bits of a centre cell: the tile's visibility byte rotated by twice its
// rotation (0919:84c8-84ed).
u8 visibility_bits(u8 tile)
{
    const u8 mask = seg_u8(CSSEG_tile_visibility, u16(CS_tile_visibility + (tile & 0x3F)));
    return rol8(mask, u8(rol8(tile, 3) & 6));
}

void fill_row(u16 es, u16 &di, u16 ax)  // rep stosw, 80h words, then the 40h bytes to the next row
{
    for (int i = 0; i < 0x80; i++) {
        mem_u8(es, di) = u8(ax);
        mem_u8(es, u16(di + 1)) = u8(ax >> 8);
        di = u16(di + 2);
    }
    di = u16(di + 0x40);
}

} // namespace

// 0919:8711 route_rotate (render3d.md §2): a tile-local point (AL, CL; 0-80h) turned by the
// tile's rotation B7E2 in 90-degree steps, times 8, plus the tile origin D9A9/D9AB. Returns AX, CX.
AxCx route_rotate(u8 al, u8 cl)
{
    const u8 rotation = ds_u8(TILE_ROTATION);
    if (rotation == 1) {
        std::swap(al, cl);
        cl = u8(-cl + 0x80);
    } else if (rotation == 2) {
        al = u8(-al + 0x80);
        cl = u8(-cl + 0x80);
    } else if (rotation > 2) {
        std::swap(al, cl);
        al = u8(-al + 0x80);
    }
    return {u16(u16(al << 3) + ds_u16(DS_tile_origin_x)), u16(u16(cl << 3) + ds_u16(DS_tile_origin_y))};
}

// 0919:8665 vertex_load (render3d.md §2): CX vertices of a TILE.BIN record at ES:SI into the group
// whose count word is at DS:BX, from index DI + count. The record keeps the columns B7E5 bytes
// apart: control byte, height, X, Y. A group full at 200h stops loading (and sets the overflow
// flag D9A6). The time-of-day colours: 0Eh -> D951, 0Ah -> D952, 02h -> D952 ^ (alt & D9B0),
// 06h -> 06h ^ ((alt & D9B1) ^ 4), alt = D9AF toggling 00h/1Fh per vertex; bit 7 kept. Returns SI
// after the vertices loaded (unchanged if none were).
u16 vertex_load(u16 es, u16 bx, u16 di, u16 cx, u16 si)
{
    if (cx == 0) return si;
    u16 ax = ds_u16(bx);
    if (ax >= 0x200) return si;
    di = u16(di + ax);
    ax = u16(ax + cx);
    if (ax >= 0x200) {
        ax = u16(ax - 0x200);
        cx = u16(cx - ax);
        ds_u8(DS_window_overflow) = 1;
        ax = 0x200;
    }
    ds_u16(bx) = ax;
    const u16 stride = ds_u8(TILE_STRIDE);
    ds_u8(LOAD_COUNT) = u8(cx);
    do {
        u8 al = mem_u8(es, si);
        const u8 mode = al & 0x80;
        al ^= mode;
        ds_u8(DS_shade_toggle) ^= 0x1F;
        u8 ah = ds_u8(u16(DS_scene_colours + 2));  // D951
        if (al != 0x0E) {
            ah = ds_u8(u16(DS_scene_colours + 3));  // D952
            if (al != 0x0A) {
                u8 ch = ds_u8(DS_shade_toggle) & ds_u8(DS_shade_mask_a);
                if (al != 2) {
                    ah = al;
                    if (al == 6) {
                        ch = u8((ds_u8(DS_shade_toggle) & ds_u8(DS_shade_mask_b)) ^ 4);
                        ah ^= ch;
                    }
                } else {
                    ah ^= ch;
                }
            }
        }
        ds_u8(u16(DS_vertex_control + di)) = u8(ah | mode);
        const u16 d2 = u16(di << 1);
        ds_u16(u16(DS_vertex_height + d2)) = mem_u8(es, u16(stride + si));
        si = u16(si + stride);
        const u8 x = mem_u8(es, u16(stride + si));
        const u8 y = mem_u8(es, u16(u16(stride << 1) + si));
        si = u16(si - stride);
        const AxCx p = route_rotate(x, y);
        ds_u16(u16(DS_vertex_x + d2)) = u16(p.ax << 2);
        ds_u16(u16(DS_vertex_y + d2)) = u16(p.cx << 2);
        di = u16(u16(d2 >> 1) + 1);
        si = u16(si + 1);
    } while (--ds_u8(LOAD_COUNT) != 0);
    return si;
}

// 0919:858f tile_load (render3d.md §2): the TILE.BIN record of a cell byte (tile = low 6 bits,
// rotation = bits 6-7) at the origin D9A9/D9AB: its group A and B vertices (vertex_load) and its
// scenery objects, appended to the object arrays after the authored ones (at most 60h in the
// window; more set the overflow flag). An object's facing is the low 3 bits of its entry's
// offset in TILE.BIN.
void tile_load(u8 al)
{
    ds_u8(DS_shade_toggle) = 0;
    const u16 es = ds_u16(DS_tile_bin_segment);
    ds_u8(TILE_ROTATION) = u8(al >> 6);
    const u16 index = u16((al & 0x3F) << 1);
    u16 si = u16(mem_u16(es, u16(index + ds_u16(DS_tile_bin_offset))) + ds_u16(DS_tile_bin_offset));
    ds_u16(DS_tile_record) = si;
    const u8 count_a = mem_u8(es, si);
    si = u16(si + 1);
    const u8 total = u8(count_a + mem_u8(es, si));
    if (total != 0) {
        si = u16(si + 3);
        ds_u8(TILE_STRIDE) = total;
        // vertex_load leaves SI where group A's vertices ended; if it loads none (group full, or
        // an empty group), group B reads from the same place (a quirk, kept).
        si = vertex_load(es, DS_group_a_count, 0, count_a, si);
        si = vertex_load(es, DS_group_b_count, 0x200, u8(total - count_a), si);
    }
    si = ds_u16(DS_tile_record);
    const u16 vertices = u8(mem_u8(es, si) + mem_u8(es, u16(si + 1)));
    si = u16(si + 2);
    u8 cl = mem_u8(es, si);
    si = u16(si + 2);
    si = u16(si + u16(vertices << 2));
    if (cl == 0) return;
    u8 n = ds_u8(DS_scenery_count);
    u16 bx = n;
    if (n >= 0x60) return;
    n = u8(n + cl);
    if (n >= 0x60) {
        n = u8(n - 0x60);
        cl = u8(cl - n);
        ds_u8(DS_window_overflow) = 1;
        n = 0x60;
    }
    ds_u8(DS_scenery_count) = n;
    ds_u8(LOAD_COUNT) = cl;
    bx = u16(bx + ds_u16(DS_authored_object_count));
    do {
        const u16 word = u16((si & 7) << 8 | mem_u8(es, si));
        const u16 b2 = u16(bx << 1);
        ds_u16(u16(DS_object_word + b2)) = word;
        si = u16(si + 1);
        const u8 x = mem_u8(es, si);
        si = u16(si + 1);
        const u8 y = mem_u8(es, si);
        si = u16(si + 2);
        const AxCx p = route_rotate(x, y);
        ds_u16(u16(DS_object_x + b2)) = p.ax;
        ds_u16(u16(DS_object_y + b2)) = p.cx;
        bx = u16(u16(b2 >> 1) + 1);
    } while (--ds_u8(LOAD_COUNT) != 0);
}

// 0919:8408 terrain_cells_update (render3d.md §2): when the boat enters another cell (D9AD), or
// the window overflowed and the boat moved to another half-cell quadrant beyond the hysteresis
// band, the window is rebuilt: the scene rebuild flag D8BC, both vertex groups and the scenery
// emptied, then up to nine cells (the centre cell and the neighbours its tile lets be seen, in
// the quadrant's order) and the terrain structures in each of them are loaded (tile_load).
void terrain_cells_update()
{
    const u8 x_high = ds_u8(u16(DS_object_x + 1)), y_high = ds_u8(u16(DS_object_y + 1));
    u8 quadrant;
    const u16 cell = grid_cell(x_high, y_high, &quadrant);
    if (ds_u16(DS_terrain_rebuild) == cell) {
        if (ds_u8(DS_window_overflow) == 0) return;
        if (ds_u8(DS_window_quadrant) == quadrant) return;
        // Hysteresis (0919:8443-848b): the old quadrant stays while the boat is near the middle.
        const u8 old = ds_u8(DS_window_quadrant);
        const u8 x = u8(u8(ds_u16(DS_object_x) >> 2) - 0x70);
        const u8 y = u8(u8(ds_u16(DS_object_y) >> 2) - 0x70);
        bool keep;
        if (y <= 0x20) {
            if (x <= 0x20) keep = true;
            else if (x < 0x90) keep = (old & 1) != 0;
            else keep = (old & 1) == 0;
        } else if (x > 0x20) {
            keep = false;
        } else if (y < 0x90) {
            keep = old >= 2;
        } else {
            keep = old <= 2;
        }
        if (keep) return;
    }
    ds_u8(DS_scene_rebuild) = 1;
    ds_u8(DS_window_quadrant) = quadrant;
    u16 order = u16(quadrant * 9);
    ds_u16(DS_terrain_rebuild) = cell;
    ds_u16(DS_group_a_count) = 0;
    ds_u16(DS_group_b_count) = 0;
    ds_u8(DS_scenery_count) = 0;
    ds_u8(DS_window_overflow) = 0;
    const u8 corner_x = x_high & 0xFC, corner_y = y_high & 0xFC;
    ds_u8(DS_window_visibility) = u8(visibility_bits(ds_u8(u16(DS_world_grid + cell))) ^ 0xFF);
    for (int k = 0; k < 9; k++) {
        const u8 n = seg_u8(CSSEG_neighbour_order, u16(CS_neighbour_order + order));
        order = u16(order + 1);
        const u16 rec = u16(CS_neighbour_records + n * 5);
        if (seg_u8(CSSEG_neighbour_records, rec) & ds_u8(DS_window_visibility)) continue;
        const u8 dh = u8(corner_x + seg_u8(CSSEG_neighbour_records, u16(rec + 1)));
        const u8 dl = u8(corner_y + seg_u8(CSSEG_neighbour_records, u16(rec + 2)));
        const u16 bx = u16(cell + seg_u16(CSSEG_neighbour_records, u16(rec + 3)));
        ds_u16(DS_tile_origin_y) = u16(dl << 8);
        ds_u16(DS_tile_origin_x) = u16(dh << 8);
        tile_load(ds_u8(u16(DS_world_grid + bx)));
        // The terrain structures (simulation §7.3) standing in this cell, last first.
        u16 s = ds_u16(DS_structure_count);
        while (!(--s & 0x8000)) {
            const u16 o = u16(s << 1);
            const u16 x = ds_u16(u16(DS_structure_x + o));
            if (((x >> 8) & 0xFC) != dh) continue;
            ds_u16(DS_tile_origin_x) = u16(x - 0x200);
            const u16 y = ds_u16(u16(DS_structure_y + o));
            if (((y >> 8) & 0xFC) != dl) continue;
            ds_u16(DS_tile_origin_y) = u16(y - 0x200);
            const u16 piece = ds_u16(u16(DS_structure_piece + o));
            tile_load(u8(u8(u8(piece) - 0x43) | (u8(piece >> 8) & 3) << 6));
        }
    }
    ds_u8(DS_window_visibility) ^= 0xFF;
}

// 0919:728c terrain_save_view (render3d.md §3): the view the terrain was last projected for.
void terrain_save_view()
{
    ds_u16(DS_saved_camera_qx) = ds_u16(DS_camera_qx);
    ds_u16(DS_saved_camera_qy) = ds_u16(DS_camera_qy);
    ds_u16(DS_saved_view_heading) = u16(ds_u8(DS_view_heading) << 8 | ds_u8(DS_view_heading_fraction));
    ds_u8(DS_saved_horizon) = ds_u8(DS_horizon_row);
}

// 0919:72a9 order_reset (render3d.md §3.5): the group A draw order 0, 1, ..., 511.
void order_reset()
{
    for (u16 i = 0; i < 0x200; i++) ds_u16(u16(DS_group_a_order + 2 * i)) = i;
}

// 0919:72c0 order_sort (render3d.md §3.5): the sort key of each group A entry (0 without a
// primitive; else the scales of its vertices, high byte the largest and low byte the middle one
// of the three, or the smaller one of a line's two), then bubble passes of keys and order,
// descending, each pass ending at the last swap of the one before; the sort stops when that swap
// was at entry 2 or below, so the first entries can stay out of order (kept).
void order_sort()
{
    auto key = [](u16 i) -> u16_m & { return ds_u16(u16(DS_group_a_key + u16(i << 1))); };
    u16 si = 0;
    if (si == ds_u16(DS_group_a_count)) return;
    do {
        const u16 so = u16(si << 1);
        u16 bx = ds_u16(u16(DS_group_a_order + so));
        const u8 ctrl = ds_u8(u16(DS_vertex_control + bx));
        bx = u16(bx << 1);
        u16 cx = 0;
        if (ctrl != 0) {
            u8 cl = u8(ds_u16(u16(DS_vertex_scale + bx)));
            if (ctrl & 0x80) cl = u8(ds_u16(u16(DS_vertex_scale - 2 + bx)));
            u8 ch = u8(ds_u16(u16(DS_vertex_scale + 2 + bx)));
            if (ctrl & 0x40) {
                if (ch < cl) std::swap(cl, ch);
            } else {
                if (ch < cl) std::swap(cl, ch);
                const u8 third = u8(ds_u16(u16(DS_vertex_scale + 4 + bx)));
                if (cl < third) {
                    cl = third;
                    if (ch < cl) std::swap(cl, ch);
                }
            }
            cx = u16(ch << 8 | cl);
        }
        ds_u16(u16(DS_group_a_key + so)) = cx;
        si = u16(u16(so >> 1) + 1);
    } while (si < ds_u16(DS_group_a_count));

    u16 di = ds_u16(DS_group_a_count);
    for (;;) {
        u16 i = 0;
        u16 dx = key(i);
        i++;
        for (;;) {  // until the first swap of the pass
            const u16 cx = key(i);
            if (dx < cx) break;
            dx = cx;
            if (++i >= di) return;
        }
        u16 last = 0;
        for (;;) {  // swap entries i - 1 and i; dx is the key moving down
            last = i;
            std::swap(key(u16(i - 1)), key(i));
            const u16 o = u16(i << 1);
            std::swap(ds_u16(u16(DS_group_a_order - 2 + o)), ds_u16(u16(DS_group_a_order + o)));
            bool again = false;
            while (++i < di) {
                const u16 cx = key(i);
                if (dx < cx) {
                    again = true;
                    break;
                }
                dx = cx;
            }
            if (!again) break;
        }
        di = last;
        if (di <= 2) return;
    }
}

// 0919:74c0 sky_water_vga (render3d.md §3.1): in the 256 x 64 view at 5028h of the page ES, rows
// CL (the rows already covered, D965) to BL - 1 = sky AX, two rows of colour 8 (the horizon
// line), then water D950 (0Eh during a flash D9B5) to row 63. The horizon D953 is decremented
// after use. Returns DI = the address of the horizon line's first row.
u16 sky_water_vga(u16 es, u16 ax, u8 bl, u8 cl)
{
    const u8 r = rol8(cl, 6);  // ror dl, 2
    const u16 dx = u16(u8((r & 0x3F) + cl) << 8 | (r & 0xC0));
    u16 di = u16(0x5028 + dx);
    bl = u8(bl - cl);
    if (bl != 0) {
        do fill_row(es, di, ax);
        while (--bl != 0);
    }
    bl = u8(0x40 - ds_u8(DS_horizon_row));
    ds_u8(DS_horizon_row)--;
    bl--;
    u8 water = ds_u8(u16(DS_scene_colours + 1));
    if (ds_u8(DS_screen_shake) != 0) water = 0x0E;
    const u16 horizon_di = di;
    fill_row(es, di, 0x0808);
    fill_row(es, di, 0x0808);
    bl--;
    do fill_row(es, di, u16(water << 8 | water));
    while (--bl != 0);
    return horizon_di;
}

// 0919:7458 water_marks_vga (render3d.md §3.2): one water mark per row from DI down, CX rows (CL
// counts down), mark BX (wrapping at 20h) at column D90D[mark] (DH:DL added to DI). The farthest
// rows (CL > 16h) get one pixel AL; nearer ones, once the mark is 0Bh old, larger marks in AL and
// AH growing with the age (11h, 17h, 1Bh) for CL <= 0Eh. A row past the view (DI >= 9EE8h) ends it.
void water_marks_vga(u16 es, u16 ax, u16 bx, u16 cx, u16 dx, u16 di)
{
    const u8 al = u8(ax), ah = u8(ax >> 8);
    do {
        u16 d = u16(di + u16((dx & 0xFF00) | ds_u8(u16(DS_water_mark_x + bx))));
        if (d >= 0x9EE8) return;
        mem_u8(es, d) = al;
        d = u16(d + 1);
        const u8 cl = u8(cx);
        if (cl <= 0x16) {
            const u8 age = ds_u8(u16(DS_water_mark_age + bx));
            if (age >= 0x0B) {
                mem_u8(es, d) = al;
                d = u16(d + 1);
                mem_u8(es, u16(d - 2)) = ah;
                mem_u8(es, u16(d - 0x140)) = al;
                if (cl <= 0x0E && age >= 0x11) {
                    bool wide = false;
                    if (age >= 0x17) {
                        mem_u8(es, u16(d - 1)) = ah;
                        mem_u8(es, u16(d - 0x140)) = ah;
                        if (age >= 0x1B) {
                            d = u16(d + 2);
                            mem_u8(es, u16(d - 0x27E)) = al;
                            wide = true;
                        }
                    }
                    if (!wide) {
                        mem_u8(es, d) = al;
                        d = u16(d + 1);
                    }
                    mem_u8(es, u16(d - 0x140)) = al;
                    mem_u8(es, u16(d + 0x13E)) = al;
                }
            }
        }
        di = u16(di + 0x140);
        bx = u16(bx + 1);
        bx = u16((bx & 0xFF00) | (bx & 0x1F));
        cx = u16(u8(cx) - 1);
    } while (cx != 0);
}

// 0919:736c terrain_setup (render3d.md §3.1, §3.2): the horizon row D953 from the pitch, the sky,
// horizon line and water of the view (sky 0Fh/0Eh while the flash counter D9B5 runs down), the
// water marks moved by the view's turn and aged (a mark 20h old respawns at a column taken from
// the RNG state bytes), and with detail high the marks drawn on the 32 rows from the horizon line.
void terrain_setup()
{
    const u16 es = ds_u16(DS_draw_page_segment);
    u8 bl = u8(ds_u8(DS_view_pitch) - 0x42 + 0x15);
    u8 cl = u8(s8(u8(ds_u8(DS_pitch_reference) - 0x80)) >> 3);
    bl = u8(bl - cl);
    if (bl > 0x3D) bl = 0x3D;
    ds_u8(DS_horizon_row) = bl;
    cl = ds_u8(DS_sky_top);
    if (cl > bl) cl = bl;
    ds_u8(DS_sky_top) = bl;
    u8 sky = ds_u8(DS_scene_colours);
    if (ds_u8(DS_screen_shake) != 0) {
        ds_u8(DS_screen_shake)--;
        sky = 0x0E;
        if (ds_u8(DS_screen_shake) != 0) sky = 0x0F;
    }
    const u8 mode = u8(ds_u16(DS_video_mode));
    // PORT: EGA (4ce2), Tandy (5529) and CGA (4587) sky and water are parked.
    if (mode <= 0x0D) render_parked("the sky and water (sky_water_ega/tandy/cga)");
    const u16 di = sky_water_vga(es, u16(sky << 8 | sky), bl, cl);

    u16 fresh = ds_u16(DS_rng_state);
    const u8 heading = ds_u8(DS_view_heading);
    const u8 turn = u8(u8(heading - ds_u8(DS_last_view_heading)) << 1);
    ds_u8(DS_last_view_heading) = heading;
    for (int k = 0x1F; k >= 0; k--) {
        u8 &x = ds_u8(u16(DS_water_mark_x + k));
        x = u8(x - turn);
        u8 age = u8(ds_u8(u16(DS_water_mark_age + k)) + 2);
        if (age >= 0x20) {
            x = u8(fresh);
            fresh = u16(fresh << 8 | fresh >> 8);
            age = 0;
        }
        ds_u8(u16(DS_water_mark_age + k)) = age;
    }
    if (ds_u8(DS_detail_low) != 0) return;
    const u16 first = (ds_u8(DS_water_phase) >> 2) & 0x1F;
    const u8 water = ds_u8(u16(DS_scene_colours + 1));
    // DX = mode (DH = 0): water_marks_vga replaces DL before it adds DX.
    water_marks_vga(es, u16(water << 8 | u8(water | 7)), first, 0x20, mode, di);
}

// 0919:7523 project (render3d.md §3.3, simulation.md §10): vertices SI .. AX - 1. The bearing word
// from the octant and the atan table, relative to the view word D190, -> 2C96; the scale (the
// halved sine of the table angle over the larger distance, FFh when very close) -> 4496; the row
// ((200h | scale) - scale * height / 32, >= 0, / 8, + horizon + 1; the low byte added without
// carry) -> 3496. A very close vertex next to a zero-height one within the bow (forward) or stern
// (reverse) window of the hull heading is a shore-contact candidate: the first in D901, the last
// of the others in D903 (as byte offsets 2i).
void project(u16 si, u16 ax)
{
    ds_u16(DS_scratch_b7dc) = ax;
    while (si < ds_u16(DS_scratch_b7dc)) {
        const u16 s = u16(si << 1);
        u8 octant = 0;
        u16 cx = u16(ds_u16(u16(DS_vertex_x + s)) - ds_u16(DS_camera_qx));
        if (cx & 0x8000) {
            octant = 3;
            cx = u16(-cx);
        }
        u16 dx = u16(ds_u16(u16(DS_vertex_y + s)) - ds_u16(DS_camera_qy));
        if (dx & 0x8000) {
            dx = u16(-dx);
            octant ^= 1;
        }
        if (cx >= dx) {
            std::swap(cx, dx);
            octant ^= 4;
        }
        ds_u8(DS_atan_octant) = octant;
        const u16 near = cx, far = dx;
        u8 ratio = 0xFF;
        if (near != far) {
            ratio = 0;
            if (far != 0) ratio = u8(div32_16(u32(near) << 16, far) >> 8);
        }
        const u8 raw = seg_u8(CSSEG_atan_table, u16(CS_atan_table + ratio));
        ds_u8(DS_atan_raw) = raw;
        const u8 r3 = rol8(raw, 5);  // ror al, 3
        u16 bearing = u16(u16(r3 << 8 | r3) & 0x1FE0);
        if (ds_u8(u16(DS_octant_negate + octant)) != 0) bearing = u16(-bearing);
        bearing = u16(u8(u8(bearing >> 8) + ds_u8(u16(DS_octant_base_view + octant))) << 8 | u8(bearing));
        bearing = u16(bearing - ds_u16(DS_view_heading_low));
        ds_u16(u16(DS_vertex_bearing + s)) = bearing;
        const u8 index = u8(u8(-u8(raw >> 1) - 1) & 0xFE);
        const u16 sine = u16(seg_u16(CSSEG_sine_table, u16(CS_sine_table + index)) >> 1);
        u8 scale = 0xFF;
        if ((far >> 8) != 0 || u8(far) > u8(sine >> 8)) scale = u8(div32_16(sine, far));
        const u16 height = ds_u16(u16(DS_vertex_height + s));
        if (scale == 0xFF && (height == 0 || ds_u16(u16(DS_vertex_height + s + 2)) == 0)) {
            const u8 rel = u8(u8(bearing >> 8) + ds_u8(DS_view_heading) - ds_u8(DS_heading) - 0x14);
            const bool reverse = ds_u8(DS_speed) & 0x80;
            const bool candidate = (rel & 0x80) ? (reverse && rel < 0xD8) : (!reverse && rel < 0x58);
            if (candidate) {
                if (ds_u16(DS_contact_candidate_a) == 0xFFFF) ds_u16(DS_contact_candidate_a) = s;
                else ds_u16(DS_contact_candidate_b) = s;
            }
        }
        ds_u16(u16(DS_vertex_scale + s)) = scale;
        u16 row = u16(0x200 | scale);
        row = u16(row - (u16(scale * u8(height)) >> 5));
        if (row & 0x8000) row = 0;
        row >>= 3;
        row = u16((row & 0xFF00) | u8(u8(row) + ds_u8(DS_horizon_row)));
        ds_u16(u16(DS_vertex_row + s)) = u16(row + 1);
        si = u16(u16(s >> 1) + 1);
    }
}

// 0919:7a3e shore_edge_test (simulation.md §10): the relative bearing (DH + CL) of a vertex next to
// the contact candidate (relative bearing CH) folded to the half-turn of the candidate; contact
// (D8FF = 1) when the two lie on different sides of 2Ch (CH compared against ACh when behind).
void shore_edge_test(u16 cx, u16 dx)
{
    const u8 cl = u8(cx), ch = u8(cx >> 8);
    u8 dh = u8(u8(dx >> 8) + cl);
    const u8 limit = (ch & 0x80) ? 0xAC : 0x2C;
    if (ch & 0x80) dh = u8(dh - 0x80);
    if ((dh & 0x80) && dh <= 0xC0) dh = 0x7F;
    const bool beyond = s8(dh) >= 0x2C;
    if (ch < limit ? beyond : !beyond) ds_u8(DS_contact) = 1;
}

// 0919:79e1 shore_contact_test (simulation.md §10): a candidate SI (2i, or FFFFh for none) against
// the vertices of its primitives: i - 2 (unless mode bit 7), i - 1, and if i has a primitive
// (colour -> D8FE) i + 1 and, for a triangle without bit 7, i + 2.
void shore_contact_test(u16 si)
{
    if (si == 0xFFFF) return;
    const u16 bx = u16(si >> 1);
    const u8 cl = u8(ds_u8(DS_view_heading) - ds_u8(DS_heading) - 0x14);
    const u8 ch = u8(u8(ds_u16(u16(DS_vertex_bearing + si)) >> 8) + cl);
    const u16 cx = u16(ch << 8 | cl);
    if (si >= 2) {
        const u8 before = ds_u8(u16(DS_vertex_control - 2 + bx));
        if (before != 0 && !(before & 0x80)) shore_edge_test(cx, ds_u16(u16(DS_vertex_bearing - 4 + si)));
    }
    if (ds_u8(u16(DS_vertex_control - 1 + bx)) != 0) shore_edge_test(cx, ds_u16(u16(DS_vertex_bearing - 2 + si)));
    const u8 ctrl = ds_u8(u16(DS_vertex_control + bx));
    ds_u8(DS_contact_colour) = ctrl;
    if (ctrl == 0) return;
    shore_edge_test(cx, ds_u16(u16(DS_vertex_bearing + 2 + si)));
    if (ctrl & 0x80) return;
    shore_edge_test(cx, ds_u16(u16(DS_vertex_bearing + 4 + si)));
}

// 0919:3f8d colour_remap (world.md §5): the bytes [DI, DX) of DGROUP through the table at SI into
// ES:DI: CGA every byte, EGA and Tandy the bytes >= 10h through SI + 10h; VGA leaves them. At
// least one byte is written in those modes (the test is at the end of the loop).
void colour_remap(u16 es, u16 di, u16 dx, u16 si)
{
    const u8 mode = u8(ds_u16(DS_video_mode));
    if (mode == 4) {
        do {
            mem_u8(es, di) = ds_u8(u16(ds_u8(di) + si));
            di = u16(di + 1);
        } while (di < dx);
    } else if (mode == 9 || mode == 0x0D) {
        do {
            u8 al = ds_u8(di);
            if (al >= 0x10) al = ds_u8(u16(al + si + 0x10));
            mem_u8(es, di) = al;
            di = u16(di + 1);
        } while (di < dx);
    }
}

// 0919:3fba video_mode_setup (world.md §5, render3d.md §1.2): the span routines D8F8/D8FA and the
// 64 view row addresses D74D of the video mode (VGA 320-byte rows at 5028h, EGA 40-byte planes,
// Tandy four 2000h banks, CGA two banks). The pointers are data here; only the VGA pair is ever
// called by the port.
void video_mode_setup()
{
    const u8 mode = u8(ds_u16(DS_video_mode));
    u16 di;
    if (mode > 0x0D) {
        ds_u16(DS_span_routine_a) = 0x788E;
        ds_u16(DS_span_routine_b) = 0x7943;
        di = 0x5028;
        for (u16 i = 0; i < 0x40; i++, di = u16(di + 0x140)) ds_u16(u16(DS_view_row_table + 2 * i)) = di;
    } else if (mode == 0x0D) {
        ds_u16(DS_span_routine_a) = 0x4DFA;
        ds_u16(DS_span_routine_b) = 0x4EB6;
        di = 0x0A05;
        for (u16 i = 0; i < 0x40; i++, di = u16(di + 0x28)) ds_u16(u16(DS_view_row_table + 2 * i)) = di;
    } else if (mode >= 9) {
        ds_u16(DS_span_routine_a) = 0x5693;
        ds_u16(DS_span_routine_b) = 0x5756;
        di = 0x0A14;
        for (u16 i = 0; i < 0x40; i++) {
            ds_u16(u16(DS_view_row_table + 2 * i)) = di;
            di = u16((di + 0x2000) & 0x7FFF);
            if (!(di & 0x6000)) di = u16(di + 0xA0);
        }
    } else {
        ds_u16(DS_span_routine_a) = 0x46E8;
        ds_u16(DS_span_routine_b) = 0x47CF;
        di = 0x0A0A;
        for (u16 i = 0; i < 0x40; i++) {
            ds_u16(u16(DS_view_row_table + 2 * i)) = di;
            di ^= 0x2000;
            if (!(di & 0x2000)) di = u16(di + 0x50);
        }
    }
}

} // namespace gb
