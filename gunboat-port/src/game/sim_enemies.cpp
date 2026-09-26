// The world group's objects (simulation.md §5.2, §8.3, §8.4): passengers at a mission stop, the
// homing missile, the enemies' effects, spotting, firing and movement, and their scheduled shots.
#include "game/sim.hpp"

#include "mem.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u16_m &object_word(u16 off) { return ds_u16(u16(DS_object_word + off)); }
u8 &object_kind(u16 off) { return ds_u8(u16(DS_object_word + off)); }
u8 &object_flags(u16 off) { return ds_u8(u16(DS_object_word + 1 + off)); }
u16_m &object_x(u16 off) { return ds_u16(u16(DS_object_x + off)); }
u16_m &object_y(u16 off) { return ds_u16(u16(DS_object_y + off)); }
u16 visible_object(u16 si) { return ds_u16(u16(DS_visible_object + si)); }

// |a - b| of two words as SUB / JNS / NEG compute it (8000h stays 8000h).
u16 distance(u16 a, u16 b)
{
    const u16 d = u16(a - b);
    return s16(d) < 0 ? u16(-d) : d;
}

// The phase scramble of the enemies' gates (0919:2fe4, 310c): SHR AL,1, three RCR AL,1, and 10h
// when the last carry is set.
u8 phase_scramble(u8 al)
{
    bool cf = al & 1;
    al >>= 1;
    for (int i = 0; i < 3; i++) {
        const bool out = al & 1;
        al = u8((cf ? 0x80 : 0) | (al >> 1));
        cf = out;
    }
    if (cf) al |= 0x10;
    return al;
}

// The effects (kinds 3Fh and above, 0919:2f39): the wakes 3Fh -> 40h -> 41h -> 3Fh (the lifetime in
// the flags byte counts down at 3Fh and removes the wake at 0), stored on even world passes only;
// the other effects keep their kind while the flags byte counts down, then take the next kind with
// a new count (43h/44h: 2, then removed at 44h; 45h..47h: 2, removed at 48h; 49h/4Ah: 4; 4Bh
// removed; 4Ch..51h: 1; 52h and above removed). A flags byte of 0 never changes.
void effect_step(u16 bx, u16 ax)
{
    u8 al = u8(ax), ah = u8(ax >> 8);
    auto store = [&](u8 lo, u8 hi) { object_word(bx) = u16(hi << 8 | lo); };
    if (al == 0x3F) {
        ah--;
        if (ah == 0) {
            store(0, 0);
            return;
        }
    }
    al++;
    if (al <= 0x42) {
        if (al == 0x42) al = u8(al - 3);
        if (!(ds_u8(DS_world_pass_counter) & 1)) store(al, ah);
        return;
    }
    al--;
    if (ah == 0) return;
    ah--;
    store(al, ah);
    if (ah != 0) return;
    al++;
    ah = 1;
    if (al <= 0x4B) {
        if (al == 0x4B) {
            store(0, 0);
            return;
        }
        ah = 2;
        if (al < 0x44) {
            store(al, ah);
            return;
        }
        if (al == 0x44) {
            store(0, 0);
            return;
        }
        if (al < 0x48) {
            store(al, ah);
            return;
        }
        if (al == 0x48) {
            store(0, 0);
            return;
        }
        ah = 4;
        if (al < 0x4B) {
            store(al, ah);
            return;
        }
    }
    if (al < 0x52) store(al, ah);
    else store(0, 0);
}

