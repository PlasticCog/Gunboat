#pragma once
// Simulation (simulation.md): one function per original function. Near assembly routines with
// register arguments take and return the registers their callers use.
#include "types.hpp"

namespace gb {

u16 vec_scale(u8 al);          // 0919:3706  returns AX
u16 heading_vector(u8 angle);  // 0919:7e5f  AL = angle, returns AX
u16 boat_move(u16 si);         // 0919:7ee8  returns SI
u16 boat_motion(u16 si);       // 0919:7ebb  returns SI (mission_stop)

// ---- controls (sim_controls.cpp): key actions, aiming, throttles, headings, fire keys
void end_mission();                     // 0919:0906
u16 aim_stern(u8 cl, u16 bx, u16 ax);  // 0919:0a22  CL = held keys, BX = rate index; AX in/out
u16 aim_stern_left(u16 bx);             // 0919:0a5b  AX out
u16 aim_stern_right(u16 bx);            // 0919:0a83  AX out
u16 aim_midship(u8 cl, u16 bx, u16 ax); // 0919:0aab  AX in/out
u16 aim_midship_left(u16 bx);           // 0919:0ae4  AX out
u16 aim_midship_right(u16 bx);          // 0919:0b0c  AX out
u16 aim_bow(u8 cl, u16 bx, u16 ax);     // 0919:0b34  AX in/out
u16 aim_bow_left(u16 bx);               // 0919:0b6d  AX out
u16 aim_bow_right(u16 bx);              // 0919:0b95  AX out
void throttle_step(u16 si, u8 cl);      // 0919:0c6a  SI = engine, CL = held keys
u16 fire_station4(u16 ax);              // 0919:0cf4  AX in/out
u16 fire_station3(u16 ax);              // 0919:0d45  AX in/out
u16 fire_bow(u16 ax);                   // 0919:0dbd  AX in/out
void pilot_slow_down();                 // 0919:0e19
void rotate_headings_minus(u16 bx);     // 0919:0e48  BX = step index
void rotate_headings_plus(u16 bx);      // 0919:0e95
u16 heading_step_plus(u16 ax, u16 bx);  // 0919:0f0b  AL = fraction, AH = heading; returns AX
u16 heading_step_minus(u16 ax, u16 bx); // 0919:0f42
void accelerate_speed(u8 ch);           // 0919:29f9  CH = target speed
void evade_incoming();                  // 0919:3479

// ---- messages (sim_messages.cpp)
void print_colon();                          // 0919:1558
void show_message_page0(u8 al);              // 0919:1565
void show_message_far(u16 message);          // 0919:1589  far C
void show_message(u8 al);                    // 0919:1594
void print_bcd_2digits(u8 al);               // 0919:174e
void print_3digits(u8 al, bool cf);          // 0919:1769  value CF*256 + AL
void print_digits_hundreds(u8 al, bool cf);  // 0919:176e
void print_digits_tens(u8 al, u8 cl);        // 0919:1799  CL = hundreds already shown

// ---- mission clock (sim_clock.cpp)
void time_of_day();  // 0919:1dd6

// ---- weapons, projectiles, hits, objects (sim_weapons.cpp)
struct SlotBxCx {
    u16 bx, cx;
};
u16 projectile_launch(u16 bx, u16 cx);                      // 0919:0ee2  BX = weapon, CH:CL = heading; AX out
void move_toward(u16 bx, u16 cx, u16 dx);                   // 0919:1fc3  BX = object offset, CX/DX = target
u16 spawn_enemy_wake(u16 bx, u16 si);                       // 0919:31f1  returns BX
u16 line_of_sight(u16 si, u16 cx);                          // 0919:348e  SI = 2*entry; returns CX (CH = 1: blocked)
SlotBxCx projectile_alloc();                                // 0919:37af  BX = slot, CX = 8*slot
u16 projectile_aim(u16 ax, u16 bx, u16 cx, u16 si);         // 0919:37c7  AL elevation AH weapon BX slot SI 8*slot
                                                            //            (AX out)
u8 elevation_add_clamped(u8 al);                            // 0919:3b3b  returns AL
void score_add(u8 dl);                                      // 0919:3b9f  DL = byte offset into B52E
u8 hit_test(u16 bx);                                        // 0919:3bbc  BX = entry; returns AH (1 = hit)
void terrain_structure_break(u8 ah, u16 si);                // 0919:3c51  AH = old kind, SI = object offset
u16 free_temp_object();                                     // 0919:6f2a  returns BX
void muzzle_flash_tick();                                   // 0919:81f8

// ---- camera (sim_camera.cpp)
void camera_pitch_bob();  // 0919:80e0

// ==== package S2a: the simulation's middle layer
// Routines that call the cockpit's near assembly routines (indicator_draw, switch_draw,
// panel_redraw_all) return the AX they leave, because their callers pass it on (hud.hpp).

// ---- keys (sim_keys.cpp): key_dispatch and the key handlers, simulation.md §3.2-§3.3
u16 key_dispatch(u16 si);               // 0919:038e  far; returns SI (F1, F9 change it)
void key_default();                     // 0919:03c7
u16 panel_switch_toggle(u16 bx);        // 0919:03cb  BX = entry offset in panel_switch_table; AX out
void key_f10_fire_at_will();            // 0919:043c
u16 key_f9_identify(u16 si);            // 0919:0465  returns SI
void key_minus_control_rate();          // 0919:0503
u16 key_f1_panel(u16 si);               // 0919:0520  returns SI
void key_f3_panel();                    // 0919:056b
void key_f2_panel();                    // 0919:05ae
u16 engine_switch(u16 si);              // 0919:0608  SI = engine; AX out
void key_f5_branch_left();              // 0919:0649
void key_f6_branch_right();             // 0919:0658
void key_f8_faster();                   // 0919:06a9
void key_f7_slower();                   // 0919:06ee
void pilot_command_reply();             // 0919:0713
void key_f4_reverse_course();           // 0919:0667
void key_m_map();                       // 0919:074a
void key_period_assignment();           // 0919:0756
void key_z_pilot_left();                // 0919:0762
void key_x_pilot_ahead();               // 0919:0769
void key_c_pilot_right();               // 0919:0770
void key_pilot_station(u16 bx);         // 0919:0777  BX = look direction
void station_dead_reply();              // 0919:07b3
void key_v_bow();                       // 0919:07c0
void key_n_midship();                   // 0919:07f6
void key_b_stern();                     // 0919:0833
void key_slash_damage_report();         // 0919:0871
void key_plus_time_compression();       // 0919:087d
void key_tab_return_to_base();          // 0919:08be
void key_comma_chase_view();            // 0919:090d
void key_d_detail();                    // 0919:0930

// ---- controls (sim_controls.cpp)
void controls_poll();                          // 0919:0953
void pilot_throttle_controls(u8 cl, u16 bx);   // 0919:0bbd  CL = held keys, BX = rate index

// ---- messages (sim_messages.cpp)
void message_sequencer();         // 0919:1528
void heading_readout(u16 ax);     // 0919:16e3  AL = heading, AH = fraction
void message_line_draw();         // 0919:17d4

// ---- crew gunners (sim_crew.cpp). They return AX and SI as the original leaves them: game_frame
// passes AX on to jet_marker, throttle_needles and panel_blink (whose lamps store AH in
// scratch_b7e3), and SI on to projectile_tick (caller_si).
struct AxSi {
    u16 ax, si;
};
AxSi crew_gunners(u16 si);                // 0919:18af  AX = fire_at_will, then the gunners'
AxSi gunner_bow(u16 ax, u16 si);          // 0919:18c7
AxSi gunner_stern(u16 ax, u16 si);        // 0919:190d
AxSi gunner_midship(u16 ax, u16 si);      // 0919:1953
AxSi gunner_aim(u16 ax, u16 si);          // 0919:19a0  gun in gauge_row, scratch_b7e2/b7e3/b7e9, scratch_b7dc
u16 gunner_key(u8 bl, u8 cl, u16 ax);     // 0919:1aa3  BL = keys, CL = rate index; AX in/out
void reload_tick();               // 0919:1cf9

// ---- mission clock (sim_clock.cpp)
void mission_clock_tick();        // 0919:1d30

// ---- engines and propulsion (sim_engines.cpp)
void propulsion();                // 0919:2273
CxDx engine_thrust(u16 si);       // 0919:2529  SI = engine; CX = forward thrust, DX = turning

// ---- damage and destruction (sim_damage.cpp)
void boat_hit(u8 al);                          // 0919:2a37  AL = weapon class
void boat_hit_component(u8 al, u8 cl);         // 0919:2a69  AL = component, CL = message
void damage_panel_refresh(u8 al);              // 0919:2bbd  AL = condition index
void sinking_update();                         // 0919:2cad
void window_hit();                             // 0919:2ce3
void boat_destroyed();                         // 0919:2d0f

// ---- enemies, missile and passengers (sim_enemies.cpp)
u16 mission_stop(u16 si);                      // 0919:1e87  returns SI
void incoming_fire(u16 si);                    // 0919:32fb  SI kept (saved in caller_si)
u16 missile_update(u16 si);                    // 0919:2038  returns SI (the source entry once found)
void enemy_update(u16 si);                     // 0919:2ea8  SI = the caller's (saved in caller_si)
void schedule_shot(u8 al, u16 si);             // 0919:3217  AL = behaviour byte, SI = 2*entry

// ---- projectile impacts (sim_weapons.cpp)
void projectile_tick(u16 si);                  // 0919:38cc  SI = the caller's
void projectile_impact(u16 bx, u16 si);        // 0919:38ed  BX = slot
void mark_near_objects(u16 si);                // 0919:3948
void hit_objects();                            // 0919:3977
u8 mission_target_check(u16 si);               // 0919:3b51  SI = object offset; returns AL = message

// ---- the crew pilot and the river routes (sim_routes.cpp)
struct RoutePoint {
    u16 ax, cx;  // the waypoint (world X, Y), or AX = 2 * tile and CX unchanged when there is none
    u16 dx;      // DH = its link (rotated with the tile), FFh when there is none; DL = 28h - 4 * row
    u16 si;      // where the record read stopped (unused by the callers)
};
RoutePoint route_point(u16 cx, u16 bx);     // 0919:8754  CL = index, BX = grid cell
u16 route_advance(u16 dx, u16 cx, u16 bx);  // 0919:87e8  DH = link, CL = index, BX = cell; returns DX
void route_find();                          // 0919:1c00
void crew_pilot_decide();                   // 0919:1b2d
void crew_pilot();                          // 0919:1ac0

} // namespace gb
