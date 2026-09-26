// The crew pilot's decisions and the river route network (simulation.md §4.5, §4.6): the waypoints
// of TILE.BIN (link, x, y), the search for a waypoint ahead, the step to the next one (forks by
// the branch command, the crossing into the next tile), and the captain's throttle and jet keys.
#include "game/sim.hpp"

#include "mem.hpp"
#include "render/render.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Scratch bytes the route code borrows (their names come from other users of the addresses).
constexpr u16 SCAN_INDEX = DS_vec_product_lo;   // B7E9: the waypoint index being scanned
constexpr u16 SCAN_BEST = DS_vec_product_hi;    // B7EA: route_find's nearest waypoint so far
constexpr u16 SCAN_AHEAD = DS_radar_steps;      // B7ED: route_find's count of later waypoints ahead
constexpr u16 SCAN_DIST = DS_scratch_b7dc;      // B7DC: route_find's nearest distance
constexpr u16 ENTRY_X = DS_vec_factor;          // B7E5: the entry X route_advance looks for (0: Y)
constexpr u16 ENTRY_Y = u16(DS_vec_factor + 1); // B7E6: the entry Y

u16 set_cl(u16 cx, u8 cl) { return u16((cx & 0xFF00) | cl); }

} // namespace

// 0919:8754 route_point (simulation.md §4.6): waypoint CL of the tile in grid cell BX (row BX / 17,
// column BX mod 17; a cell of 4352 or more is a divide error). The tile origin D9A9 = column * 400h,
// D9AB = (28h - 4 * row) << 8; the tile's rotation (grid bits 6-7) -> B7E2. The TILE.BIN record
// (counts A, B and objects, then the waypoint count) is followed by 4 bytes per vertex and object
// (the three counts summed in 8 bits) and 3 per waypoint: link, x, y. An index past the count
// returns DH = FFh (AX = 2 * tile, CX unchanged); else DH = the link, turned with the tile when it
// is a positive direction ((link - 1 + rotation) & FBh) + 1, and AX, CX = the point turned and
// placed by route_rotate. BX and ES are kept; DL = 28h - 4 * row.
RoutePoint route_point(u16 cx, u16 bx)
{
    u16 ax = bx;
    if (ax != 0) ax = div16_8(ax, 0x11);  // AL = row, AH = column
    const u8 dl = u8(-u8(u8(ax) << 2) + 0x28);
    const u8 dh = u8(u8(ax >> 8) << 2);
    ds_u16(DS_tile_origin_x) = u16(dh << 8);
    ds_u16(DS_tile_origin_y) = u16(dl << 8);
    const u8 cell = ds_u8(u16(DS_world_grid + bx));
    const u16 es = ds_u16(DS_tile_bin_segment);
    ds_u8(DS_scratch_b7e2) = u8(cell >> 6);
    ax = u16((cell & 0x3F) << 1);
    const u16 base = ds_u16(DS_tile_bin_offset);
    u16 si = u16(mem_u16(es, u16(ax + base)) + base);
    const u8 sum = u8(mem_u8(es, si) + mem_u8(es, u16(si + 1)) + mem_u8(es, u16(si + 2)));
    si = u16(si + 3);
    if (u8(cx) >= mem_u8(es, si)) return {ax, cx, u16(0xFF00 | dl), si};
    si = u16(si + 1);
    si = u16(si + u16(sum << 2));
    cx &= 0x00FF;
    si = u16(si + 3 * cx);
    u8 link = mem_u8(es, si);
    if (link != 0 && !(link & 0x80)) link = u8(u8(u8(link - 1 + ds_u8(DS_scratch_b7e2)) & 0xFB) + 1);
    si = u16(si + 1);
    const u8 x = mem_u8(es, si);
    si = u16(si + 1);
    const u8 y = mem_u8(es, si);
    const AxCx p = route_rotate(x, y);
    return {p.ax, p.cx, u16(link << 8 | dl), si};
}

