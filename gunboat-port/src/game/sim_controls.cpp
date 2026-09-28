// Controls (simulation.md §3.4, §4.1, §4.3, §6.1): the held controls, aiming the guns, the
// throttles and the jet, the heading steps, the fire keys of the gun stations and the speed response.
#include "game/sim.hpp"

#include "host.hpp"
#include "hud/hud.hpp"
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
// Returns AX as the routines leave it: AL = the stepped fraction, AH = the relative heading + bias.
u16 aim_turn(u16 heading, u16 fraction, u16 bx, bool plus, u8 bias)
{
    ds_u8(DS_view_sky_top_prev) = 0;
    ds_u8(DS_view_sky_top) = 0;
    const u16 ax = u16(ds_u8(heading) << 8 | ds_u8(fraction));
    const u16 r = plus ? heading_step_plus(ax, bx) : heading_step_minus(ax, bx);
    const u8 dl = u8(r >> 8);
    const u8 rel = u8(dl - ds_u8(DS_heading) + bias);
    const u16 out = u16(rel << 8 | u8(r));
    if (rel <= 0x41) return out;
    ds_u8(fraction) = u8(r);
    ds_u8(heading) = dl;
    return out;
}

// The end of aim_stern / aim_midship / aim_bow: left, then AL = CL & 8, then right. Returns AX.
u16 aim_sides(u8 cl, u16 bx, u16 ax, u16 (*left)(u16), u16 (*right)(u16))
{
    if (cl & 4) ax = left(bx);
    ax = u16((ax & 0xFF00) | (cl & 8));
    if (cl & 8) ax = right(bx);
    return ax;
}

// Headings: hull, bow, midship, stern (heading_fraction the same order).
constexpr u16 HULL = 0, BOW = 1, MIDSHIP = 2, STERN = 3;
constexpr u16 heading(u16 gun) { return u16(DS_heading + gun); }
constexpr u16 fraction(u16 gun) { return u16(DS_heading_fraction + gun); }

} // namespace

// 0919:0906 end_mission (simulation.md §9.1): station 9, the mission loop's exit.
void end_mission() { ds_u16(DS_station) = 9; }

// 0919:0a22 aim_stern (simulation.md §3.4): CL = held keys (1 up, 2 down, 4 left, 8 right), BX =
// control-rate index into turn_step_table. Returns AX as the original leaves it (AH of AX in when
// the gun does not turn; the crew gunners pass it on, see gunner_aim).
u16 aim_stern(u8 cl, u16 bx, u16 ax)
{
    aim_elevation(DS_elevation_stern, cl, bx);
    return aim_sides(cl, bx, ax, aim_stern_left, aim_stern_right);
}

// 0919:0a5b aim_stern_left (simulation.md §3.4): returns AX.
u16 aim_stern_left(u16 bx) { return aim_turn(heading(STERN), fraction(STERN), bx, false, 0x20); }

// 0919:0a83 aim_stern_right (simulation.md §3.4): returns AX.
u16 aim_stern_right(u16 bx) { return aim_turn(heading(STERN), fraction(STERN), bx, true, 0x20); }

// 0919:0aab aim_midship (simulation.md §3.4): returns AX.
u16 aim_midship(u8 cl, u16 bx, u16 ax)
{
    aim_elevation(DS_elevation_midship, cl, bx);
    return aim_sides(cl, bx, ax, aim_midship_left, aim_midship_right);
}

// 0919:0ae4 aim_midship_left (simulation.md §3.4): returns AX.
u16 aim_midship_left(u16 bx) { return aim_turn(heading(MIDSHIP), fraction(MIDSHIP), bx, false, 0x20); }

// 0919:0b0c aim_midship_right (simulation.md §3.4): returns AX.
u16 aim_midship_right(u16 bx) { return aim_turn(heading(MIDSHIP), fraction(MIDSHIP), bx, true, 0x20); }

// 0919:0b34 aim_bow (simulation.md §3.4): returns AX.
u16 aim_bow(u8 cl, u16 bx, u16 ax)
{
    aim_elevation(DS_elevation_bow, cl, bx);
    return aim_sides(cl, bx, ax, aim_bow_left, aim_bow_right);
}

