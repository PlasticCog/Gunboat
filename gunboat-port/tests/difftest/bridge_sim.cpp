// Bridge entries: simulation (simulation.md).
#include "bridge.hpp"
#include "game/sim.hpp"

using namespace gb;

BRIDGE(vec_scale) { r.ax = vec_scale(u8(r.ax)); }
BRIDGE(heading_vector) { r.ax = heading_vector(u8(r.ax)); }
BRIDGE(boat_move) { boat_move(); }

// ---- package S1: controls, messages, projectiles, hits, time of day, camera bob (simulation.md)
#include "sound/sound.hpp"

BRIDGE(end_mission) { end_mission(); }
BRIDGE(aim_stern) { r.ax = aim_stern(u8(r.cx), r.bx, r.ax); }
BRIDGE(aim_stern_left) { r.ax = aim_stern_left(r.bx); }
BRIDGE(aim_stern_right) { r.ax = aim_stern_right(r.bx); }
BRIDGE(aim_midship) { r.ax = aim_midship(u8(r.cx), r.bx, r.ax); }
BRIDGE(aim_midship_left) { r.ax = aim_midship_left(r.bx); }
BRIDGE(aim_midship_right) { r.ax = aim_midship_right(r.bx); }
BRIDGE(aim_bow) { r.ax = aim_bow(u8(r.cx), r.bx, r.ax); }
BRIDGE(aim_bow_left) { r.ax = aim_bow_left(r.bx); }
BRIDGE(aim_bow_right) { r.ax = aim_bow_right(r.bx); }
BRIDGE(throttle_step) { throttle_step(r.si, u8(r.cx)); }
BRIDGE(fire_station4) { r.ax = fire_station4(r.ax); }
BRIDGE(fire_station3) { r.ax = fire_station3(r.ax); }
BRIDGE(fire_bow) { r.ax = fire_bow(r.ax); }
BRIDGE(pilot_slow_down) { pilot_slow_down(); }
BRIDGE(rotate_headings_minus) { rotate_headings_minus(r.bx); }
BRIDGE(rotate_headings_plus) { rotate_headings_plus(r.bx); }
BRIDGE(projectile_launch) { r.ax = projectile_launch(r.bx, r.cx); }
BRIDGE(heading_step_plus) { r.ax = heading_step_plus(r.ax, r.bx); }
BRIDGE(heading_step_minus) { r.ax = heading_step_minus(r.ax, r.bx); }
BRIDGE(accelerate_speed) { accelerate_speed(u8(r.cx >> 8)); }
BRIDGE(evade_incoming) { evade_incoming(); }

BRIDGE(print_colon) { print_colon(); }
BRIDGE(show_message_page0) { show_message_page0(u8(r.ax)); }
BRIDGE(show_message_far) { show_message_far(a[0]); }
BRIDGE(show_message) { show_message(u8(r.ax)); }
BRIDGE(print_bcd_2digits) { print_bcd_2digits(u8(r.ax)); }
BRIDGE(print_3digits) { print_3digits(u8(r.ax), false); }  // the harness enters with CF = 0
BRIDGE(print_digits_hundreds) { print_digits_hundreds(u8(r.ax), false); }
BRIDGE(print_digits_tens) { print_digits_tens(u8(r.ax), u8(r.cx)); }
// test_sim.py enters the original at 0919:1729 (inside 0919:16e3: shr ax,7; CF = AH != 0; call
// print_3digits; ret) to run print_3digits with the carry set; the bridge does the same shift.
BRIDGE(print_3digits_carry)
{
    const u16 v = u16(r.ax >> 7);
    print_3digits(u8(v), (v >> 8) != 0);
}

BRIDGE(time_of_day) { time_of_day(); }

BRIDGE(move_toward) { move_toward(r.bx, r.cx, r.dx); }
BRIDGE(spawn_enemy_wake) { r.bx = spawn_enemy_wake(r.bx, r.si); }
BRIDGE(line_of_sight) { r.cx = line_of_sight(r.si, r.cx); }
BRIDGE(projectile_alloc)
{
    const SlotBxCx s = projectile_alloc();
    r.bx = s.bx;
    r.cx = s.cx;
}
BRIDGE(projectile_aim) { r.ax = projectile_aim(r.ax, r.bx, r.cx, r.si); }
BRIDGE(elevation_add_clamped) { r.ax = u16((r.ax & 0xFF00) | elevation_add_clamped(u8(r.ax))); }
BRIDGE(score_add) { score_add(u8(r.dx)); }
BRIDGE(hit_test) { r.ax = u16((r.ax & 0x00FF) | hit_test(r.bx) << 8); }
BRIDGE(terrain_structure_break) { terrain_structure_break(u8(r.ax >> 8), r.si); }
BRIDGE(free_temp_object) { r.bx = free_temp_object(); }
BRIDGE(muzzle_flash_tick) { muzzle_flash_tick(); }

BRIDGE(camera_pitch_bob) { camera_pitch_bob(); }
BRIDGE(engine_sound_update) { engine_sound_update(); }