// 0919:87e8 route_advance (simulation.md §4.6): the next waypoint after the one with link DH at
// index CL in cell BX. No waypoint (DH = FFh): the answer 22h "-I don't know where to go!" (unless
// the mission is ending) and the search state 2. Forward (D684 = 0): links C0h and above and 40h-7Fh
// step +1; 80h-BFh (a fork) step by its low 6 bits when the branch command is 1 (left), else +1;
// 1-3Fh leave the tile. Backward: links C0h and above step by the link as a negative number, 0 and
// 1-3Fh step -1, 40h-7Fh leave the tile; at a fork, after a fork (D688 >= F0h) the direction turns
// forward and +1, or stays backward and -1 with the branch command 2 (right); before one, -1, or
// forward by the low 6 bits with the branch command 2. The new index, the previous one, the link,
// the cell and the point are stored and the state is 1 (following). Leaving the tile (link - 1) & 3:
// the cell moves by D68F[edge] and the new tile's waypoints are searched for the entry X (or Y) of
// D697[edge] in 1/8 units, 7 bits; a match gives the direction (forward unless its link is 40h or
// more) and becomes the waypoint; none turns the boat around (D684 toggled) and advances again from
// the current waypoint (the recursion ends at the next level for D684 = 0 or 1, the only values
// the game writes). Returns DX (DH = the new waypoint's link, FFh at the end of a route).
u16 route_advance(u16 dx, u16 cx, u16 bx)
{
    u8 dh = u8(dx >> 8);
    if (dh == 0xFF) {
        if (ds_u8(DS_message_state) != 2) {
            ds_u8(DS_message_state) = 1;
            ds_u8(DS_message_reply) = 0x22;
            ds_u8(DS_message_timer) = 0x0C;
        }
        ds_u8(DS_crew_pilot_state) = 2;
        return dx;
    }
    u8 step;
    bool edge = false;
    if (ds_u8(DS_route_direction) == 0) {
        step = 1;
        if (dh < 0xC0) {
            if (dh & 0x80) {
                if (ds_u8(DS_branch_command) == 1) step = dh & 0x3F;
            } else if (dh != 0 && dh < 0x40) {
                edge = true;
            }
        }
    } else {
        step = dh;
        if (dh < 0xC0) {
            step = 0xFF;
            if (dh & 0x80) {
                if (ds_u8(DS_route_link) >= 0xF0) {
                    step = 1;
                    ds_u8(DS_route_direction) = 0;
                    if (ds_u8(DS_branch_command) == 2) {
                        step = 0xFF;
                        ds_u8(DS_route_direction) ^= 1;
                    }
                } else if (ds_u8(DS_branch_command) == 2) {
                    step = dh & 0x3F;
                    ds_u8(DS_route_direction) = 0;
                }
            } else if (dh >= 0x40) {
                edge = true;
            }
        }
    }
    if (edge) {
        ds_u8(DS_route_link) = 0;
        const u16 e = u16(u8(dh - 1) & 3);
        bx = u16(bx + ds_u16(u16(DS_route_cell_step + 2 * e)));
        const u16 entry = ds_u16(u16(DS_route_entry_coord + 2 * e));
        ds_u8(ENTRY_X) = u8(entry);
        ds_u8(ENTRY_Y) = u8(entry >> 8);
        ds_u8(SCAN_INDEX) = 0;
        for (;;) {
            cx = set_cl(cx, ds_u8(SCAN_INDEX));
            const RoutePoint p = route_point(cx, bx);
            ds_u8(SCAN_INDEX)++;
            dx = p.dx;
            cx = p.cx;
            if (u8(dx >> 8) == 0xFF) {  // no entry waypoint: turn around, advance from the current one
                ds_u8(DS_route_direction) ^= 1;
                const RoutePoint q = route_point(set_cl(cx, ds_u8(DS_route_index)), ds_u16(DS_route_cell));
                return route_advance(q.dx, set_cl(q.cx, ds_u8(DS_route_index)), ds_u16(DS_route_cell));
            }
            const u8 x = u8(p.ax >> 3) & 0x7F;
            cx = u16(cx >> 3);
            cx = set_cl(cx, u8(cx) & 0x7F);
            if (ds_u8(ENTRY_X) != 0 ? x == ds_u8(ENTRY_X) : u8(cx) == ds_u8(ENTRY_Y)) break;
        }
        dh = u8(dx >> 8);
        cx = set_cl(cx, u8(ds_u8(SCAN_INDEX) - 1));
        ds_u8(DS_route_direction) = 0;
        if (dh < 0x40) ds_u8(DS_route_direction) ^= 1;
    } else {
        cx = set_cl(cx, u8(u8(cx) + step));
    }
    const u8 previous = ds_u8(DS_route_index);
    ds_u8(DS_route_index) = u8(cx);
    ds_u8(DS_route_prev_index) = previous;
    ds_u8(DS_route_link) = dh;
    ds_u16(DS_route_cell) = bx;
    const RoutePoint p = route_point(set_cl(cx, ds_u8(DS_route_index)), bx);
    ds_u16(DS_route_x) = p.ax;
    ds_u16(DS_route_y) = p.cx;
    ds_u8(DS_crew_pilot_state) = 1;
    return p.dx;
}