// Firing (0919:2fce), for an alerted and active hostile at entry SI (2*entry), object BX: not the
// missile's source; a phase gate on the entry (weapon classes up to 2 masked with C1h) and the
// region; a random gate by the fire-rate class (one class less with flag bit 3); a weapon class, the
// class-1 range (distance class / 2 below 6), the region's range, flag bit 5 clear and a line of
// sight. Kind 14h (RNG byte 0089 & 3) and kind 17h at distance class 3 or more (& 0Fh) launch the
// missile when none is flying and a temporary slot is free; everything else schedules a shot.
void enemy_fire(u16 si, u16 bx)
{
    if (!(ds_u8(DS_enemy_class) & 0xC0)) return;
    if (bx == ds_u16(DS_missile_source)) return;
    const u16 region = ds_u8(DS_region);
    u8 al = phase_scramble(u8(si));
    al = u8(al + ds_u8(DS_world_pass_counter));
    if ((ds_u8(DS_enemy_behaviour) & 0x1C) <= 8) al &= 0xC1;
    al &= ds_u8(u16(DS_region_fire_phase + region));
    if (al != 0) return;
    u8 rate = u8(ds_u8(DS_enemy_class) & 0xC0);
    rate = u8(rate << 2 | rate >> 6);  // ROL BL,1 twice
    if (ds_u8(DS_vec_sin) & 8) rate--;
    if (ds_u8(DS_rng_state + 2) & ds_u8(u16(DS_fire_rate_mask + rate))) return;
    al = u8(ds_u8(u16(DS_visible_bearing + (si >> 1))) + ds_u8(DS_view_heading) - 0x48);
    ds_u8(DS_scratch_b7e6) = al;
    ds_u8(DS_vec_angle) = al;
    al = ds_u8(DS_enemy_behaviour) & 0x1C;
    if (al == 0) return;
    if (al < 8 && ds_u8(DS_scratch_b7e2) >= 6) return;
    if (ds_u8(DS_scratch_b7e2) >= ds_u8(u16(DS_region_max_range + region))) return;
    if (ds_u8(DS_vec_sin) & 0x20) return;
    if (line_of_sight(si, 0) >> 8) return;
    bx = visible_object(si);
    const u8 kind = object_kind(bx);
    u8 mask = 3;
    bool missile = kind == 0x14;
    if (kind == 0x17) {
        mask = 0x0F;
        missile = ds_u8(u16(DS_visible_distance + si)) >= 3;
    }
    if (missile && (ds_u8(DS_rng_state + 1) & mask) == 0 && ds_u8(DS_missile_age) == 0) {
        const u16 slot = free_temp_object();
        if (u8(slot) < 0x46) {
            ds_u16(DS_missile_object) = slot;
            object_word(slot) = 0x0012;
            bx = visible_object(si);
            ds_u16(DS_missile_source) = bx;
            ds_u8(DS_missile_age) = 1;
            const u16 x = object_x(bx), y = object_y(bx);
            object_x(ds_u16(DS_missile_object)) = x;
            object_y(ds_u16(DS_missile_object)) = y;
            ds_u16(DS_missile_target_x) = u16(ds_u16(DS_object_x) + 2);
            ds_u16(DS_missile_target_y) = u16(ds_u16(DS_object_y) + 1);
            const u8 behaviour = ds_u8(DS_enemy_behaviour), cls = ds_u8(DS_enemy_class);
            show_message(0x2F);
            ds_u8(DS_enemy_class) = cls;
            ds_u8(DS_enemy_behaviour) = behaviour;
            return;
        }
    }
    schedule_shot(ds_u8(DS_enemy_behaviour), si);
}

// Spotting (0919:3104), for a hostile not yet alerted or not active, object BX: a phase gate on the
// object offset; then s = (night: 4) + distance class / 2 (/ 8 when alerted but not active), / 4 at
// high throttle (sum 88h or more), / 2 with the spotlight on; below 12h the object becomes alerted
// and active when RNG byte 0088 is below spot_thresholds[s].
void enemy_spot(u16 si, u16 bx)
{
    const u16 region = ds_u8(DS_region);
    u8 al = phase_scramble(u8(bx));
    al = u8(al + ds_u8(DS_world_pass_counter));
    al &= 0xF8;
    al &= ds_u8(u16(DS_region_fire_phase + region));
    if (al != 0) return;
    al = u8(u8(u8(ds_u8(DS_daylight) << 2) ^ 4) + ds_u8(DS_scratch_b7e2));
    if (u8(ds_u8(DS_throttle) + ds_u8(DS_throttle + 1)) >= 0x88) al >>= 2;
    if (ds_u8(DS_spotlight_on) != 0) al >>= 1;
    if (al >= 0x12) return;
    if (ds_u8(DS_rng_state) >= ds_u8(u16(DS_spot_thresholds + al))) return;
    object_flags(visible_object(si)) |= 0xC0;
}

