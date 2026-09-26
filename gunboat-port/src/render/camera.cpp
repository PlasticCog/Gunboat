// Camera, angles and the chase-view terrain test (render3d.md §4, simulation.md §5.3).
#include "render/render.hpp"

#include <utility>

#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

void render_parked(const char *what)
{
    host_fatal("render: %s is not ported (EGA/Tandy/CGA parked)", what);
}

namespace {

// The 1/4-unit camera coordinate: the map position shifted left twice with the top bits of the
// fraction byte rotated in (shl bl / rcl cx, twice).
u16 quarter(u16 v, u8 fraction) { return u16(v << 2 | fraction >> 6); }

} // namespace

// 0919:3712 atan (simulation.md §4.6, render3d.md §5.2): the angle byte of the vector (CX, DX).
// The octant goes to B7F5 (bit 0 = dy < 0, bits 0-1 = 3 for dx < 0, bit 2 = |dx| >= |dy|); the
// ratio min/max (as its high byte, FFh when equal) indexes the atan table, whose value goes to
// D730 and, as 1/8 steps, to B7E3 (whole) and B7E2 (the low 3 bits); in the octants of
// octant_negate both are negated (B7E2 as 7 - it). AL = B7E3 + octant_base[octant].
AtanOut atan(u16 cx, u16 dx)
{
    u8 octant = 0;
    if (cx & 0x8000) {
        octant = 3;
        cx = u16(-cx);
    }
    if (dx & 0x8000) {
        dx = u16(-dx);
        octant ^= 1;
    }
    if (cx >= dx) {
        std::swap(cx, dx);
        octant ^= 4;
    }
    ds_u8(DS_atan_octant) = octant;
    // cx = min, dx = max; DX:0 / max with DX = min (no overflow: min < max)
    u16 ratio = 0xFF;
    if (cx != dx) {
        ratio = 0;
        if (dx != 0) ratio = u16(div32_16(u32(cx) << 16, dx) >> 8);
    }
    u8 al = seg_u8(CSSEG_atan_table, u16(CS_atan_table + ratio));
    ds_u8(DS_atan_raw) = al;
    const u8 ah = al & 7;
    ds_u8(DS_scratch_b7e3) = u8(al >> 3);
    ds_u8(DS_scratch_b7e2) = ah;
    if (ds_u8(u16(DS_octant_negate + octant)) != 0) {
        ds_u8(DS_scratch_b7e3) = u8(-ds_u8(DS_scratch_b7e3));
        ds_u8(DS_scratch_b7e2) = u8(-ds_u8(DS_scratch_b7e2) + 7);
    }
    al = u8(ds_u8(DS_scratch_b7e3) + ds_u8(u16(DS_octant_base + octant)));
    return {u16(ah << 8 | al), cx, dx};
}

// 0919:8331 polar_small (simulation.md §5.3, §8.5): the offset (CX, DX) of a point DL units away
// at angle BL, from the high bytes of the sine table, each -2 * (DL * sin >> 8), turned into the
// angle's quadrant.
CxDx polar_small(u8 bl, u8 dl)
{
    u8 quadrant = 0;
    if (bl & 0x80) quadrant += 2;
    if (bl & 0x40) quadrant += 1;
    ds_u8(DS_atan_octant) = quadrant;
    const u16 bx = u16(u8(bl << 2));
    auto part = [dl](u16 index) {
        const u8 hi = u8(seg_u16(CSSEG_sine_table, u16(CS_sine_table + index)) >> 8);
        return u16(-u16(u16(dl * hi) >> 8) << 1);
    };
    u16 cx = part(bx);
    u16 dx = part(u16(-bx + 0x100));
    if (quadrant == 1) {
        std::swap(cx, dx);
        dx = u16(-dx);
    } else if (quadrant == 2) {
        cx = u16(-cx);
        dx = u16(-dx);
    } else if (quadrant == 3) {
        std::swap(cx, dx);
        cx = u16(-cx);
    }
    return {cx, dx};
}