// 0919:1c00 route_find (simulation.md §4.6): the crew pilot's search (state 2). The cell of the
// boat (row 10 - (Y >> 10), column X >> 10, in 8 bits) goes to D689; with no waypoint there the
// rate is 0 and the search goes on next time. Else the waypoints up to the first "none" (DH = FFh:
// the end, or a waypoint with link FFh) within 28h of the hull heading are the candidates, the
// nearest by |dx| + |dy| (16 bits) wins. In the missions (not practice) each candidate sets the
// direction: 0 when it is not nearer, 1 when it is nearer than an earlier one. B7ED counts the
// candidates after the first from FFh: with exactly two (B7ED = 0) the direction becomes 1, or 0
// when the winner's link has bit 6 set. The winner becomes the waypoint (link 0, state 1).
void route_find()
{
    const u8 row = u8(-u8(ds_u8(u16(DS_object_y + 1)) >> 2) + 0x0A);
    const u16 bx = u16((ds_u8(u16(DS_object_x + 1)) >> 2) + u16(row * 0x11));
    ds_u16(DS_route_cell) = bx;
    // PORT: CH of the first call is route_find's entry CX in the original; it only reaches the
    // registers on the early return (DH = FFh), which no caller reads.
    RoutePoint p = route_point(0, bx);
    if (u8(p.dx >> 8) == 0xFF) {
        ds_u8(DS_crew_pilot_rate) = 0;
        return;
    }
    u16 cx = 0xFFFF;
    ds_u16(SCAN_DIST) = 0xFFFF;
    ds_u8(SCAN_INDEX) = 0xFF;
    ds_u8(SCAN_BEST) = 0xFF;
    ds_u8(SCAN_AHEAD) = 0xFF;
    for (;;) {
        ds_u8(SCAN_INDEX)++;
        p = route_point(set_cl(cx, ds_u8(SCAN_INDEX)), bx);
        cx = p.cx;
        if (u8(p.dx >> 8) == 0xFF) break;
        const AtanOut a = atan(u16(p.ax - ds_u16(DS_object_x)), u16(p.cx - ds_u16(DS_object_y)));
        if (u8(u8(a.ax) - ds_u8(DS_heading) + 0x28) > 0x50) continue;
        if (ds_u8(SCAN_BEST) != 0xFF) ds_u8(SCAN_AHEAD)++;
        u16 ax = u16(p.ax - ds_u16(DS_object_x));
        if (ax & 0x8000) ax = u16(-ax);
        cx = u16(p.cx - ds_u16(DS_object_y));
        if (cx & 0x8000) cx = u16(-cx);
        ax = u16(ax + cx);
        if (ax >= ds_u16(SCAN_DIST)) {
            if (ds_u16(DS_practice_mode) == 0) ds_u8(DS_route_direction) = 0;
            continue;
        }
        ds_u16(SCAN_DIST) = ax;
        const u8 index = ds_u8(SCAN_INDEX);
        if (ds_u8(SCAN_BEST) != 0xFF && ds_u16(DS_practice_mode) == 0) ds_u8(DS_route_direction) = 1;
        ds_u8(SCAN_BEST) = index;
    }
    const u8 best = ds_u8(SCAN_BEST);
    if (best == 0xFF) {
        ds_u8(DS_crew_pilot_rate) = 0;
        return;
    }
    ds_u8(DS_route_link) = 0;
    ds_u8(DS_route_index) = best;
    p = route_point(set_cl(cx, best), bx);
    ds_u16(DS_route_x) = p.ax;
    ds_u16(DS_route_y) = p.cx;
    ds_u8(DS_crew_pilot_state) = 1;
    if (ds_u8(SCAN_AHEAD) == 0) {
        ds_u8(DS_route_direction) = 0;
        if (!(p.dx & 0x4000)) ds_u8(DS_route_direction) = 1;
    }
}

