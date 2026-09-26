// Boat motion (simulation.md §5).
#include "game/sim.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// |speed| as the original forms it: xor FFh, add 1 (so -80h stays 80h).
u8 abs_speed()
{
    const u8 a = ds_u8(DS_speed);
    return s8(a) < 0 ? u8(-a) : a;
}

// One axis towards smaller values (0919:7f12 for Y, 0919:7f8e for X): the fraction drops by the
// step fraction B7E3, the borrow joins the whole step B7E2, and the position word drops by B7E2
// unless that would take its high byte below 5, in which case the position is not stored at all.
void axis_back(u16 fraction, u16 position)
{
    u8 &whole = ds_u8(DS_scratch_b7e2);
    const u8 step = ds_u8(DS_scratch_b7e3);
    u8 &f = ds_u8(fraction);
    if (f < step) whole++;
    f = u8(f - step);
    const u16 w = ds_u16(position);
    u8 lo = u8(w), hi = u8(w >> 8);
    if (lo < whole) {
        if (hi < 5) return;
        hi--;
    }
    lo = u8(lo - whole);
    ds_u16(position) = u16(hi << 8 | lo);
}

// One axis towards larger values (0919:7f35 for Y, 0919:7fb1 for X): as axis_back with carries; the
// high byte stops below limit (27h for Y, 3Fh for X).
void axis_forward(u16 fraction, u16 position, u8 limit)
{
    u8 &whole = ds_u8(DS_scratch_b7e2);
    const u8 step = ds_u8(DS_scratch_b7e3);
    u8 &f = ds_u8(fraction);
    if (f + step > 0xFF) whole++;
    f = u8(f + step);
    const u16 w = ds_u16(position);
    u8 lo = u8(w), hi = u8(w >> 8);
    if (lo + whole > 0xFF) {
        if (hi >= limit) return;
        hi++;
    }
    lo = u8(lo + whole);
    ds_u16(position) = u16(hi << 8 | lo);
}

// The boat's step on one axis from vec_product_hi (0919:7ef3, 0919:7f71): shl drops bit 7, two
// shl/rcl pairs move bits 6 and 5 into B7E2, and B7E3 keeps the rest shifted left by 3.
void split_boat_step()
{
    const u8 hi = ds_u8(DS_vec_product_hi);
    ds_u8(DS_scratch_b7e2) = u8(hi >> 5 & 3);
    ds_u8(DS_scratch_b7e3) = u8(hi << 3);
}

} // namespace

// 0919:3706 vec_scale (simulation.md §5.1): AX = AL * vec_factor (MUL), also kept in
// vec_product_lo/hi.
u16 vec_scale(u8 al)
{
    const u16 ax = u16(al * ds_u8(DS_vec_factor));
    ds_u8(DS_vec_product_lo) = u8(ax);
    ds_u8(DS_vec_product_hi) = u8(ax >> 8);
    return ax;
}

// 0919:7e5f heading_vector (simulation.md §5.1): the sine components of an angle (256 per turn)
// from the high bytes of sine_table, then vec_factor scaled by |speed|; in reverse the angle is
// turned round. Returns vec_scale's AX (BX, a table address, is used by no caller).
u16 heading_vector(u8 angle)
{
    ds_u8(DS_vec_angle) = angle;
    u8 i = angle & 0x3F;
    if (angle & 0x40) i = u8((i ^ 0xFF) + 0x41);
    ds_u8(DS_vec_sin) = u8(seg_u16(CSSEG_sine_table, u16(CS_sine_table + 4 * i)) >> 8);
    const u8 j = u8((i ^ 0xFF) + 0x41);
    ds_u8(DS_vec_factor) = u8(seg_u16(CSSEG_sine_table, u16(CS_sine_table + 4 * j)) >> 8);
    if (s8(ds_u8(DS_speed)) < 0) ds_u8(DS_vec_angle) ^= 0x80;
    return vec_scale(abs_speed());
}

// 0919:7ee8 boat_move (simulation.md §5.1): moves the boat (object 0) along the hull heading by the
// speed with 1/256 fractions, clipped to the map, then shifts the water marks (render3d.md §3.2) by
// the motion relative to the view.
void boat_move()
{
    // Y: vec_factor is the cosine of the hull heading.
    heading_vector(ds_u8(DS_heading));
    split_boat_step();
    u8 quadrant = ds_u8(DS_vec_angle) & 0xC0;
    if (quadrant == 0x00 || quadrant == 0xC0) axis_forward(DS_boat_y_fraction, DS_object_y, 0x27);
    else axis_back(DS_boat_y_fraction, DS_object_y);

    // X: the sine.
    ds_u8(DS_vec_factor) = ds_u8(DS_vec_sin);
    vec_scale(abs_speed());
    split_boat_step();
    if (ds_u8(DS_vec_angle) & 0x80) axis_back(DS_boat_x_fraction, DS_object_x);
    else axis_forward(DS_boat_x_fraction, DS_object_x, 0x3F);

    // Water marks: the view's heading relative to the hull. heading[station - 1] is read as
    // DS:B81D + station, so stations above 4 read the bytes after the heading array.
    const u16 station = ds_u16(DS_station);
    heading_vector(u8(ds_u8(u16(DS_heading - 1 + station)) - ds_u8(DS_heading)));
    u8 &whole = ds_u8(DS_scratch_b7e2);
    u8 &fraction = ds_u8(DS_water_phase_fraction);
    const u8 hi = ds_u8(DS_vec_product_hi);
    whole = u8(hi >> 6);  // shl/rcl twice: bits 7 and 6
    quadrant = ds_u8(DS_vec_angle) & 0xC0;
    if (quadrant != 0x00 && quadrant != 0xC0) {
        const u8 step = ds_u8(DS_scratch_b7e3) = u8(hi << 2);
        if (fraction < step) whole++;
        fraction = u8(fraction - step);
        ds_u8(DS_water_phase) = u8(whole + ds_u8(DS_water_phase));
    } else {
        const u8 step = u8(hi << 2);  // this branch does not store it in B7E3
        if (fraction + step > 0xFF) whole++;
        fraction = u8(fraction + step);
        ds_u8(DS_water_phase) = u8(ds_u8(DS_water_phase) - whole);
    }

    ds_u8(DS_vec_factor) = ds_u8(DS_vec_sin);
    vec_scale(abs_speed());
    const u8 n = ds_u8(DS_vec_product_hi) >> 3;
    if (n == 0) return;
    // 16 table rows, two marks each, starting at the mark of the first horizon row. n + 10 * row
    // can pass the 160-byte table and read the code bytes of heading_vector after it, as the
    // original does.
    u16 si = n;
    u16 k = (ds_u8(DS_water_phase) >> 2) & 0x1F;
    for (int row = 0; row < 16; row++, si += 10) {
        const u8 e = seg_u8(CSSEG_water_shift_table, u16(CS_water_shift_table + si));
        for (int twice = 0; twice < 2; twice++, k = (k + 1) & 0x1F) {
            u8 &f = ds_u8(u16(DS_water_mark_x_fraction + k));
            u8 &x = ds_u8(u16(DS_water_mark_x + k));
            const u8 step = e & 0xFC;
            u8 move = e & 3;
            if (ds_u8(DS_vec_angle) & 0x80) {
                if (f + step > 0xFF) move++;
                f = u8(f + step);
                x = u8(x - move);
            } else {
                if (f < step) move++;
                f = u8(f - step);
                x = u8(x + move);
            }
        }
    }
}

} // namespace gb
