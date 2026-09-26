// The crew gunners and the reload counters (simulation.md §6.1, §6.2).
//
// gunner_aim works on the gun its caller describes in shared scratch variables: gauge_row (DS:B7DE,
// the hud's gauge row elsewhere) holds the address of the gun's heading byte, scratch_b7e3 its
// fraction, scratch_b7e2 the gun (0 bow, 1 stern, 2 midship), vec_product_lo (DS:B7E9) its
// elevation and scratch_b7dc the address of its sweep memory byte (gunner_sweep).
//
// The gunners return AX and SI as the original leaves them: game_frame passes AX on to jet_marker,
// throttle_needles and panel_blink (whose lamps store AH in scratch_b7e3), and SI on to
// projectile_tick (which stores it in caller_si).
#include "game/sim.hpp"

#include "hud/hud.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u16 set_al(u16 ax, u8 al) { return u16((ax & 0xFF00) | al); }

// The part the three gunner routines share (0919:18e8 and its copies): describe the gun, then aim
// (AL = the gun's elevation, loaded last).
AxSi gunner_setup(u16 ax, u16 si, u16 heading, u16 fraction, u8 gun, u16 elevation, u16 sweep)
{
    ds_u16(DS_gauge_row) = heading;
    ds_u8(DS_scratch_b7e3) = ds_u8(fraction);
    ds_u8(DS_scratch_b7e2) = gun;
    ds_u8(DS_vec_product_lo) = ds_u8(elevation);
    ds_u16(DS_scratch_b7dc) = sweep;
    return gunner_aim(set_al(ax, ds_u8(elevation)), si);
}

// Whether a gunner of condition byte c acts this frame: not dead (2); a wounded one (0 or 1) only
// when the RNG byte DS:0088 has the gunner's bit clear. AL = c & 3 is left in AX.
bool gunner_acts(u8 c, u8 rng_bit, u16 &ax)
{
    c &= 3;
    ax = set_al(ax, c);
    if (c == 2) return false;
    if (c == 3) return true;
    return (ds_u8(DS_rng_state) & rng_bit) == 0;
}

// The fire routine of the gun in scratch_b7e2 (0919:1a8e). Returns AX.
u16 gunner_fire(u16 ax)
{
    const u8 gun = ds_u8(DS_scratch_b7e2);
    if (gun < 1) return fire_bow(ax);
    if (gun == 1) return fire_station4(ax);
    return fire_station3(ax);
}

} // namespace

// 0919:18af crew_gunners (simulation.md §6.2): with fire at will on and not in chase view, the bow,
// stern and midship gunners, in that order. Returns AX (fire_at_will when nothing runs, else what the
// gunners leave) and SI.
AxSi crew_gunners(u16 si)
{
    const u16 ax = ds_u16(DS_fire_at_will);
    if (ax == 0 || ds_u8(DS_chase_view) != 0) return {ax, si};
    AxSi r = gunner_bow(ax, si);
    r = gunner_stern(r.ax, r.si);
    return gunner_midship(r.ax, r.si);
}

// 0919:18c7 gunner_bow (simulation.md §6.2): unless the player is at the bow gun (not in the demo);
// the gunner's mate, RNG bit 20h.
AxSi gunner_bow(u16 ax, u16 si)
{
    if (ds_u16(DS_demo_mode) == 0 && ds_u16(DS_station) == 2) return {ax, si};
    if (!gunner_acts(ds_u8(DS_gunners_mate_condition), 0x20, ax)) return {ax, si};
    return gunner_setup(ax, si, u16(DS_heading + 1), u16(DS_heading_fraction + 1), 0, DS_elevation_bow,
                        DS_gunner_sweep);
}

// 0919:190d gunner_stern (simulation.md §6.2): the engineman, RNG bit 40h.
AxSi gunner_stern(u16 ax, u16 si)
{
    if (ds_u16(DS_demo_mode) == 0 && ds_u16(DS_station) == 4) return {ax, si};
    if (!gunner_acts(ds_u8(DS_engineman_condition), 0x40, ax)) return {ax, si};
    return gunner_setup(ax, si, u16(DS_heading + 3), u16(DS_heading_fraction + 3), 1, DS_elevation_stern,
                        u16(DS_gunner_sweep + 1));
}

// 0919:1953 gunner_midship (simulation.md §6.2): only with a midship weapon other than 0; the
// seaman, RNG bit 80h.
AxSi gunner_midship(u16 ax, u16 si)
{
    if (ds_u16(DS_demo_mode) == 0 && ds_u16(DS_station) == 3) return {ax, si};
    ax = set_al(ax, ds_u8(DS_midship_weapon));
    if (u8(ax) == 0) return {ax, si};
    if (!gunner_acts(ds_u8(DS_seaman_condition), 0x80, ax)) return {ax, si};
    return gunner_setup(ax, si, u16(DS_heading + 2), u16(DS_heading_fraction + 2), 2, DS_elevation_midship,
                        u16(DS_gunner_sweep + 2));
}

