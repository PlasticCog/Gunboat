// Camera pitch and the wave bob (simulation.md §5.4).
#include "game/sim.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

// 0919:80e0 camera_pitch_bob (simulation.md §5.4): the pitch impulse decays by 4 toward 0 and is
// clamped to -18h..18h; the pitch reference is (-(2*impulse + 80h)) & F8h (low byte). The bob
// position moves by the bob velocity / 2 (in 1/256): a step that would wrap (overflow up, or
// underflow or land on 0 going down) is not stored, and the bob period is shortened to end the
// swing now. The view pitch is the bob's high byte, at a gun station plus the gun's elevation minus
// the pitch reference (8 if that is negative), clamped to 8..1F0h, / 8 + 43h. When the countdown
// ends, the phase advances: odd phases reverse the velocity, even ones draw a new swing from the RNG
// byte DS:008A masked by bob_masks[sea state]: period = 1Dh + (mask low) - 3 * speed band (at least
// 1), velocity = rol(mask high, 2) + 1 + speed band, negated in phase 2.
void camera_pitch_bob()
{
    u16 ax = ds_u16(DS_pitch_impulse);
    if (ax != 0) {
        if (s16(ax) >= 0) {
            ax = u16(ax - 4);
            if (s16(ax) < 0) ax = 0;
        } else {
            ax = u16(ax + 4);
            if (s16(ax) >= 0) ax = 0;
        }
    }
    if (!(s16(ax) < 0x18)) ax = 0x18;
    if (!(s16(ax) > -0x18)) ax = u16(-0x18);
    ds_u16(DS_pitch_impulse) = ax;
    ds_u8(DS_pitch_reference) = u8(u8(-u8(u8(ax << 1) + 0x80)) & 0xF8);

    // bob position += velocity * 128 (sar of the velocity in the high byte)
    const u16 step = u16(s16(u16(ds_u8(DS_bob_velocity) << 8)) >> 1);
    const u16 pos = ds_u16(DS_bob_position);
    const u32 sum = u32(step) + pos;
    bool store;
    if (s16(step) >= 0) store = sum <= 0xFFFF;                  // add; jae store
    else store = u16(sum) != 0 && sum > 0xFFFF;                 // je: no; jb: store
    if (store) {
        ds_u16(DS_bob_position) = u16(sum);
    } else {
        ds_u8(DS_bob_period) = u8(ds_u8(DS_bob_period) - ds_u8(DS_bob_countdown));
        ds_u8(DS_bob_countdown) = 1;
    }

    const u16 cx = ds_u8(DS_bob_position + 1);
    u16 pitch = cx;
    const u16 station = ds_u16(DS_station);
    if (u8(station) >= 2) {
        u8 e = ds_u8(DS_elevation_bow);
        if (u8(station) >= 3) e = u8(station) == 3 ? ds_u8(DS_elevation_midship) : ds_u8(DS_elevation_stern);
        // sub ax,bx with BX = station's high byte : pitch reference
        pitch = u16(e - u16((station & 0xFF00) | ds_u8(DS_pitch_reference)));
        pitch = u16(pitch + cx);
        if (s16(pitch) < 0) pitch = 8;
    }
    if (pitch < 8) pitch = 8;
    if (pitch > 0x1F0) pitch = 0x1F0;
    ds_u8(DS_view_pitch) = u8((pitch >> 3) + 0x43);

    if (--ds_u8(DS_bob_countdown) != 0) return;
    ds_u8(DS_bob_countdown) = ds_u8(DS_bob_period);
    const u8 phase = u8((ds_u8(DS_bob_phase) + 1) & 3);
    ds_u8(DS_bob_phase) = phase;
    if (phase & 1) {
        ds_u8(DS_bob_velocity) = u8(-ds_u8(DS_bob_velocity));
        return;
    }
    const u8 r = ds_u8(DS_rng_state + 2);
    const u16 mask = ds_u16(u16(DS_bob_masks + u16(ds_u16(DS_sea_state) << 1)));
    const u16 m = u16((r << 8 | r) & mask);
    u8 al = u8(u8(m) + 0x1D);
    const u8 band = ds_u8(DS_speed_band);
    al = u8(al - band);
    al = u8(al - band);
    al = u8(al - band);
    if (al == 0) al = 1;
    ds_u8(DS_bob_period) = al;
    ds_u8(DS_bob_countdown) = al;
    u8 ah = u8(m >> 8);
    ah = u8(ah << 2 | ah >> 6);  // rol ah,1 twice
    ah = u8(ah + 1 + band);
    if (ds_u8(DS_bob_phase) & 2) ah = u8(-ah);
    ds_u8(DS_bob_velocity) = ah;
}

} // namespace gb