// 0919:1b2d crew_pilot_decide (simulation.md §4.5): the captain's keys D682 and rate index D683.
// Throttle: the lower throttle (& FCh) above the target D680 (& FCh) -> down (2), below -> up (1)
// while the target is above 8, else nothing. State 0: throttle only; 2: route_find; above 2:
// nothing. State 1: the bearing of the waypoint relative to the hull + 80h; at 48h or less the jet
// goes left (4), at B8h or more right (8), with rate 0; between, rate 1 (2 within 70h-90h) and the
// jet angle it wants, ((rel - 40h) * 2 + 2) & FCh, against twice the jet angle B818 (8 bits): left
// when below, right when above. Within 16 units of the waypoint on both axes the next waypoint is
// taken (route_point, route_advance).
void crew_pilot_decide()
{
    const u8 target = ds_u8(DS_crew_throttle_target) & 0xFC;
    u8 lower = ds_u8(u16(DS_throttle + 1));
    if (lower > ds_u8(DS_throttle)) lower = ds_u8(DS_throttle);
    lower &= 0xFC;
    u8 keys = 0;
    if (lower != target) {
        keys = 2;
        if (lower < target) keys = ds_u8(DS_crew_throttle_target) > 8 ? 1 : 0;
    }
    ds_u8(DS_crew_pilot_keys) = keys;
    const u8 state = ds_u8(DS_crew_pilot_state);
    if (state == 0) return;
    if (state >= 2) {
        if (state == 2) route_find();
        return;
    }
    ds_u8(DS_crew_pilot_rate) = 0;
    const AtanOut a = atan(u16(ds_u16(DS_route_x) - ds_u16(DS_object_x)), u16(ds_u16(DS_route_y) - ds_u16(DS_object_y)));
    const u8 rel = u8(u8(a.ax) - ds_u8(DS_heading) + 0x80);
    ds_u8(DS_crew_pilot_rate) = 0;
    u8 jet = 4;
    if (rel > 0x48) {
        jet = 8;
        if (rel < 0xB8) {
            ds_u8(DS_crew_pilot_rate) = 1;
            if (rel >= 0x70 && rel <= 0x90) ds_u8(DS_crew_pilot_rate) = 2;
            const u8 want = u8(u8(u8(rel - 0x40) << 1) + 2) & 0xFC;
            const u8 diff = u8(want - u8(ds_u8(DS_jet_angle) << 1));
            jet = 0;
            if (diff != 0) jet = (diff & 0x80) ? 4 : 8;
        }
    }
    ds_u8(DS_crew_pilot_keys) |= jet;
    if (u16(u16(ds_u16(DS_object_x) - ds_u16(DS_route_x)) + 0x10) & 0xFFE0) return;
    if (u16(u16(ds_u16(DS_object_y) - ds_u16(DS_route_y)) + 0x10) & 0xFFE0) return;
    // CX = 10h before: CH = 0
    const RoutePoint p = route_point(ds_u8(DS_route_index), ds_u16(DS_route_cell));
    route_advance(p.dx, set_cl(p.cx, ds_u8(DS_route_index)), ds_u16(DS_route_cell));
}

} // namespace gb