// Movement (0919:3164): behaviour 2..7 (enemy_behaviour >> 5), flag bit 4 clear, class bit 5 set.
// Circling (4..7): every 8 passes a wake, every 16 the facing (flag bits 0-2) turns +1 (-1 for 6, 7);
// patrol (2, 3): every 64 passes the facing reverses. Then the position moves by enemy_velocity
// [facing] (the fast table for odd behaviours).
void enemy_move(u16 si)
{
    const u8 ah = u8(ds_u8(DS_enemy_behaviour) >> 5);
    if (ah < 2) return;
    if (ds_u8(DS_vec_sin) & 0x10) return;
    if (!(ds_u8(DS_enemy_class) & 0x20)) return;
    u16 bx = visible_object(si);
    u8 cl = object_flags(bx);
    const u8 pass = ds_u8(DS_world_pass_counter);
    if (ah >= 4) {
        if ((pass & 7) == 0) {
            bx = spawn_enemy_wake(bx, si);
            if ((ds_u8(DS_world_pass_counter) & 0x0F) == 0) {
                u8 al = u8(cl + 1);
                if (ah >= 6) al = u8(al - 2);
                cl = u8((cl & 0xF8) | (al & 7));
            }
        }
    } else if ((pass & 0x3F) == 0) {
        cl ^= 4;
    }
    object_flags(bx) = cl;
    u16 v = u16((cl & 7) << 1);
    if (ah & 1) v = u16(v + 0x10);
    object_x(bx) = u16(object_x(bx) + s8(ds_u8(u16(DS_enemy_velocity + v))));
    object_y(bx) = u16(object_y(bx) + s8(ds_u8(u16(DS_enemy_velocity + 1 + v))));
}

} // namespace

// 0919:1e87 mission_stop (simulation.md §5.2, when the boat stands still): for mission types 2
// (insertion), 4 and 0Ah (extraction). Not locked: with the objective pending and the boat within
// 80h of the first object of the mission's row on both axes, the boat locks and five passengers
// appear in objects 1..5 (flags 5, 4, 3, 2, 1): for an insertion kind 25h at the boat, X spread by
// (word >> 4) - 20h; for an extraction kind 24h at the row's first object and the four objects after
// it. Locked: each passenger walks (move_toward, steps 2) to the boat (kind 24h) or to its row
// object; within 10h (|dx| + |dy|) it disappears; after the fifth the boat is free, the objective is
// done (B544 = 5, objective lamp 3), sound 4, "MISSION ACCOMPLISHED!" on page 0. Returns SI as the
// original leaves it (the last row object it loaded); DI is left changed too (not passed on).
u16 mission_stop(u16 si)
{
    const u16 type = ds_u16(DS_mission_type);
    u16 bx = u16(ds_u16(DS_mission_number) << 1);
    const u16 ax2 = bx;
    bx = u16(bx << 2);
    bx = u16(bx + ax2);  // 10 * mission number: the mission's row of mission_table
    const u8 cl = u8(type);
    if (cl != 2 && cl != 4 && cl != 0x0A) return si;
    if (ds_u8(DS_boat_locked) == 0) {
        if ((ds_u8(DS_objective_state) & 3) != 2) return si;
        si = ds_u16(u16(DS_mission_table + bx));
        if (distance(object_x(si), ds_u16(DS_object_x)) >= 0x80) return si;
        if (distance(object_y(si), ds_u16(DS_object_y)) >= 0x80) return si;
        ds_u8(DS_boat_locked) = 1;
        ds_u8(DS_passengers_pending) = 5;
        u16 b = 2;
        u8 kind = 0x24;
        if (cl == 2) {
            kind = 0x25;
            si = 0;
        }
        for (u8 ch = 5; ch != 0; ch--) {
            const u16 cx = u16(ch << 8 | kind);
            object_word(b) = cx;
            u16 x = object_x(si);
            object_y(b) = object_y(si);
            if (kind != 0x24) {
                x = u16(x + (cx >> 4));
                x = u16(x - 0x20);
                si = u16(si - 2);
            }
            object_x(b) = x;
            si = u16(si + 2);
            b = u16(b + 2);
        }
        return si;
    }
    u16 di = bx;
    bx = 2;
    ds_u8(DS_scratch_b7e2) = 5;
    do {
        if (object_word(bx) != 0) {
            u16 cx = ds_u16(DS_object_x), dx = ds_u16(DS_object_y);
            if (object_kind(bx) != 0x24) {
                si = ds_u16(u16(DS_mission_table + di));
                cx = object_x(si);
                dx = object_y(si);
            }
            const u16 d = u16(distance(object_x(bx), cx) + distance(object_y(bx), dx));
            if (d < 0x10) {
                object_word(bx) = 0;
                if (--ds_u8(DS_passengers_pending) == 0) {
                    ds_u8(DS_boat_locked) = 0;
                    ds_u8(DS_objective_progress) = 5;
                    ds_u8(DS_objective_state) = u8((ds_u8(DS_objective_state) & 0xFC) + 3);
                    sfx_play(4);
                    show_message_page0(0x16);
                    return si;
                }
            } else {
                ds_u16(DS_move_step_x) = 2;
                ds_u16(DS_move_step_y) = 2;
                move_toward(bx, cx, dx);
            }
        }
        di = u16(di + 2);
        bx = u16(bx + 2);
    } while (--ds_u8(DS_scratch_b7e2) != 0);
    return si;
}