// 0919:19a0 gunner_aim (simulation.md §6.2): the visible list from its last entry down. The scan
// ends (the gun sweeps) at the first entry whose distance class is above 12h. A target: kind 01h..17h
// except 10h and 11h, relative bearing a = bearing + view heading - gun heading - 38h within 0..28h,
// and a clear line of sight. Then a -= 14h: off by more than 4 the gun turns at rate 0 (left for a
// negative a); else the fine bearing (a * 8 + fine bearing + view fraction - gun fraction) & F8h, if
// nonzero, turns it at rate 2; aligned, the elevation error sat(elevation + 0Eh) + gun elevation (its
// carry = too high: down) & FCh after folding a negative byte: 0 fires, else up / down at rate 1 (2
// when below 8), and down with the gun at 68h or lower fires. No target: the gun sweeps left or right
// (its sweep bit) at rate 1, and the bit flips when the heading did not change (the arc stopped it).
// AX and SI as the original leaves them: AH is AH in throughout, AL the last value loaded; SI the
// last object offset or 2*entry the scan loaded.
AxSi gunner_aim(u16 ax, u16 si)
{
    const u8 ah = u8(ax >> 8);
    u8 al = u8(ax);
    const auto with_al = [ah](u8 v) { return u16(ah << 8 | v); };
    const u8 dl = ds_u8(ds_u16(DS_gauge_row));
    u16 bx = u16(ds_u16(DS_visible_count) - 1);
    for (;;) {
        bx = u16(bx << 1);
        al = ds_u8(u16(DS_visible_distance + bx));
        if (al > 0x12) break;
        si = ds_u16(u16(DS_visible_object + bx));
        bx = u16(bx >> 1);
        al = ds_u8(u16(DS_object_word + si));
        if (al < 0x18 && al != 0 && al != 0x10 && al != 0x11) {
            al = u8(ds_u8(u16(DS_visible_bearing + bx)) + ds_u8(DS_view_heading) - dl - 0x38);
            if (al <= 0x28) {
                si = u16(bx << 1);
                if ((line_of_sight(si, 0) >> 8) == 0) {
                    // 1a16: the target
                    al = u8(al - 0x14);
                    u8 cl;
                    if (al == 0 || al <= 4 || al >= 0xFC) {
                        al = u8(u8(al << 3) + ds_u8(u16(DS_visible_fine_bearing + bx)) +
                                ds_u8(DS_view_heading_fraction) - ds_u8(DS_scratch_b7e3));
                        al &= 0xF8;
                        if (al == 0) {
                            // 1a4a: the elevation
                            u8 e = ds_u8(u16(DS_visible_elevation + bx));
                            e = e + 0x0E > 0xFF ? 0xFF : u8(e + 0x0E);
                            const bool carry = e + ds_u8(DS_vec_product_lo) > 0xFF;
                            e = u8(e + ds_u8(DS_vec_product_lo));
                            if (s8(e) < 0) e ^= 0xFF;
                            e &= 0xFC;
                            if (e == 0) return {gunner_fire(with_al(0)), si};
                            cl = e < 8 ? 2 : 1;
                            const u8 keys = u8((carry ? 1 : 0) + 1);  // 1 up, 2 down
                            if (keys == 2 && ds_u8(DS_vec_product_lo) <= 0x68) return {gunner_fire(with_al(e)), si};
                            return {gunner_key(keys, cl, with_al(keys)), si};
                        }
                        cl = 2;
                    } else {
                        cl = 0;
                    }
                    const u8 keys = al >= 0x80 ? 4 : 8;
                    return {gunner_key(keys, cl, with_al(keys)), si};
                }
            }
        }
        bx--;
        if (s16(bx) < 0) break;
    }
    // 19ef: no target, sweep
    const u8 bl = u8(((ds_u8(ds_u16(DS_scratch_b7dc)) & 1) << 2) + 4);
    ax = gunner_key(bl, 1, with_al(al));
    const bool unmoved = dl == ds_u8(ds_u16(DS_gauge_row));
    if (unmoved) ds_u8(ds_u16(DS_scratch_b7dc)) ^= 1;
    return {ax, si};
}

// 0919:1aa3 gunner_key (simulation.md §6.2): BL = keys, CL = rate index, to the aiming routine of
// the gun in scratch_b7e2 (0 bow, 1 stern, else midship). Returns AX.
u16 gunner_key(u8 bl, u8 cl, u16 ax)
{
    const u8 gun = ds_u8(DS_scratch_b7e2);
    if (gun < 1) return aim_bow(bl, cl, ax);
    if (gun == 1) return aim_stern(bl, cl, ax);
    return aim_midship(bl, cl, ax);
}

// 0919:1cf9 reload_tick (simulation.md §6.1): a reload counter below its ready value (midship 30h,
// stern 8) counts down; at 0 it is ready again. Each step redraws the mount's lamp (19h midship,
// 1Ch stern): 2 while reloading, 3 when ready.
void reload_tick()
{
    if (ds_u8(DS_reload_midship) != 0x30) {
        u8 ah = 2;
        if (--ds_u8(DS_reload_midship) == 0) {
            ds_u8(DS_reload_midship) = 0x30;
            ah = 3;
        }
        indicator_draw(u16(ah << 8 | 0x19));
    }
    if (ds_u8(DS_reload_stern) != 8) {
        u8 ah = 2;
        if (--ds_u8(DS_reload_stern) == 0) {
            ds_u8(DS_reload_stern) = 8;
            ah = 3;
        }
        indicator_draw(u16(ah << 8 | 0x1C));
    }
}

} // namespace gb
