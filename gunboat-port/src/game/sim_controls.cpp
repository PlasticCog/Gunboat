// Controls (simulation.md §3.4, §4.1, §4.3, §6.1): aiming the guns, the throttles, the heading
// steps, the fire keys of the gun stations and the speed response.
#include "game/sim.hpp"

#include "mem.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// turn_step_table[BX]: [bx - 29D7h] with a 16-bit index, so any BX reads DGROUP.
u8 turn_step(u16 bx) { return ds_u8(u16(DS_turn_step_table + bx)); }

// The elevation part of aim_stern / aim_midship / aim_bow (the original repeats it inline): up adds
// the step unless it carries past FFh; down subtracts it and keeps the result only if it is >= 60h
// (the subtraction's borrow is not tested).
void aim_elevation(u16 elevation, u8 cl, u16 bx)
{
    if (cl & 1) {
        const u8 e = ds_u8(elevation), s = turn_step(bx);
        if (e + s <= 0xFF) ds_u8(elevation) = u8(e + s);
    }
    if (cl & 2) {
        const u8 e = u8(ds_u8(elevation) - turn_step(bx));
        if (e >= 0x60) ds_u8(elevation) = e;
    }
}

// The body of the six aim_*_left / aim_*_right routines: clear the sky redraw rows, step the gun's
// heading, and keep the new heading unless it falls in the blocked arc relative to the hull:
// (rel + bias) <= 41h, bias 20h for the midship and stern guns (the arc ahead), -60h for the bow
// gun (the arc behind).
void aim_turn(u16 heading, u16 fraction, u16 bx, bool plus, u8 bias)
{
    ds_u8(DS_view_sky_top_prev) = 0;
    ds_u8(DS_view_sky_top) = 0;
    const u16 ax = u16(ds_u8(heading) << 8 | ds_u8(fraction));
    const u16 r = plus ? heading_step_plus(ax, bx) : heading_step_minus(ax, bx);
    const u8 dl = u8(r >> 8);
    if (u8(dl - ds_u8(DS_heading) + bias) <= 0x41) return;
    ds_u8(fraction) = u8(r);
    ds_u8(heading) = dl;
}

// Headings: hull, bow, midship, stern (heading_fraction the same order).
constexpr u16 HULL = 0, BOW = 1, MIDSHIP = 2, STERN = 3;
constexpr u16 heading(u16 gun) { return u16(DS_heading + gun); }
constexpr u16 fraction(u16 gun) { return u16(DS_heading_fraction + gun); }

} // namespace

// 0919:0906 end_mission (simulation.md §9.1): station 9, the mission loop's exit.
void end_mission() { ds_u16(DS_station) = 9; }

// 0919:0a22 aim_stern (simulation.md §3.4): CL = held keys (1 up, 2 down, 4 left, 8 right), BX =
// control-rate index into turn_step_table.
void aim_stern(u8 cl, u16 bx)
{
    aim_elevation(DS_elevation_stern, cl, bx);
    if (cl & 4) aim_stern_left(bx);
    if (cl & 8) aim_stern_right(bx);
}

// 0919:0a5b aim_stern_left (simulation.md §3.4)
void aim_stern_left(u16 bx) { aim_turn(heading(STERN), fraction(STERN), bx, false, 0x20); }

// 0919:0a83 aim_stern_right (simulation.md §3.4)
void aim_stern_right(u16 bx) { aim_turn(heading(STERN), fraction(STERN), bx, true, 0x20); }

// 0919:0aab aim_midship (simulation.md §3.4)
void aim_midship(u8 cl, u16 bx)
{
    aim_elevation(DS_elevation_midship, cl, bx);
    if (cl & 4) aim_midship_left(bx);
    if (cl & 8) aim_midship_right(bx);
}

// 0919:0ae4 aim_midship_left (simulation.md §3.4)
void aim_midship_left(u16 bx) { aim_turn(heading(MIDSHIP), fraction(MIDSHIP), bx, false, 0x20); }

// 0919:0b0c aim_midship_right (simulation.md §3.4)
void aim_midship_right(u16 bx) { aim_turn(heading(MIDSHIP), fraction(MIDSHIP), bx, true, 0x20); }

// 0919:0b34 aim_bow (simulation.md §3.4)
void aim_bow(u8 cl, u16 bx)
{
    aim_elevation(DS_elevation_bow, cl, bx);
    if (cl & 4) aim_bow_left(bx);
    if (cl & 8) aim_bow_right(bx);
}

// 0919:0b6d aim_bow_left (simulation.md §3.4): the bow gun's blocked arc is behind (sub ah,60h).
void aim_bow_left(u16 bx) { aim_turn(heading(BOW), fraction(BOW), bx, false, u8(-0x60)); }

