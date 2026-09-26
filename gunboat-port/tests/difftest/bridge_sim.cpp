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
BRIDGE(aim_stern) { aim_stern(u8(r.cx), r.bx); }
BRIDGE(aim_stern_left) { aim_stern_left(r.bx); }
BRIDGE(aim_stern_right) { aim_stern_right(r.bx); }
BRIDGE(aim_midship) { aim_midship(u8(r.cx), r.bx); }
BRIDGE(aim_midship_left) { aim_midship_left(r.bx); }
BRIDGE(aim_midship_right) { aim_midship_right(r.bx); }
BRIDGE(aim_bow) { aim_bow(u8(r.cx), r.bx); }
BRIDGE(aim_bow_left) { aim_bow_left(r.bx); }
BRIDGE(aim_bow_right) { aim_bow_right(r.bx); }
BRIDGE(throttle_step) { throttle_step(r.si, u8(r.cx)); }
BRIDGE(fire_station4) { fire_station4(); }
BRIDGE(fire_station3) { fire_station3(); }
BRIDGE(fire_bow) { fire_bow(); }
BRIDGE(pilot_slow_down) { pilot_slow_down(); }
BRIDGE(rotate_headings_minus) { rotate_headings_minus(r.bx); }
BRIDGE(rotate_headings_plus) { rotate_headings_plus(r.bx); }
BRIDGE(projectile_launch) { projectile_launch(r.bx, r.cx); }
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
BRIDGE(projectile_aim) { projectile_aim(r.ax, r.bx, r.cx, r.si); }
BRIDGE(elevation_add_clamped) { r.ax = u16((r.ax & 0xFF00) | elevation_add_clamped(u8(r.ax))); }
BRIDGE(score_add) { score_add(u8(r.dx)); }
BRIDGE(hit_test) { r.ax = u16((r.ax & 0x00FF) | hit_test(r.bx) << 8); }
BRIDGE(terrain_structure_break) { terrain_structure_break(u8(r.ax >> 8), r.si); }
BRIDGE(free_temp_object) { r.bx = free_temp_object(); }
BRIDGE(muzzle_flash_tick) { muzzle_flash_tick(); }

BRIDGE(camera_pitch_bob) { camera_pitch_bob(); }
BRIDGE(engine_sound_update) { engine_sound_update(); }