// 0919:0b6d aim_bow_left (simulation.md §3.4): the bow gun's blocked arc is behind (sub ah,60h).
u16 aim_bow_left(u16 bx) { return aim_turn(heading(BOW), fraction(BOW), bx, false, u8(-0x60)); }

// 0919:0b95 aim_bow_right (simulation.md §3.4): returns AX.
u16 aim_bow_right(u16 bx) { return aim_turn(heading(BOW), fraction(BOW), bx, true, u8(-0x60)); }

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
// starts; otherwise weapon 1 with sound 2. Sets the flash counter, then launches. Returns AX: AX in
// when nothing is fired, else projectile_launch's.
u16 fire_station4(u16 ax)
{
    if ((ds_u8(DS_stern_mount) & 1) || (ds_u8(DS_stern_mount + 1) & 1)) return ax;
    u16 bx;
    u8 al;
    if (ds_u8(DS_stern_weapon) == 0) {
        if (ds_u8(DS_reload_stern) != 8) return ax;
        ds_u8(DS_reload_stern)--;
        // PORT: tells the host that the grenade launcher fires (its own effect on the AdLib,
        // sfx_adlib.cpp); nothing in mem[] changes.
        host_shell_fired(2);
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
    return projectile_launch(bx, u16(ds_u8(heading(STERN)) << 8 | ds_u8(fraction(STERN))));
}

// 0919:0d45 fire_station3 (simulation.md §6.1): the midship mount. Weapon 0: weapon 3 (sound 8)
// when the reload counter is ready (30h); 1: weapon 4 (sound 1) when time compression is on or on
// odd world passes; 2 and above: weapon 1 (sound 2). Returns AX (as fire_station4).
u16 fire_station3(u16 ax)
{
    if ((ds_u8(DS_midship_mount) & 1) || (ds_u8(DS_midship_mount + 1) & 1)) return ax;
    const u8 w = ds_u8(DS_midship_weapon);
    u16 bx;
    if (w > 1) {
        sfx_play(2);
        bx = 1;
        ds_u8(DS_flash_midship) = 1;
    } else if (w == 0) {
        if (ds_u8(DS_reload_midship) != 0x30) return ax;
        ds_u8(DS_reload_midship)--;
        host_shell_fired(3);  // PORT: the mortar fires (as the grenade launcher in fire_station4)
        sfx_play(8);
        bx = 3;
        ds_u8(DS_flash_midship) = 2;
    } else {
        if (ds_u8(DS_time_compression) == 0 && (ds_u8(DS_world_pass_counter) & 1) == 0) return ax;
        sfx_play(1);
        bx = 4;
        ds_u8(DS_flash_midship) = 2;
    }
    ds_u8(DS_shot_elevation) = ds_u8(DS_elevation_midship);
    return projectile_launch(bx, u16(ds_u8(heading(MIDSHIP)) << 8 | ds_u8(fraction(MIDSHIP))));
}

// 0919:0dbd fire_bow (simulation.md §6.1): the bow mount. Weapon 0 (twin guns): the barrels
// alternate with bow_barrel, weapon 4, sound 1; otherwise weapon 5 with sound 3. Returns AX (as
// fire_station4).
u16 fire_bow(u16 ax)
{
    if ((ds_u8(DS_bow_mount) & 1) || (ds_u8(DS_bow_mount + 1) & 1)) return ax;
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
    return projectile_launch(bx, u16(ds_u8(heading(BOW)) << 8 | ds_u8(fraction(BOW))));
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

// 0919:0953 controls_poll (simulation.md §3.4): the held keys and the joystick bits (1 up, 2 down,
// 4 left, 8 right, 10h fire) at a 3D station (station low byte below 5). In chase view they move the
// camera: up / down the distance by 8 (not below 28h, not wrapping to 0), left / right the heading by
// +4 / -4. With the main switch off the fire bit is dropped, and the pilot's controls are ignored.
// Pilot (0, 1): Enter slows down, then the throttles and the jet; gun stations: fire first, then
// aim, with the control rate (control_rate & 3) as the step index.
void controls_poll()
{
    u8 cl = ds_u8(DS_held_keys) | ds_u8(DS_joystick_bits);
    const u16 station = ds_u16(DS_station);
    const u8 st = u8(station);
    if (st >= 5) return;
    cl &= 0x1F;
    if (cl == 0) return;
    if (ds_u8(DS_chase_view) != 0) {
        if (cl & 3) {
            u8 al = ds_u8(DS_chase_distance);
            if (cl & 1) {
                if (al > 0x28) ds_u8(DS_chase_distance) = u8(al - 8);
            } else {
                al = u8(al + 8);
                if (al != 0) ds_u8(DS_chase_distance) = al;
            }
        }
        if (cl & 0x0C) {
            ds_u8(DS_chase_heading) = u8(ds_u8(DS_chase_heading) + 4);
            if (!(cl & 4)) ds_u8(DS_chase_heading) = u8(ds_u8(DS_chase_heading) - 8);
        }
        return;
    }
    if (ds_u8(DS_panel_switches) & 1) {
        cl &= 0x0F;
        if (cl == 0) return;
    }
    if (st < 2) {
        if (ds_u8(DS_panel_switches) & 1) return;
        if (cl & 0x10) pilot_slow_down();
        pilot_throttle_controls(cl, ds_u8(DS_control_rate) & 3);
    } else {
        // AX: the station word with AL = CL & 10h (MOV AL,CL / AND AL,10h), then what fire leaves
        u16 ax = u16((station & 0xFF00) | (cl & 0x10));
        if (st == 2) {
            if (cl & 0x10) ax = fire_bow(ax);
            aim_bow(cl, ds_u8(DS_control_rate) & 3, ax);
        } else if (st == 3) {
            if (cl & 0x10) ax = fire_station3(ax);
            aim_midship(cl, ds_u8(DS_control_rate) & 3, ax);
        } else {
            if (cl & 0x10) ax = fire_station4(ax);
            aim_stern(cl, ds_u8(DS_control_rate) & 3, ax);
        }
    }
}

// 0919:0bbd pilot_throttle_controls (simulation.md §4.1): four rounds of throttle_step for both
// engines; then with left or right: evade_incoming, and the jet angle (bits 0-6 of jet_angle; bit 7,
// reverse, kept) moves by jet_step_table[BX]: left down to 0 (a result below 0 gives 0, with no phase
// change and no redraw), right up to 7Dh (the limit: the same), each with a detent at 40h (ahead)
// when it is crossed. A move steps the jet indicator phase (left -1 wrapping to 2, right +1 modulo 3)
// and redraws the indicator.
void pilot_throttle_controls(u8 cl, u16 bx)
{
    for (int round = 0; round < 4; round++) {
        throttle_step(0, cl);
        throttle_step(1, cl);
    }
    if (!(cl & 0x0C)) return;
    evade_incoming();
    u8 ch = 1;
    const u8 step = ds_u8(u16(DS_jet_step_table + bx));
    const u8 jet = ds_u8(DS_jet_angle);
    const u8 ah = jet & 0x80;
    const u8 dl = jet & 0x7F;
    u8 al;
    if (cl & 4) {
        ch = 0xFF;
        al = u8(dl - step);
        if (s8(al) < 0) {
            ds_u8(DS_jet_angle) = ah;  // 0 | reverse bit
            return;
        }
        if (dl > 0x40 && al < 0x40) al = 0x40;
    } else {
        if (!(cl & 8)) return;
        al = u8(dl + step);
        if (al >= 0x7D) {
            ds_u8(DS_jet_angle) = u8(0x7D | ah);
            return;
        }
        if (dl < 0x40 && al > 0x40) al = 0x40;
    }
    al |= ah;
    ds_u8(DS_jet_angle) = al;
    ch = u8(ch + ds_u8(DS_jet_indicator_phase));
    if (s8(ch) < 0) ch = 2;
    if (ch >= 3) ch = 0;
    ds_u8(DS_jet_indicator_phase) = ch;
    jet_indicator_draw(u16(ah << 8 | al));
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
