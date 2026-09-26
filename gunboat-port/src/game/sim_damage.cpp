// Damage to the boat and its loss (simulation.md §8.6, §8.7): hits on components, the cockpit window,
// sinking, and the boat destroyed.
#include "game/sim.hpp"

#include "hud/hud.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// DS:B7F2, the screen shake steps that screen_shake_step (render3d §7) counts down.
constexpr u16 SHAKE_STEPS = 0xB7F2;

u16_m &object_word(u16 off) { return ds_u16(u16(DS_object_word + off)); }
u16_m &object_x(u16 off) { return ds_u16(u16(DS_object_x + off)); }
u16_m &object_y(u16 off) { return ds_u16(u16(DS_object_y + off)); }

// A condition byte of the boat record by its index (component_condition_index).
u8 &condition(u16 index) { return ds_u8(u16(DS_boat_record + index)); }

// A temporary object at the boat's position + (dx, dy).
void place(u16 word, s16 dx, s16 dy)
{
    const u16 bx = free_temp_object();
    object_word(bx) = word;
    object_x(bx) = u16(ds_u16(DS_object_x) + dx);
    object_y(bx) = u16(ds_u16(DS_object_y) + dy);
}

} // namespace

// 0919:2a37 boat_hit (simulation.md §8.6): a hit of weapon class AL & 7 (shake steps 2, or 8 above
// class 2). A random component r = random() & 0Fh is hit only when below hit_thresholds[class]; above
// class 2 the screen shakes. Then as boat_hit_component(r, message 0Ah "Hit to ...").
void boat_hit(u8 al)
{
    al &= 7;
    ds_u8(DS_scratch_b7e2) = al;
    ds_u8(SHAKE_STEPS) = 2;
    if (al > 2) ds_u8(SHAKE_STEPS) = 8;
    const u8 cls = al;
    const u8 r = u8(random()) & 0x0F;
    if (r >= ds_u8(u16(DS_hit_thresholds + cls))) return;
    if (cls > 2) ds_u8(DS_screen_shake) = 3;
    boat_hit_component(r, 0x0A);
}

// 0919:2a69 boat_hit_component (simulation.md §8.6; also the ramming in render3d §5.4): component AL
// (0..0Fh, kept in hit_thresholds[0] for the message), message CL. Nothing if its condition is already
// 2 (destroyed). The message names it; in practice and in missions 0-1 that is all. An engine
// (condition index 6 / 7) halves its throttle maximum (quirk: after the starboard one the halved
// maximum is compared with 6, and a result of 6 also halves the port one); fuel tanks and engines
// (4..7) leak one level more one time in four. Condition 3 becomes 1, 0 and 1 become 2, the lamp is
// redrawn (and the gun panel of a spotlight). A destroyed engine or fuel tank (0Bh..0Eh) loses the
// boat unless (008A & 0Ch); the captain: main switch off, mission result 1, "Sir, you just died!";
// the gunner at the player's station: the player goes to the damage report; an engine: its switch
// (1 / 2) set to 5 and the engine stopped; then the component's second lamp.
void boat_hit_component(u8 al, u8 cl)
{
    ds_u8(DS_scratch_b7e2) = al;
    ds_u8(DS_hit_thresholds) = al;
    if (al == 0x0C) ds_u8(SHAKE_STEPS) = 8;
    if ((condition(ds_u8(u16(DS_component_condition_index + al))) & 3) == 2) return;
    show_message(cl);
    u16 bx = ds_u8(DS_hit_thresholds);
    if (u8(ds_u16(DS_practice_mode)) != 0) return;
    if (u8(ds_u16(DS_mission_number)) <= 1) return;
    u8 idx = ds_u8(u16(DS_component_condition_index + bx));
    if (idx >= 4 && idx <= 7) {
        if (idx == 7) {
            idx = u8(ds_u8(DS_throttle_max + 1) >> 1);
            ds_u8(DS_throttle_max + 1) = idx;
            if (ds_u8(DS_throttle + 1) > idx) ds_u8(DS_throttle + 1) = idx;
        }
        if (idx == 6) {
            idx = u8(ds_u8(DS_throttle_max) >> 1);
            ds_u8(DS_throttle_max) = idx;
            if (ds_u8(DS_throttle) > idx) ds_u8(DS_throttle) = idx;
        }
        if ((random() & 3) == 0) ds_u8(DS_leak_level)++;
    }
    const u8 ci = ds_u8(u16(DS_component_condition_index + bx));
    const u8 c = condition(ci) & 3;
    if (c == 2) return;
    const u8 ah = c > 2 ? 1 : 2;
    indicator_draw(u16(ah << 8 | ci));
    damage_panel_refresh(ci);
    if (ah != 2) return;
    // destroyed
    const u8 comp = ds_u8(DS_hit_thresholds);
    if (comp >= 0x0B && comp <= 0x0E && (ds_u8(DS_rng_state + 2) & 0x0C) == 0) {
        boat_destroyed();
        return;
    }
    if (comp == 0x0F) {
        // 2b9b: AL is the new condition, 2 here (the branch that sets score word 0 to 1 is unreachable)
        panel_redraw_all(switch_draw(0x0500));
        ds_u8(DS_mission_result) = 1;
        show_message(0x0D);
        return;
    }
    if (comp == 3) {
        if (ds_u16(DS_station) == 4) ds_u16(DS_station) = 7;
        return;
    }
    if (comp == 9) {
        if (ds_u16(DS_station) == 2) ds_u16(DS_station) = 7;
        return;
    }
    if (comp == 4) {
        if (ds_u16(DS_station) == 3) ds_u16(DS_station) = 7;
        return;
    }
    if (comp == 0x0B) {
        switch_draw(0x0501);
        ds_u8(DS_engine_state) = 0;
    } else if (comp == 0x0C) {
        switch_draw(0x0502);
        ds_u8(DS_engine_state + 1) = 0;
    }
    const u8 lamp = ds_u8(u16(DS_component_indicator + ds_u8(DS_hit_thresholds)));
    if (lamp == 0) return;
    indicator_draw(u16(0x8500 | lamp));
}

