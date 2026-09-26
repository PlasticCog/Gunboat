// Engines and propulsion (simulation.md §4.2, §4.3): fuel, leaks, the start and stop countdowns,
// each engine's thrust through the waterjet, the boat's wake, acceleration and turning.
#include "game/sim.hpp"

#include "hud/hud.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u16_m &object_word(u16 off) { return ds_u16(u16(DS_object_word + off)); }
u16_m &object_x(u16 off) { return ds_u16(u16(DS_object_x + off)); }
u16_m &object_y(u16 off) { return ds_u16(u16(DS_object_y + off)); }

// The sine table words at a byte offset from sine_table (CS:3502).
u16 sine_word(u16 off) { return seg_u16(CSSEG_sine_table, u16(CS_sine_table + off)); }

// An engine whose fuel ran out (0919:254f, 2675): fuel 0; if its switch is on it is switched off
// (engine_switch); then the switch byte 15h and the lamp byte 95h (off, locked, blinking).
void fuel_out(u16 si)
{
    ds_u16(u16(DS_fuel + u16(si << 1))) = 0;
    if (!(ds_u8(u16(DS_engine_switches + si)) & 1)) engine_switch(si);
    ds_u8(u16(DS_engine_switches + si)) = 0x15;
    ds_u8(u16(DS_engine_indicator + si)) = 0x95;
}

// Fuel minus n, or out of fuel when n is not below what is left.
void burn(u16 si, u16 n)
{
    u16_m &fuel = ds_u16(u16(DS_fuel + u16(si << 1)));
    if (fuel > n) fuel = u16(fuel - n);
    else fuel_out(si);
}

} // namespace

// 0919:2273 propulsion (simulation.md §4.3, every boat pass). At high detail, every wake_timer passes
// with a throttle above 0Bh (port first) a wake object (kind 3Fh, flags 46h - ((t - 8) >> 1)) at the
// boat, the timer 11h - ((t - 8) >> 3). Then both engines' thrust: the speed accelerates twice toward
// the high byte of the summed forward thrust; the summed turning thrust |t| >> 9 (at most 37h) is the
// step of slot 4 of turn_step_table, and all four headings turn by it (plus for a negative sum; on
// the other side the speed band is updated too). The pilot's station redraws the radar.
void propulsion()
{
    if (ds_u8(DS_detail_low) == 0 && --ds_u8(DS_wake_timer) == 0) {
        u8 al = 0x11, ch = 0x46;
        ds_u8(DS_wake_timer) = al;
        u8 ah = ds_u8(DS_throttle);
        bool wake = true;
        if (!(ah > 0x0B)) {
            ah = ds_u8(DS_throttle + 1);
            if (ah <= 0x0B) wake = false;
        }
        if (wake) {
            ah = u8(ah - 8);
            ah >>= 1;
            ch = u8(ch - ah);
            ah >>= 2;
            al = u8(al - ah);
            ds_u8(DS_wake_timer) = al;
            const u16 bx = free_temp_object();
            object_word(bx) = u16(ch << 8 | 0x3F);
            object_x(bx) = ds_u16(DS_object_x);
            object_y(bx) = ds_u16(DS_object_y);
        }
    }
    const CxDx port = engine_thrust(0);
    const CxDx stbd = engine_thrust(1);
    u16 dx = u16(stbd.dx + port.dx);
    const u16 cx = u16(stbd.cx + port.cx);
    accelerate_speed(u8(cx >> 8));
    accelerate_speed(u8(cx >> 8));
    u8 al = 0;
    if (s16(dx) < 0) {
        al = 1;
        dx = u16(-dx);
    }
    ds_u8(DS_scratch_b7e2) = al;
    dx >>= 1;
    u8 dh = u8(dx >> 8);
    if (dh >= 0x38) dh = 0x37;
    ds_u8(u16(DS_turn_step_table + 4)) = dh;
    if (ds_u8(DS_scratch_b7e2) != 0) {
        rotate_headings_plus(4);
    } else {
        rotate_headings_minus(4);
        u8 speed = ds_u8(DS_speed);
        if (s8(speed) < 0) speed = u8(-speed);
        ds_u8(DS_speed_band) = u8(speed >> 3);
    }
    const u16 station = ds_u16(DS_station);
    if (u8(station) == 1) radar_scope(station);
}