// 0919:0b95 aim_bow_right (simulation.md §3.4)
void aim_bow_right(u16 bx) { aim_turn(heading(BOW), fraction(BOW), bx, true, u8(-0x60)); }

// 0919:0c6a throttle_step (simulation.md §4.1): one step of engine SI's throttle for the held keys
// CL, only while that engine runs. DI = SI xor 1 is the other engine. In forward (jet_angle bit 7
// clear) up raises the throttle toward its maximum and down lowers it toward the other throttle;
// at idle (8) down flips into reverse and raises it again. In reverse the roles swap.
void throttle_step(u16 si, u8 cl)
{
    const u16 di = si ^ 1;
    u8 &mine = ds_u8(u16(DS_throttle + si));
    const u8 other = ds_u8(u16(DS_throttle + di));
    const u8 max = ds_u8(u16(DS_throttle_max + si));
    u8 &jet = ds_u8(DS_jet_angle);
    if (ds_u8(u16(DS_engine_state + si)) != 1) return;
    if (cl & 2) {
        if (jet & 0x80) {
            if (mine < max) mine++;
        } else if (mine == 8) {
            jet |= 0x80;
            mine++;
        } else if (mine >= other) {
            mine--;
        }
    } else if (cl & 1) {
        if (jet & 0x80) {
            if (mine == 8) {
                jet &= 0x7F;
                mine++;
            } else if (mine >= other) {
                mine--;
            }
        } else if (mine < max) {
            mine++;
        }
    }
}

// 0919:0cf4 fire_station4 (simulation.md §6.1): the stern mount, unless either mount byte has bit 0
// (off). Stern weapon 0: weapon 2 with sound 8, only when the reload counter is ready (8), which it
// starts; otherwise weapon 1 with sound 2. Sets the flash counter, then launches.
void fire_station4()
{
    if ((ds_u8(DS_stern_mount) & 1) || (ds_u8(DS_stern_mount + 1) & 1)) return;
    u16 bx;
    u8 al;
    if (ds_u8(DS_stern_weapon) == 0) {
        if (ds_u8(DS_reload_stern) != 8) return;
        ds_u8(DS_reload_stern)--;
        sfx_play(8);
        bx = 2;
        al = 2;
    } else {
        sfx_play(2);
        bx = 1;
        al = 1;
    }
    ds_u8(DS_flash_stern) = al;
    ds_u8(DS_shot_elevation) = ds_u8(DS_elevation_stern);
    projectile_launch(bx, u16(ds_u8(heading(STERN)) << 8 | ds_u8(fraction(STERN))));
}

// 0919:0d45 fire_station3 (simulation.md §6.1): the midship mount. Weapon 0: weapon 3 (sound 8)
// when the reload counter is ready (30h); 1: weapon 4 (sound 1) when time compression is on or on
// odd world passes; 2 and above: weapon 1 (sound 2).
void fire_station3()
{
    if ((ds_u8(DS_midship_mount) & 1) || (ds_u8(DS_midship_mount + 1) & 1)) return;
    const u8 w = ds_u8(DS_midship_weapon);
    u16 bx;
    if (w > 1) {
        sfx_play(2);
        bx = 1;
        ds_u8(DS_flash_midship) = 1;
    } else if (w == 0) {
        if (ds_u8(DS_reload_midship) != 0x30) return;
        ds_u8(DS_reload_midship)--;
        sfx_play(8);
        bx = 3;
        ds_u8(DS_flash_midship) = 2;
    } else {
        if (ds_u8(DS_time_compression) == 0 && (ds_u8(DS_world_pass_counter) & 1) == 0) return;
        sfx_play(1);
        bx = 4;
        ds_u8(DS_flash_midship) = 2;
    }
    ds_u8(DS_shot_elevation) = ds_u8(DS_elevation_midship);
    projectile_launch(bx, u16(ds_u8(heading(MIDSHIP)) << 8 | ds_u8(fraction(MIDSHIP))));
}

// 0919:0dbd fire_bow (simulation.md §6.1): the bow mount. Weapon 0 (twin guns): the barrels
// alternate with bow_barrel, weapon 4, sound 1; otherwise weapon 5 with sound 3.
void fire_bow()
{
    if ((ds_u8(DS_bow_mount) & 1) || (ds_u8(DS_bow_mount + 1) & 1)) return;
    u16 bx;
    if (ds_u8(DS_bow_weapon) != 0) {
        ds_u8(DS_flash_bow) = 2;
        sfx_play(3);
        bx = 5;
    } else {
        ds_u8(DS_bow_barrel)++;
        if ((ds_u8(DS_bow_barrel) & 1) == 0) ds_u8(DS_flash_bow) = 2;
        else ds_u8(DS_flash_bow + 1) = 2;
        sfx_play(1);
        bx = 4;
    }
    ds_u8(DS_shot_elevation) = ds_u8(DS_elevation_bow);
    projectile_launch(bx, u16(ds_u8(heading(BOW)) << 8 | ds_u8(fraction(BOW))));
}