// 0919:2bbd damage_panel_refresh (simulation.md §8.6): a spotlight hit at the gun station whose panel
// shows it (condition index 0Ch front / bow, 0Dh rear / midship, 0Ah middle / stern) redraws the gun
// panel: gun_panel_copy(the condition byte) on page 0 (draw_page restored after).
void damage_panel_refresh(u8 al)
{
    u16 station, index;
    if (al == 0x0C) {
        station = 2;
        index = 0x0C;
    } else if (al == 0x0D) {
        station = 3;
        index = 0x0D;
    } else if (al == 0x0A) {
        station = 4;
        index = 0x0A;
    } else {
        return;
    }
    if (ds_u16(DS_station) != station) return;
    const u16 page = ds_u16(DS_draw_page);
    ds_u16(DS_draw_page) = 0;
    gun_panel_copy(condition(index));
    ds_u16(DS_draw_page) = page;
}

// 0919:2cad sinking_update (simulation.md §8.7, world group): a hull condition up to 2 raises the leak
// level to it. Once per 256 world passes (world_pass_counter 5Bh) a leak level of 2 or more (as a
// signed byte) adds leak - 1 to the water; at 10h or more (8-bit sum) the boat is lost, else "We're
// sinking!" (0Bh).
void sinking_update()
{
    u8 al = ds_u8(DS_hull_condition);
    if (al <= 2 && ds_u8(DS_leak_level) < al) ds_u8(DS_leak_level) = al;
    if (ds_u8(DS_world_pass_counter) != 0x5B) return;
    al = u8(ds_u8(DS_leak_level) - 1);
    if (s8(al) <= 0) return;
    al = u8(al + ds_u8(DS_water_level));
    if (al >= 0x10) {
        boat_destroyed();
        return;
    }
    ds_u8(DS_water_level) = al;
    show_message(0x0B);
}

// 0919:2ce3 window_hit (simulation.md §8.6): unless (DS:0089 & 18h), a crack bit 1 << (DS:0089 & 7)
// in the station's window_cracks byte, drawn on page 0 (draw_page restored after).
void window_hit()
{
    const u8 r = ds_u8(DS_rng_state + 1);
    if (r & 0x18) return;
    ds_u8(u16(DS_window_cracks + ds_u16(DS_station))) |= u8(1 << (r & 7));
    const u16 page = ds_u16(DS_draw_page);
    ds_u16(DS_draw_page) = 0;
    window_cracks_draw();
    ds_u16(DS_draw_page) = page;
}

// 0919:2d0f boat_destroyed (simulation.md §8.7): mission result 1; the chase view (if not on)
// looking back at the boat from distance 38h, the scene rebuilt; sound 7, the shake; speed,
// throttles and the crew throttle 0; the boat becomes a wreck (kind 30h) with eight fires and
// explosions around it; the main switch off, the panel redrawn; "We're goners!" (3); score word 0 = 1.
void boat_destroyed()
{
    ds_u8(DS_mission_result) = 1;
    if (ds_u8(DS_chase_view) != 1) {
        ds_u8(DS_chase_view) = 1;
        ds_u8(DS_chase_distance) = 0x38;
        ds_u8(DS_chase_heading) = u8(ds_u8(DS_view_heading) + 0x80);
        ds_u8(DS_scene_rebuild) = 1;
    }
    sfx_play(7);
    ds_u8(DS_screen_shake) = 3;
    ds_u8(DS_speed) = 0;
    ds_u8(DS_throttle) = 0;
    ds_u8(DS_throttle + 1) = 0;
    ds_u8(DS_crew_throttle_target) = 0;
    ds_u8(DS_object_word) = 0x30;
    place(0x0248, 0, 5);
    place(0x054B, 3, 3);
    place(0x084B, -3, 3);
    place(0x0248, 0, -5);
    place(0x054B, 3, -3);
    place(0x084B, -3, -3);
    place(0x0B48, -5, 0);
    place(0x0648, 5, 0);
    panel_redraw_all(switch_draw(0x0500));
    show_message(3);
    ds_u16(DS_score_words) = 1;
}

} // namespace gb