// 0919:2529 engine_thrust (simulation.md §4.2): engine SI. A leaking fuel tank (condition 0..2) loses
// 16 * condition; the countdown in engine_state steps (a start from 7Fh down by 2 to 1, running; a
// stop from 80h up by 2 to 0; the lamp is set to 3 when the start ends) and sets the throttle to
// 8 - ((state & 70h) >> 4); a stopped engine has throttle 0. Both clear the reverse bit and give no
// thrust. A running engine: p = max(throttle - 9, 0); the jet direction j = |jet_angle + the engine's
// offset| (bit 7 of jet_angle included), folded to 80h - j above 40h; CX = hi(p * 80h * sine[j])
// (halved and negated in reverse), DX = hi(p * 80h * sine[40h - j]) (negated when j > 40h). Every
// 32nd RNG byte 008B burns throttle / 4 of fuel. At the pilot's station (station low byte 1) a
// damaged waterjet halves both (a destroyed one quarters them).
CxDx engine_thrust(u16 si)
{
    u8 al = ds_u8(u16(DS_fuel_tank_condition + si)) & 3;
    if (al != 3) burn(si, u16(al << 4));
    u8 &state = ds_u8(u16(DS_engine_state + si));
    u8 &throttle = ds_u8(u16(DS_throttle + si));
    u16 cx = 0, dx = 0;
    al = state;
    bool running = false;
    if (al != 0) {
        bool countdown = true;
        if (s8(al) < 0) {
            if (++state != 0) ++state;
        } else if (al == 1) {
            running = true;
            countdown = false;
        } else {
            state--;
            bool lamp = al == 2;
            if (!lamp) {
                state--;
                lamp = al == 3;
            }
            if (lamp) indicator_draw(u16(0x0300 | u8(si + 1)));
        }
        if (countdown) {
            al = state;
            if (al != 0) {
                throttle = u8(u8(~((al & 0x70) >> 4)) + 9);
                ds_u8(DS_jet_angle) &= 0x7F;
            }
        }
    }
    if (!running && state == 0) {
        throttle = 0;
        ds_u8(DS_jet_angle) &= 0x7F;
    }
    if (running) {
        u8 ah = u8(throttle - 9);
        al = s8(ah) < 0 ? 0 : ah;
        ds_u8(DS_vec_factor) = al;
        u8 bl = u8(ds_u8(DS_jet_angle) + ds_u8(u16(DS_jet_engine_offset + si)));
        if (s8(bl) < 0) bl = u8(-bl);
        ds_u8(DS_scratch_b7e2) = bl;
        if (bl > 0x40) bl = u8(u8(-bl) + 0x80);
        ds_u8(DS_scratch_b7e3) = bl;
        u16 bx = u16(bl << 2);
        u16 p = u16(u16(al << 8) >> 1);
        u16 hi = u16((u32(p) * sine_word(bx)) >> 16);
        if (ds_u8(DS_jet_angle) & 0x80) {
            hi >>= 1;
            hi = u16(-hi);
        }
        cx = hi;
        bx = u16(u16(-bx) + 0x100);
        p = u16(u16(ds_u8(DS_vec_factor) << 8) >> 1);
        dx = u16((u32(p) * sine_word(bx)) >> 16);
        if (ds_u8(DS_scratch_b7e2) > 0x40) dx = u16(-dx);
    }
    // 264e: fuel burn
    if ((ds_u8(DS_rng_state + 3) & 0x1F) == 0) burn(si, u16(throttle >> 2));
    if (ds_u8(DS_station) == 1) {
        al = ds_u8(u16(DS_waterjet_condition + si)) & 3;
        if (al != 3) {
            cx = u16(s16(cx) >> 1);
            dx = u16(s16(dx) >> 1);
            if (al == 2) {
                cx = u16(s16(cx) >> 1);
                dx = u16(s16(dx) >> 1);
            }
        }
    }
    return {cx, dx};
}

} // namespace gb
