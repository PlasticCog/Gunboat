// Weapons, projectiles, hits and object helpers (simulation.md §4.3, §5.2, §6.1, §7, §8.2, §8.3).
#include "game/sim.hpp"

#include "game/flow.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

#include <utility>

namespace gb {

namespace {

// Object tables by byte offset (BX/SI = 2*i).
u16_m &object_word(u16 off) { return ds_u16(u16(DS_object_word + off)); }
u16_m &object_x(u16 off) { return ds_u16(u16(DS_object_x + off)); }
u16_m &object_y(u16 off) { return ds_u16(u16(DS_object_y + off)); }

// Projectile record fields: [si + D1DC..D1E3] with SI = 8*slot.
constexpr u16 REC_WEAPON = 0, REC_X = 1, REC_Y = 3, REC_FRACTION = 5, REC_HEADING = 6, REC_RANGE = 7;
u8 &record(u16 si, u16 field) { return ds_u8(u16(DS_projectile_record + field + si)); }

// A sprite cache slot's descriptor: slot s (1-based sprite index) is in segment sprite_segment_a
// when s >= 69h, else in sprite_segment_b; its offset is sprite_slot_pointers[s - 1] (CS:583D).
FarPtr sprite_descriptor(u8 sprite)
{
    const u16 es = sprite >= 0x69 ? ds_u16(DS_sprite_segment_a) : ds_u16(DS_sprite_segment_b);
    const u8 slot = u8(sprite - 1);
    return {seg_u16(CSSEG_sprite_slot_pointers, u16(CS_sprite_slot_pointers + 2 * slot)), es};
}

// The sine table words at a byte offset from sine_table (CS:3502).
u16 sine_word(u16 off) { return seg_u16(CSSEG_sine_table, u16(CS_sine_table + off)); }

} // namespace

// 0919:0ee2 projectile_launch (simulation.md §7.1): BX = weapon, CH = heading, CL = fraction. A slot
// from projectile_alloc; the elevation is the camera pitch above its reference plus the gun's
// elevation (shot_elevation), clamped to 0..FFh and to the weapon's limit; then projectile_aim.
// Returns AX as projectile_aim leaves it.
u16 projectile_launch(u16 bx, u16 cx)
{
    const SlotBxCx slot = projectile_alloc();
    const u16 si = slot.cx;
    u8 al = u8(ds_u8(DS_bob_position + 1) - ds_u8(DS_pitch_reference));
    al = elevation_add_clamped(al);
    const u8 limit = ds_u8(u16(DS_weapon_elevation_limit + bx));
    if (al >= limit) al = limit;
    return projectile_aim(u16(u8(bx) << 8 | al), slot.bx, cx, si);
}

// 0919:1fc3 move_toward (simulation.md §5.2): moves object BX toward (CX, DX) by at most the steps
// move_step_x / move_step_y. The step on the axis with the smaller distance is halved first (in
// memory). On an axis where the target is below, the step variable is negated in memory (quirk: its
// sign flips on every such call) and used as the lower limit; unsigned compares throughout.
void move_toward(u16 bx, u16 cx, u16 dx)
{
    u16_m &step_x = ds_u16(DS_move_step_x);
    u16_m &step_y = ds_u16(DS_move_step_y);
    const u16 ddx = u16(cx - object_x(bx));
    const u16 ddy = u16(dx - object_y(bx));
    const u16 ax_ = s16(ddx) < 0 ? u16(-ddx) : ddx;
    const u16 ay_ = s16(ddy) < 0 ? u16(-ddy) : ddy;
    if (ax_ != ay_) {
        if (ax_ < ay_) step_x = u16(step_x >> 1);
        else step_y = u16(step_y >> 1);
    }
    u16 mx = ddx;
    if (s16(ddx) < 0) {
        step_x = u16(-step_x);
        if (!(mx > step_x)) mx = step_x;
    } else if (!(mx < step_x)) {
        mx = step_x;
    }
    object_x(bx) = u16(object_x(bx) + mx);
    u16 my = ddy;
    if (s16(ddy) < 0) {
        step_y = u16(-step_y);
        if (!(my > step_y)) my = step_y;
    } else if (!(my < step_y)) {
        my = step_y;
    }
    object_y(bx) = u16(object_y(bx) + my);
}

// 0919:31f1 spawn_enemy_wake (simulation.md §8.3): a wake object (kind 3Fh, flags 0Ah) in a free
// temporary slot at object BX's position. Returns BX = visible_object[SI] (SI = 2*entry).
u16 spawn_enemy_wake(u16 bx, u16 si)
{
    const u16 src = bx;
    const u16 slot = free_temp_object();
    object_word(slot) = 0x0A3F;
    object_x(slot) = object_x(src);
    object_y(slot) = object_y(src);
    return ds_u16(u16(DS_visible_object + si));
}

// 0919:348e line_of_sight (simulation.md §8.2): is visible entry SI/2 covered by a later (nearer)
// entry? An entry covers it when it is a listed object beyond the temporary slots (offset >= 48h),
// not of kind 35h or 10h, with a sprite, and the bearing difference d (signed byte) satisfies
// -w <= d < w with w = descriptor byte 3 >> 2. Returns CX: CH = 1 when covered, else 0; CL as the
// original leaves it (the last examined sprite index - 1; callers use only CH).
u16 line_of_sight(u16 si, u16 cx)
{
    u8 cl = u8(cx);
    si >>= 1;
    const u16 bx = si;
    for (;;) {
        si++;
        if (!(si < ds_u16(DS_visible_count))) return cl;
        const u16 di = ds_u16(u16(DS_visible_object + u16(si << 1)));
        si = u16(u16(si << 1) >> 1);  // shl si,1 / shr si,1 drops bit 15
        if (di < 0x48) continue;
        const u8 kind = ds_u8(u16(DS_object_word + di));
        if (kind == 0x35 || kind == 0x10) continue;
        const u8 al = u8(ds_u8(u16(DS_visible_bearing + bx)) - ds_u8(u16(DS_visible_bearing + si)));
        const u8 sprite = ds_u8(u16(DS_visible_sprite + si));
        cl = sprite;
        if (sprite == 0) continue;
        const FarPtr d = sprite_descriptor(sprite);
        cl = u8(sprite - 1);
        const u8 ah = u8(far_u8(d, 3) >> 2);
        if (s8(al) >= s8(ah)) continue;
        if (s8(u8(al + ah)) >= 0) return u16(0x0100 | cl);
    }
}

// 0919:37af projectile_alloc (simulation.md §7.1): the highest free projectile slot (countdown 0),
// or slot 0 when all are busy. Returns BX = slot, CX = 8*slot.
SlotBxCx projectile_alloc()
{
    s16 bx = 0x1F;
    for (; bx >= 0; bx--) {
        if (ds_u8(u16(DS_projectile_timer + bx)) == 0) break;
    }
    if (bx < 0) bx = 0;
    return {u16(bx), u16(bx << 3)};
}

// 0919:37c7 projectile_aim (simulation.md §7.1): AL = elevation, AH = weapon, BX = slot, CH:CL =
// heading:fraction, SI = 8*slot. The flight countdown is (AL >> 3) - 0Eh (at least 0) + 1; the range
// byte is -AL - 13h (at least 0); the distance factor 7FFFh (range <= 1) or FFFFh / range; the
// impact point is the boat's position plus that factor times the sine and cosine of the heading
// (interpolated between table words by the fraction's two low bits), rotated into the heading's
// quadrant, each >> 3 (arithmetic). Returns AX as the original leaves it: AH from the low word of
// the second product, AL = 1 in the fourth quadrant, else 0 (what the quadrant count-down leaves).
u16 projectile_aim(u16 ax, u16 bx, u16 cx, u16 si)
{
    u8 al = u8(ax);
    const u8 ah = u8(ax >> 8);
    u8 dl = u8((al >> 3) - 0x0E);
    if (s8(dl) < 0) dl = 0;
    dl++;
    ds_u8(u16(DS_projectile_timer + bx)) = dl;
    al = u8(-al);
    record(si, REC_WEAPON) = ah;
    const u8 fraction = u8(cx), heading = u8(cx >> 8);
    record(si, REC_FRACTION) = fraction;
    record(si, REC_HEADING) = heading;
    const u8 range = al >= 0x13 ? u8(al - 0x13) : 0;
    record(si, REC_RANGE) = range;
    u16 factor = 0x7FFF;
    if (range > 1) factor = div32_16(0xFFFF, range);
    ds_u8(DS_scratch_b7f5) = u8(heading >> 6);
    const u8 f = u8(fraction << 6);  // the two bits that interpolate
    const u16 i4 = u16(((heading & 0x3F) << 1) << 1);

    u16 d = sine_word(i4);
    u16 step = u16(u16(sine_word(u16(i4 + 2)) - d) >> 1);
    if (f & 0x80) d = u16(d + step);
    step >>= 1;
    if (f & 0x40) d = u16(d + step);
    ds_u16(DS_scratch_b7dc) = d;

    const u16 j = u16(u16(0x100 - i4));
    u16 c = sine_word(j);
    step = u16(u16(c - sine_word(u16(j - 2))) >> 1);
    if (f & 0x80) c = u16(c - step);
    step >>= 1;
    if (f & 0x40) c = u16(c - step);

    u16 hc = u16((u32(factor) * c) >> 16);                         // mul dx; mov cx,dx
    const u32 product = u32(factor) * ds_u16(DS_scratch_b7dc);    // mul dx
    u16 hd = u16(product >> 16);
    u16 rdx = hc, rcx = hd;                                          // xchg dx,cx
    u8 q = ds_u8(DS_scratch_b7f5);
    const u16 ax_out = u16((u16(product) & 0xFF00) | (q == 3 ? 1 : 0));
    if (q != 0) {
        std::swap(rdx, rcx);
        rdx = u16(-rdx);
        if (--q != 0) {
            rcx = u16(-rcx);
            std::swap(rdx, rcx);
            if (--q != 0) {
                std::swap(rdx, rcx);
                rdx = u16(-rdx);
            }
        }
    }
    rcx = u16(s16(rcx) >> 3);
    rdx = u16(s16(rdx) >> 3);
    rcx = u16(rcx + ds_u16(DS_object_x));
    record(si, REC_X) = u8(rcx);
    record(si, REC_X + 1) = u8(rcx >> 8);
    rdx = u16(rdx + ds_u16(DS_object_y));
    record(si, REC_Y) = u8(rdx);
    record(si, REC_Y + 1) = u8(rdx >> 8);
    return ax_out;
}

// 0919:3b3b elevation_add_clamped (simulation.md §7.1): AL (signed) + shot_elevation, clamped to
// 0..FFh by the carry: a negative AL without a carry gives 0, a positive one with a carry FFh.
u8 elevation_add_clamped(u8 al)
{
    const u8 e = ds_u8(DS_shot_elevation);
    const bool carry = al + e > 0xFF;
    const u8 sum = u8(al + e);
    if (s8(al) < 0) return carry ? sum : 0;
    return carry ? 0xFF : sum;
}

// 0919:3b9f score_add (simulation.md §7.2): score word at DS:B52E + DL += 1 (BCD).
void score_add(u8 dl)
{
    u16_m &w = ds_u16(u16(DS_score_words + 4 + dl));
    w = bcd_inc(w);
}

// 0919:3bbc hit_test (simulation.md §7.2): does the shot (bearing shot_bearing, range byte
// shot_range) hit visible entry BX? c = |entry bearing:fine bearing + view heading - shot_bearing -
// 4C00h| (16-bit), its low byte rotated right by 3, then >> 6: must be < 28h. With the sprite's
// descriptor bytes w = +3, h = (+4 + +6) << 2 (8-bit): c <= 2w; e = entry elevation - range + 2 must
// be 0..7Fh and <= 2h; c + e must be <= 2(w + h) (all byte arithmetic, as below). BX = entry (not
// doubled). Returns AH = 1 on a hit, else 0 (AL is kept; CX and DX are clobbered).
u8 hit_test(u16 bx)
{
    u16 cx = u16(ds_u8(u16(DS_visible_bearing + bx)) << 8 | ds_u8(u16(DS_visible_fine_bearing + bx)));
    cx = u16(cx - ds_u16(DS_shot_bearing));
    cx = u16(cx + (ds_u8(DS_view_heading) << 8 | ds_u8(DS_view_heading_fraction)));  // add cl / adc ch
    cx = u16(cx - 0x4C00);
    if (s16(cx) < 0) cx = u16(-cx);
    u8 cl = u8(cx);
    cl = u8(cl >> 3 | cl << 5);  // ror cl,1 three times
    cx = u16((cx & 0xFF00) | cl);
    cx = u16(cx >> 6);
    if (cx >= 0x28) return 0;
    const FarPtr d = sprite_descriptor(ds_u8(u16(DS_visible_sprite + bx)));
    u8 dl = far_u8(d, 3);
    u8 dh = u8(far_u8(d, 4) + far_u8(d, 6));
    dh = u8(dh << 2);
    const u8 ah = u8(u8(dl + dh) << 1);
    dl = u8(dl << 1);
    dh = u8(dh << 1);
    if (u8(cx) > dl) return 0;
    dl = u8(ds_u8(u16(DS_visible_elevation + bx)) - ds_u8(DS_shot_range));
    dl = u8(dl + 2);
    if (s8(dl) < 0) return 0;
    if (dl > dh) return 0;
    cx = u16(cx + dl);
    if (cx >> 8) return 0;
    if (u8(cx) > ah) return 0;
    return 1;
}

// 0919:3c51 terrain_structure_break (simulation.md §7.3): only when the old kind AH is a bridge-type
// object (10h, 20h, 11h, 21h). The structure list is scanned from its last entry down (with a count
// of 0 the first pass reads the word before the list); the first structure in object SI's 1024-unit
// cell whose piece is below 62h and not 5Dh changes (<5Ch -> 62h, 5Ch -> 0, 5Dh..5Fh -> 5Dh,
// 60h/61h -> 63h; the high byte is kept) and the terrain is rebuilt. Only that one structure.
void terrain_structure_break(u8 ah, u16 si)
{
    if (ah != 0x10 && ah != 0x20 && ah != 0x11 && ah != 0x21) return;
    u16 bx = u16(u16(ds_u16(DS_structure_count) - 1) << 1);
    const u16 cx = object_x(si) & 0xFC00;
    const u16 dx = object_y(si) & 0xFC00;
    do {
        if ((ds_u16(u16(DS_structure_x + bx)) & 0xFC00) == cx && (ds_u16(u16(DS_structure_y + bx)) & 0xFC00) == dx) {
            const u16 w = ds_u16(u16(DS_structure_piece + bx));
            const u8 al = u8(w);
            if (al != 0x5D && al < 0x62) {
                u8 piece = 0x62;
                if (al >= 0x5C) piece = al == 0x5C ? 0 : al <= 0x5F ? 0x5D : 0x63;
                ds_u16(u16(DS_structure_piece + bx)) = u16((w & 0xFF00) | piece);
                ds_u16(DS_terrain_rebuild) = 0xFFFF;
                return;
            }
        }
        bx = u16(bx - 2);
    } while (s16(bx) >= 0);
}

// 0919:6f2a free_temp_object (simulation.md §4.3): BX = 2*i of the first object i in 1..35 whose kind
// byte is 0; 46h (object 35) when none is free, so the caller then overwrites object 35 (quirk).
u16 free_temp_object()
{
    u16 bx = 0;
    do {
        bx += 2;
        if (ds_u8(u16(DS_object_word + bx)) == 0) break;
    } while (bx < 0x46);
    return bx;
}

// 0919:81f8 muzzle_flash_tick (simulation.md §6.1): the four flash counters count down to 0.
void muzzle_flash_tick()
{
    for (u16 off : {DS_flash_midship, u16(DS_flash_bow), u16(DS_flash_bow + 1), DS_flash_stern}) {
        if (ds_u8(off) != 0) ds_u8(off)--;
    }
}

// 0919:38cc projectile_tick (simulation.md §7.1, once per frame): nothing while the scene is rebuilt;
// otherwise the flight countdowns of slots 31..0, and projectile_impact for each that reaches 0. SI is
// the caller's (projectile_impact passes it on to caller_si).
void projectile_tick(u16 si)
{
    if (ds_u8(DS_scene_rebuild) != 0) return;
    for (s16 bx = 0x1F; bx >= 0; bx--) {
        u8 &timer = ds_u8(u16(DS_projectile_timer + bx));
        if (timer == 0) continue;
        if (--timer == 0) projectile_impact(u16(bx), si);
    }
}

// 0919:38ed projectile_impact (simulation.md §7.2): slot BX lands. The weapon goes to vec_product_hi
// (DS:B7EA), the shot's bearing (heading:fraction) and range to shot_bearing / shot_range; an
// explosion object (kind 42h, 4Bh for weapons 2 and 3, flags 3) at the impact point in a free
// temporary slot; then mark_near_objects and the hit tests.
void projectile_impact(u16 bx, u16 si)
{
    bx = u16(bx << 3);
    ds_u8(DS_vec_product_hi) = record(bx, REC_WEAPON);
    ds_u16(DS_shot_bearing) = u16(record(bx, REC_HEADING) << 8 | record(bx, REC_FRACTION));
    ds_u8(DS_shot_range) = record(bx, REC_RANGE);
    const u16 cx = u16(record(bx, REC_X + 1) << 8 | record(bx, REC_X));
    const u16 dx = u16(record(bx, REC_Y + 1) << 8 | record(bx, REC_Y));
    const u16 slot = free_temp_object();
    u8 al = 0x42;
    const u8 weapon = ds_u8(DS_vec_product_hi);
    if (weapon > 1 && weapon < 4) al = 0x4B;
    object_word(slot) = u16(0x0300 | al);
    object_x(slot) = cx;
    object_y(slot) = dx;
    mark_near_objects(si);
}

// 0919:3948 mark_near_objects (simulation.md §7.2): SI goes to caller_si. Every visible entry with a
// kind below 19h and a distance class below 7 is alerted (flag bit 80h); then hit_objects.
void mark_near_objects(u16 si)
{
    ds_u16(DS_caller_si) = si;
    u16 bx = u16(ds_u16(DS_visible_count) - 1);
    if (s16(bx) >= 0) {
        bx = u16(bx << 1);
        do {
            const u16 obj = ds_u16(u16(DS_visible_object + bx));
            if (ds_u8(u16(DS_object_word + obj)) < 0x19 && ds_u8(u16(DS_visible_distance + bx)) < 7)
                ds_u8(u16(DS_object_word + 1 + obj)) |= 0x80;
            bx = u16(bx - 2);
        } while (s16(bx) >= 0);
    }
    hit_objects();
}

// 0919:3977 hit_objects (simulation.md §7.2): the visible list from its last entry down; the first
// entry the shot hits (hit_test) takes it, and the routine ends with it. Skipped: kind 0, kinds 3Fh
// and above, the boat and the temporary objects (offset below 48h) except a missile (12h), wrecks
// 30h/31h (the shot goes on), and kinds 17h and above three times in four (RNG byte 0089 & 0Ch). A
// hit on a kind above 31h stops the shot. A friendly (19h..27h) hit counts, stops the crew's fire and
// says "HEY! That's friendly!". Class byte 0: nothing more; class 1 (exactly): weapons 2 and 3 wreck it
// (31h, 30h below kind 2Ah). Otherwise damage_matrix[(class & 18h) | weapon] (bit 7: OR into the
// damage bits, else added) below 38h: "Good shot." (0Eh); at 38h the object is destroyed into its
// wreck (class & 7: 30h + n, 0, 38h, 37h, 4Bh for class 7 or 18h), a fire object beside it unless the
// wreck is 32h, 33h or 37h or it was a missile (12h/13h) (when the temporary slots are full, not in
// the last slot nor the missile's), its score word, the objectives, and "Target destroyed." or
// "MISSION ACCOMPLISHED!". The message goes to page 0 unless the object was friendly.
void hit_objects()
{
    u16 bx = ds_u16(DS_visible_count);
    for (;;) {
        bx--;
        if (s16(bx) < 0) return;
        const u16 si = ds_u16(u16(DS_visible_object + u16(bx << 1)));
        u8 al = ds_u8(u16(DS_object_word + si));
        if (al != 0x12 && si < 0x48) continue;
        if (al == 0 || al >= 0x3F) continue;
        if (hit_test(bx) == 0) continue;
        if (al > 0x31) return;
        if (al >= 0x30) continue;
        const u16 word = object_word(si);
        al = u8(word);
        u8 ah = u8(word >> 8) & 0x38;
        if (al == 0x12) {
            ah = 0x30;
            ds_u8(DS_missile_age) = 0;
            ds_u16(DS_missile_source) = 0;
            ds_u16(DS_missile_object) = 0;
        }
        ds_u8(DS_shot_elevation) = ah;  // DS:B7F8: here the object's damage bits
        ds_u8(DS_hit_kind) = al;
        if (al >= 0x17) {
            if (ds_u8(DS_rng_state + 1) & 0x0C) continue;
            if (al >= 0x19 && al < 0x28) {
                ds_u8(DS_friendly_hits)++;
                // (the original reloads SI from caller_si around the message: no effect on memory)
                ds_u16(DS_fire_at_will) = 0;
                show_message_page0(0x15);
            }
        }
        // 39fc: the damage
        const u16 k2 = u8(al << 1);
        u8 cls = ds_u8(u16(DS_object_class + k2));
        if (cls == 0) return;
        u8 wreck;
        const u8 variant = cls & 7;
        if (variant < 4) wreck = u8(variant + 0x30);
        else if (variant == 4) wreck = 0;
        else if (variant == 5) wreck = 0x38;
        else if (variant == 6) wreck = 0x37;
        else wreck = cls == 7 ? 0x4B : 0x18;
        ds_u8(DS_scratch_b7e2) = wreck;
        cls = ds_u8(u16(DS_object_class + k2));
        if (cls == 1) {
            const u8 weapon = ds_u8(DS_vec_product_hi);
            if (weapon == 1 || weapon >= 4) return;
            const u8 old = ds_u8(u16(DS_object_word + si));
            ds_u8(u16(DS_object_word + si)) = 0x31;
            if (old < 0x2A) ds_u8(u16(DS_object_word + si)) = 0x30;
            return;
        }
        u8 m = seg_u8(CSSEG_damage_matrix, u16(CS_damage_matrix + u8((cls & 0x18) | ds_u8(DS_vec_product_hi))));
        if (m & 0x80) m = u8((m & 0x7F) | ds_u8(DS_shot_elevation));
        else m = u8(m + ds_u8(DS_shot_elevation));
        u8 message;
        if (m < 0x38) {
            ds_u8(DS_shot_elevation) = m;
            u8 &flags = ds_u8(u16(DS_object_word + 1 + si));
            flags = u8((flags & 0xC7) | ds_u8(DS_shot_elevation));
            message = 0x0E;
        } else {
            if (ds_u8(DS_scratch_b7e2) == 0x4B) {
                ds_u8(u16(DS_object_word + 1 + si)) = 1;
                sfx_play(8);
            }
            const u8 old = ds_u8(u16(DS_object_word + si));
            const u8 kind = ds_u8(DS_scratch_b7e2);
            ds_u8(u16(DS_object_word + si)) = kind;
            terrain_structure_break(old, si);
            u8 ah_left = old;  // AH: the old kind, or 04h after the fire object (MOV AX,0448h)
            if (!(kind == 0x33 || kind == 0x32 || kind == 0x37 || old == 0x12 || old == 0x13)) {
                const u16 x = object_x(si), y = object_y(si);
                u16 slot = free_temp_object();
                if (slot == 0x46) {
                    slot = u16((slot & 0xFF00) | u8(slot - 2));
                    if (slot == ds_u16(DS_missile_object)) slot = u16((slot & 0xFF00) | u8(slot - 2));
                }
                object_word(slot) = 0x0448;
                object_x(slot) = x;
                object_y(slot) = y;
                ah_left = 0x04;
            }
            u8 score = ds_u8(u16(DS_object_score_index + old));
            if (score != 0) {
                score = u8(score << 1);
                ds_u8(DS_scratch_b7e2) = ah_left;  // quirk: 04h instead of the old kind after a fire
                score_add(score);
            }
            message = mission_target_check(si);
        }
        if (ds_u8(DS_hit_kind) < 0x19 || ds_u8(DS_hit_kind) >= 0x28) show_message_page0(message);
        return;
    }
}

// 0919:3b51 mission_target_check (simulation.md §7.2): object SI destroyed. If it is one of the five
// objective objects, the objective progress counts; at 3 the objective lamp (3) is set on page 0,
// sound 4, and the message is "MISSION ACCOMPLISHED!" (16h). Otherwise "Target destroyed." (0Fh).
u8 mission_target_check(u16 si)
{
    for (s16 bx = 8; bx >= 0; bx = s16(bx - 2)) {
        if (si != ds_u16(u16(DS_objective_objects + bx))) continue;
        ds_u8(DS_objective_progress)++;
        if (ds_u8(DS_objective_progress) != 3) return 0x0F;
        const u16 page = ds_u16(DS_draw_page);
        ds_u16(DS_draw_page) = 0;
        gfx_set_draw_page(0);
        indicator_draw(0x0303);
        ds_u16(DS_draw_page) = page;
        gfx_set_draw_page(s16(page));
        sfx_play(4);
        return 0x16;
    }
    return 0x0F;
}

} // namespace gb