// 0919:0e19 pilot_slow_down (simulation.md §4.1): four times, each throttle above idle that is not
// below the other is lowered by one (the starboard test sees the port throttle already lowered).
void pilot_slow_down()
{
    u8 &port = ds_u8(DS_throttle), &stbd = ds_u8(DS_throttle + 1);
    for (u8 bl = 4; bl != 0; bl--) {
        if (port > 8 && port >= stbd) port--;
        if (stbd > 8 && stbd >= port) stbd--;
    }
}

// 0919:0e48 rotate_headings_minus (simulation.md §4.3): all four headings (hull, bow, stern,
// midship, in that order) step down by turn_step_table[BX]; the sky rows are redrawn.
void rotate_headings_minus(u16 bx)
{
    ds_u8(DS_view_sky_top_prev) = 0;
    ds_u8(DS_view_sky_top) = 0;
    for (u16 gun : {HULL, BOW, STERN, MIDSHIP}) {
        const u16 r = heading_step_minus(u16(ds_u8(heading(gun)) << 8 | ds_u8(fraction(gun))), bx);
        ds_u8(fraction(gun)) = u8(r);
        ds_u8(heading(gun)) = u8(r >> 8);
    }
}

// 0919:0e95 rotate_headings_plus (simulation.md §4.3)
void rotate_headings_plus(u16 bx)
{
    ds_u8(DS_view_sky_top_prev) = 0;
    ds_u8(DS_view_sky_top) = 0;
    for (u16 gun : {HULL, BOW, STERN, MIDSHIP}) {
        const u16 r = heading_step_plus(u16(ds_u8(heading(gun)) << 8 | ds_u8(fraction(gun))), bx);
        ds_u8(fraction(gun)) = u8(r);
        ds_u8(heading(gun)) = u8(r >> 8);
    }
}

// 0919:0f0b heading_step_plus (simulation.md §4.3): AL (fraction, eighths) += turn_step_table[BX]
// (8-bit, the carry is lost); the unrolled chain then adds one whole unit to AH for each of the
// thresholds 8, 10h, ... 40h that AL reaches (at most 8), and AL keeps its low 3 bits.
u16 heading_step_plus(u16 ax, u16 bx)
{
    u8 al = u8(u8(ax) + turn_step(bx));
    u8 ah = u8(ax >> 8);
    for (u8 limit = 8; limit <= 0x40 && al >= limit; limit = u8(limit + 8)) ah++;
    return u16(ah << 8 | (al & 7));
}

// 0919:0f42 heading_step_minus (simulation.md §4.3): AL -= turn_step_table[BX]; on a borrow AH drops
// by one, then up to seven times AL += 8 and, while the result is still negative (bit 7), AH drops
// by one more (at most 8 in all). AL keeps its low 3 bits.
u16 heading_step_minus(u16 ax, u16 bx)
{
    const u8 step = turn_step(bx);
    u8 al = u8(ax);
    u8 ah = u8(ax >> 8);
    const bool borrow = al < step;
    al = u8(al - step);
    if (borrow) {
        ah--;
        for (int i = 0; i < 7; i++) {
            al = u8(al + 8);
            if (!(al & 0x80)) break;
            ah--;
        }
    }
    return u16(ah << 8 | (al & 7));
}

// 0919:29f9 accelerate_speed (simulation.md §4.3): the speed moves one unit toward the target CH
// (signed), and the pitch impulse by -5 (slowing) or +5, unless the boat is locked (passengers).
void accelerate_speed(u8 ch)
{
    const u8 al = ds_u8(DS_speed);
    if (ch == al) return;
    bool up;
    if (s8(ch) >= 0) up = s8(al) < 0 || al < ch;
    else up = s8(al) < 0 && al <= ch;
    if (ds_u8(DS_boat_locked) != 0) return;
    if (up) {
        ds_u8(DS_speed)++;
        ds_u16(DS_pitch_impulse) = u16(ds_u16(DS_pitch_impulse) + 5);
    } else {
        ds_u8(DS_speed)--;
        ds_u16(DS_pitch_impulse) = u16(ds_u16(DS_pitch_impulse) - 5);
    }
}

// 0919:3479 evade_incoming (simulation.md §4.1, §8.5): every jet move lowers the accuracy (low five
// bits) of the incoming slots 7..0 that still have some.
void evade_incoming()
{
    for (s16 bx = 7; bx >= 0; bx--) {
        u8 &d = ds_u8(u16(DS_incoming_descriptor + bx));
        if (d & 0x1F) d--;
    }
}

} // namespace gb