// 0919:7c38 terrain_rect_test (simulation.md §5.3): scratch B7E2 = 1 if the point (CX, DX) in 1/4
// units is on land as the loaded terrain window sees it, else 0. A camera cell outside the
// window's centre cell and its loaded neighbours counts as land. Inside, every group A primitive
// (in draw order, all but the last two entries) and every group B vertex from 200h (all but the
// last three) is a candidate: the point is "in" it when on each axis it lies between the first
// vertex's coordinate and one of the next two (the crude box test of the original).
void terrain_rect_test(u16 cx, u16 dx)
{
    ds_u8(DS_scratch_b7e2) = 0;
    const u8 row = u8(9 - u8(u8(ds_u8(u16(DS_camera_y + 1)) - 4) >> 2));
    const u16 cell = u16(u16(row * 0x11) + u16(u16(u8(ds_u8(u16(DS_camera_x + 1)) - 4)) >> 2) + 1);
    const u16 centre = ds_u16(DS_terrain_rebuild);
    if (cell != centre) {
        const u8 tile = ds_u8(u16(DS_world_grid + centre));
        const u8 rot = u8(u8(tile << 3 | tile >> 5) & 6);
        u8 mask = seg_u8(CSSEG_tile_visibility, u16(CS_tile_visibility + (tile & 0x3F)));
        if (rot) mask = u8(mask << rot | mask >> (8 - rot));
        bool loaded = false;
        u16 si = u16(CS_neighbour_records + 3);
        do {
            const bool bit = mask & 1;
            mask >>= 1;
            if (bit && u16(seg_u16(CSSEG_neighbour_records, si) + centre) == cell) {
                loaded = true;
                break;
            }
            si = u16(si + 5);
        } while (mask);
        if (!loaded) {
            ds_u8(DS_scratch_b7e2)++;
            return;
        }
    }
    // Is the point between the first coordinate a and one of b, c (unsigned)?
    auto between = [](u16 v, u16 a, u16 b, u16 c) {
        if (v >= a) return v <= b || v <= c;
        return v >= b || v >= c;
    };
    // Vertex i's box test; the mode-2 primitives (bit 7) start at vertex i - 1.
    auto inside = [&](u16 i) {
        const u8 ctrl = ds_u8(u16(DS_vertex_control + i));
        if ((ctrl & 0x40) || ctrl == 0) return false;
        const u16 b = u16(2 * i);
        const u16 first = (ctrl & 0x80) ? u16(b - 2) : b;
        if (!between(cx, ds_u16(u16(DS_vertex_x + first)), ds_u16(u16(DS_vertex_x + b + 2)),
                     ds_u16(u16(DS_vertex_x + b + 4))))
            return false;
        return between(dx, ds_u16(u16(DS_vertex_y + first)), ds_u16(u16(DS_vertex_y + b + 2)),
                       ds_u16(u16(DS_vertex_y + b + 4)));
    };
    u16 n = u16(ds_u16(DS_group_a_count) - 2);
    if (n != 0 && !(n & 0x8000)) {
        ds_u16(DS_scratch_b7e0) = n;
        u16 k = 0;
        do {
            if (inside(ds_u16(u16(DS_group_a_order + k)))) {
                ds_u8(DS_scratch_b7e2)++;
                return;
            }
            k = u16(k + 2);
        } while (--ds_u16(DS_scratch_b7e0) != 0);
    }
    const u16 end = u16(ds_u16(DS_group_b_count) - 3);
    if (end & 0x8000) return;
    ds_u16(DS_scratch_b7dc) = u16(end + 0x200);
    u16 i = 0x200;
    do {
        if (inside(i)) {
            ds_u8(DS_scratch_b7e2)++;
            return;
        }
        i++;
    } while (i < ds_u16(DS_scratch_b7dc));
}

// 0919:82de chase_view_collision (simulation.md §5.3): in chase view, every eighth world pass, the
// boat's own position is tested; on land it goes back to the last good position, else that
// position is remembered.
void chase_view_collision()
{
    if (ds_u8(DS_chase_view) == 0 || (ds_u8(DS_world_pass_counter) & 7) != 0) return;
    terrain_rect_test(quarter(ds_u16(DS_object_x), ds_u8(DS_boat_x_fraction)),
                      quarter(ds_u16(DS_object_y), ds_u8(DS_boat_y_fraction)));
    if (ds_u8(DS_scratch_b7e2) != 0) {
        ds_u16(DS_object_x) = ds_u16(DS_last_good_x);
        ds_u16(DS_object_y) = ds_u16(DS_last_good_y);
    } else {
        ds_u16(DS_last_good_x) = ds_u16(DS_object_x);
        ds_u16(DS_last_good_y) = ds_u16(DS_object_y);
    }
}

// 0919:8229 camera_position (simulation.md §5.3): the camera at the boat, or in chase view
// polar_small(chase heading, distance) away from it; a chase camera on land is turned -8, +8,
// -16, +16, +24, +32 ... heading units from the heading it started with (D96C and the view heading
// follow) until it is clear; after 20h tries the chase view is switched off. Returns the camera
// in 1/4 units (CX, DX); D96E/D970 get it in map units.
CxDx camera_position()
{
    chase_view_collision();
    ds_u8(DS_scratch_b7e3) = 0;
    ds_u8(DS_vec_product_lo) = ds_u8(DS_chase_heading);  // B7E9: the heading the tries start from
    for (;;) {
        u16 cx = 0, dx = 0;
        if (ds_u8(DS_chase_view) != 0) {
            const CxDx p = polar_small(ds_u8(DS_chase_heading), ds_u8(DS_chase_distance));
            cx = p.cx;
            dx = p.dx;
        }
        cx = u16(cx + ds_u16(DS_object_x));
        dx = u16(dx + ds_u16(DS_object_y));
        if (ds_u8(DS_chase_view) == 0) {
            ds_u16(DS_last_good_x) = cx;
            ds_u16(DS_last_good_y) = dx;
        }
        ds_u16(DS_camera_x) = cx;
        ds_u16(DS_camera_y) = dx;
        cx = quarter(cx, ds_u8(DS_boat_x_fraction));
        dx = quarter(dx, ds_u8(DS_boat_y_fraction));
        if (ds_u8(DS_chase_view) == 0) return {cx, dx};
        terrain_rect_test(cx, dx);
        if (ds_u8(DS_scratch_b7e2) == 0) return {cx, dx};
        const u8 tries = ++ds_u8(DS_scratch_b7e3);
        u8 ah = ds_u8(DS_vec_product_lo);
        if (tries > 0x20) {
            ds_u8(DS_chase_view) = 0;
            ds_u8(DS_scene_rebuild) = 1;
            ds_u8(DS_chase_view_reset) = 1;
            return {cx, dx};
        }
        u8 al = tries;
        ah = u8(ah - 8);
        if (--al != 0) {
            ah = u8(ah + 0x10);
            if (--al != 0) {
                ah = u8(ah - 0x18);
                if (--al != 0) {
                    ah = u8(ah + 0x18);
                    do ah = u8(ah + 8);
                    while (--al != 0);
                }
            }
        }
        ds_u8(DS_chase_heading) = ah;
        ds_u8(DS_view_heading) = ah;
    }
}

} // namespace gb