// ---- package S2a: keys, controls, messages, crew, clock, engines, damage, enemies, impacts
BRIDGE(key_dispatch) { r.si = key_dispatch(r.si); }
BRIDGE(key_default) { key_default(); }
BRIDGE(panel_switch_toggle) { r.ax = panel_switch_toggle(r.bx); }
BRIDGE(key_f10_fire_at_will) { key_f10_fire_at_will(); }
BRIDGE(key_f9_identify) { r.si = key_f9_identify(r.si); }
BRIDGE(key_minus_control_rate) { key_minus_control_rate(); }
BRIDGE(key_f1_panel) { r.si = key_f1_panel(r.si); }
BRIDGE(key_f3_panel) { key_f3_panel(); }
BRIDGE(key_f2_panel) { key_f2_panel(); }
BRIDGE(engine_switch) { r.ax = engine_switch(r.si); }
BRIDGE(key_f5_branch_left) { key_f5_branch_left(); }
BRIDGE(key_f6_branch_right) { key_f6_branch_right(); }
BRIDGE(key_f8_faster) { key_f8_faster(); }
BRIDGE(key_f7_slower) { key_f7_slower(); }
BRIDGE(pilot_command_reply) { pilot_command_reply(); }
BRIDGE(key_m_map) { key_m_map(); }
BRIDGE(key_period_assignment) { key_period_assignment(); }
BRIDGE(key_z_pilot_left) { key_z_pilot_left(); }
BRIDGE(key_x_pilot_ahead) { key_x_pilot_ahead(); }
BRIDGE(key_c_pilot_right) { key_c_pilot_right(); }
BRIDGE(key_pilot_station) { key_pilot_station(r.bx); }
BRIDGE(station_dead_reply) { station_dead_reply(); }
BRIDGE(key_v_bow) { key_v_bow(); }
BRIDGE(key_n_midship) { key_n_midship(); }
BRIDGE(key_b_stern) { key_b_stern(); }
BRIDGE(key_slash_damage_report) { key_slash_damage_report(); }
BRIDGE(key_plus_time_compression) { key_plus_time_compression(); }
BRIDGE(key_tab_return_to_base) { key_tab_return_to_base(); }
BRIDGE(key_comma_chase_view) { key_comma_chase_view(); }
BRIDGE(key_d_detail) { key_d_detail(); }

BRIDGE(controls_poll) { controls_poll(); }
BRIDGE(pilot_throttle_controls) { pilot_throttle_controls(u8(r.cx), r.bx); }

BRIDGE(message_sequencer) { message_sequencer(); }
BRIDGE(heading_readout) { heading_readout(r.ax); }
BRIDGE(message_line_draw) { message_line_draw(); }

namespace {
void put(Regs &r, AxSi v)
{
    r.ax = v.ax;
    r.si = v.si;
}
} // namespace
BRIDGE(crew_gunners) { put(r, crew_gunners(r.si)); }
BRIDGE(gunner_bow) { put(r, gunner_bow(r.ax, r.si)); }
BRIDGE(gunner_stern) { put(r, gunner_stern(r.ax, r.si)); }
BRIDGE(gunner_midship) { put(r, gunner_midship(r.ax, r.si)); }
BRIDGE(gunner_aim) { put(r, gunner_aim(r.ax, r.si)); }
BRIDGE(gunner_key) { r.ax = gunner_key(u8(r.bx), u8(r.cx), r.ax); }
BRIDGE(reload_tick) { reload_tick(); }

BRIDGE(mission_clock_tick) { mission_clock_tick(); }

BRIDGE(propulsion) { propulsion(); }
BRIDGE(engine_thrust)
{
    const CxDx t = engine_thrust(r.si);
    r.cx = t.cx;
    r.dx = t.dx;
}

BRIDGE(boat_hit) { boat_hit(u8(r.ax)); }
BRIDGE(boat_hit_component) { boat_hit_component(u8(r.ax), u8(r.cx)); }
BRIDGE(damage_panel_refresh) { damage_panel_refresh(u8(r.ax)); }
BRIDGE(sinking_update) { sinking_update(); }
BRIDGE(window_hit) { window_hit(); }
BRIDGE(boat_destroyed) { boat_destroyed(); }

BRIDGE(mission_stop) { r.si = mission_stop(r.si); }
BRIDGE(missile_update) { r.si = missile_update(r.si); }
BRIDGE(enemy_update) { enemy_update(r.si); }
BRIDGE(schedule_shot) { schedule_shot(u8(r.ax), r.si); }

BRIDGE(projectile_tick) { projectile_tick(r.si); }
BRIDGE(projectile_impact) { projectile_impact(r.bx, r.si); }
BRIDGE(mark_near_objects) { mark_near_objects(r.si); }
BRIDGE(hit_objects) { hit_objects(); }
BRIDGE(mission_target_check) { r.ax = u16((r.ax & 0xFF00) | mission_target_check(r.si)); }