// 0919:2038 missile_update (simulation.md §8.4, world group): while a missile flies; every 8 world
// passes it ages. It explodes when its source is a wreck (18h, 31h) or no longer in the visible list
// (searched from missile_source_entry, then from the end); every 4 passes with a line of sight from
// the source it retargets the boat; it moves toward the target by missile_speed, and explodes on
// arrival or at age 42h. The explosion (kind 4Bh, flags 3) at the missile; exactly on the boat, unless
// RNG byte 0089 has bit 20h, the boat is hit by class 7 twice, three times in region 1 and five in
// regions 2 and 3. SI is left at the source's entry (2*entry) once it was found in the list (game_frame
// passes SI on to incoming_fire, which keeps it in caller_si): returns SI.
u16 missile_update(u16 si)
{
    if (ds_u8(DS_missile_age) == 0) return si;
    if ((ds_u8(DS_world_pass_counter) & 7) == 0) ds_u8(DS_missile_age)++;
    u16 bx = ds_u16(DS_missile_source);
    const u8 kind = object_kind(bx);
    bool explode = kind == 0x18 || kind == 0x31;
    if (!explode) {
        const u16 dx = bx;
        bx = ds_u16(DS_missile_source_entry);
        bool found = dx == visible_object(bx);
        if (!found) {
            bx = u16(ds_u16(DS_visible_count) << 1);
            for (;;) {
                bx = u16(bx - 2);
                if (s16(bx) < 0) break;
                if (dx == visible_object(bx)) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            explode = true;
        } else {
            ds_u16(DS_missile_source_entry) = bx;
            si = bx;
            if ((ds_u8(DS_world_pass_counter) & 3) == 0 && (line_of_sight(si, 0) >> 8) == 0) {
                ds_u16(DS_missile_target_x) = ds_u16(DS_object_x);
                ds_u16(DS_missile_target_y) = ds_u16(DS_object_y);
            }
            const u16 step = ds_u8(DS_missile_speed);
            ds_u16(DS_move_step_x) = step;
            ds_u16(DS_move_step_y) = step;
            bx = ds_u16(DS_missile_object);
            move_toward(bx, ds_u16(DS_missile_target_x), ds_u16(DS_missile_target_y));
            const bool arrived =
                object_x(bx) == ds_u16(DS_missile_target_x) && object_y(bx) == ds_u16(DS_missile_target_y);
            if (!arrived && ds_u8(DS_missile_age) < 0x42) return si;
        }
    }
    // 20db: the explosion
    bx = ds_u16(DS_missile_object);
    const u16 cx = object_x(bx), dx = object_y(bx);
    object_word(bx) = 0x034B;
    ds_u8(DS_missile_age) = 0;
    ds_u16(DS_missile_source) = 0;
    ds_u16(DS_missile_object) = 0;
    if (ds_u16(DS_object_x) != cx || ds_u16(DS_object_y) != dx) return si;
    if (ds_u8(DS_rng_state + 1) & 0x20) return si;
    boat_hit(7);
    boat_hit(7);
    if (ds_u8(DS_region) < 1) return si;
    boat_hit(7);
    if (ds_u8(DS_region) < 2) return si;
    boat_hit(7);
    boat_hit(7);
    return si;
}

// 0919:2ea8 enemy_update (simulation.md §8.3, world group): nothing while the scene is rebuilt. The
// caller's SI is kept in caller_si. Bit 20h of the byte at world_a_data + [world_a_data] toggles every
// 2^n world passes (n = the time compression, 2 while Backspace is held). world_pass_counter + 1.
// Then the visible list from its last entry down, the entry number in scratch_b7dc (reloaded from
// there each time: quirk, a message printed meanwhile leaves its last character there): the effects
// (kinds 3Fh and above), the two-frame objects (28h <-> 29h every 16 passes; 33h -> 34h -> 35h every 8),
// and the hostiles (01h..18h) within distance class 18h with a behaviour: spotting, firing, movement.
void enemy_update(u16 si_in)
{
    if (ds_u8(DS_scene_rebuild) != 0) return;
    ds_u16(DS_caller_si) = si_in;
    const u16 flag_byte = u16(ds_u16(DS_world_a_data) + DS_world_a_data);
    u8 al = ds_u8(flag_byte);
    u8 cl = 2;
    if (ds_u8(DS_fast_forward_key) == 0) cl = ds_u8(DS_time_compression);
    // SHL AH,CL: the count is masked to 5 bits (286 and later, as the emulator)
    u8 ah = u8(u8(u32(1) << (cl & 0x1F)) - 1);
    ah &= ds_u8(DS_world_pass_counter);
    if (ah == 0) {
        al ^= 0x20;
        ds_u8(flag_byte) = al;
    }
    ds_u8(DS_world_pass_counter)++;
    u16 cx = ds_u16(DS_visible_count);
    for (;; cx = ds_u16(DS_scratch_b7dc)) {
        cx--;
        const u16 si = u16(cx << 1);
        if (si & 0x8000) return;
        ds_u16(DS_scratch_b7dc) = cx;
        u16 bx = visible_object(si);
        u16 ax = object_word(bx);
        const u8 kind = u8(ax);
        if (kind == 0) continue;
        if (kind >= 0x3F) {
            effect_step(bx, ax);
            continue;
        }
        if (kind >= 0x35) continue;
        if (kind >= 0x19) {
            if (kind < 0x28) continue;
            if (kind <= 0x29) {
                ax = kind == 0x28 ? u16(ax + 1) : u16(ax - 1);
                if (!(ds_u8(DS_world_pass_counter) & 0x0F)) object_word(bx) = ax;
                continue;
            }
            if (kind < 0x33) continue;
            ax = u16(ax + 1);
            if (!(ds_u8(DS_world_pass_counter) & 7)) object_word(bx) = ax;
            continue;
        }
        // 2f88: a hostile
        u8 d = ds_u8(u16(DS_visible_distance + si));
        if (d >= 0x18) continue;
        d >>= 1;
        ds_u8(DS_scratch_b7e2) = d;
        const u16 k2 = u16(kind << 1);
        const u8 behaviour = ds_u8(u16(DS_object_behaviour + k2));
        if (!(behaviour & 0xE0)) continue;
        ds_u8(DS_enemy_behaviour) = behaviour;
        ds_u8(DS_enemy_class) = ds_u8(u16(DS_object_class + k2));
        bx = visible_object(si);
        const u8 flags = object_flags(bx);
        ds_u8(DS_vec_sin) = flags;  // DS:B7E8, the flags byte for the tests below
        if (flags & 0x80) {
            if (flags & 0x40) {
                enemy_fire(si, bx);
            } else {
                ds_u8(DS_scratch_b7e2) = u8(ds_u8(DS_scratch_b7e2) >> 1);
                ds_u8(DS_scratch_b7e2) = u8(ds_u8(DS_scratch_b7e2) >> 1);
                enemy_spot(si, bx);
            }
        } else {
            enemy_spot(si, bx);
        }
        enemy_move(si);
    }
}

// 0919:3217 schedule_shot (simulation.md §8.3): AL = the shooter's behaviour byte, SI = 2*entry,
// scratch_b7e2 = distance class / 2. Accuracy 8 * (AL & 3) + 6. Weapon class 5 (AL & 1Ch = 14h) is a
// salvo, only with incoming slots 8..15 all free: "Salvo coming in!" (35h), eight shots A0h |
// (RNG byte 008B & 18h) + k with timers d/2 + 1 + 7k, the hull heading and speed saved. Otherwise the
// first free slot 0..7 gets ((AL << 3) & E0h) | accuracy and timer d/2 + 1, and a muzzle flash object
// (kind 53h, flags 1; 54h for shooter kinds 0Dh..0Fh; 52h for weapon classes above 2) at the shooter.
void schedule_shot(u8 al, u16 si)
{
    ds_u8(DS_scratch_b7e3) = al;
    u8 ah = al;
    al = u8(((((al & 3) << 1 | 1) << 1) | 1) << 1);
    ds_u8(DS_vec_product_lo) = al;
    ah &= 0x1C;
    if (ah == 0x14) {
        for (u16 bx = 8; bx < 0x10; bx++)
            if (ds_u8(u16(DS_incoming_timer + bx)) != 0) return;
        ah = u8(u8(ah << 3) | (ds_u8(DS_rng_state + 3) & 0x18));
        al = u8(u8(ds_u8(DS_scratch_b7e2) >> 1) + 1);
        ds_u8(DS_salvo_heading) = ds_u8(DS_heading);
        ds_u8(DS_salvo_speed) = ds_u8(DS_speed);
        show_message(0x35);
        for (u16 bx = 8; bx < 0x10; bx++) {
            ds_u8(u16(DS_incoming_descriptor + bx)) = ah;
            ds_u8(u16(DS_incoming_timer + bx)) = al;
            ds_u8(DS_incoming_count)++;
            al = u8(al + 7);
            ah++;
        }
        return;
    }
    u16 bx = 0;
    while (ds_u8(u16(DS_incoming_timer + bx)) != 0) {
        if (++bx >= 8) return;
    }
    ds_u8(u16(DS_incoming_descriptor + bx)) = u8((u8(ds_u8(DS_scratch_b7e3) << 3) & 0xE0) | ds_u8(DS_vec_product_lo));
    ds_u8(u16(DS_incoming_timer + bx)) = u8(u8(ds_u8(DS_scratch_b7e2) >> 1) + 1);
    ds_u8(DS_incoming_count)++;
    const u16 src = visible_object(si);
    bx = free_temp_object();
    object_x(bx) = object_x(src);
    object_y(bx) = object_y(src);
    object_word(bx) = 0x0153;
    const u8 kind = object_kind(src);
    if (kind >= 0x0D && kind <= 0x0F) object_word(bx) = 0x0154;
    if ((ds_u8(DS_scratch_b7e3) & 0x1C) > 8) object_word(bx) = 0x0152;
}

} // namespace gb
