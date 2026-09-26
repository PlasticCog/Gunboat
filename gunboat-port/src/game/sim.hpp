#pragma once
// Simulation (simulation.md): one function per original function. Near assembly routines with
// register arguments take and return the registers their callers use.
#include "types.hpp"

namespace gb {

u16 vec_scale(u8 al);          // 0919:3706  returns AX
u16 heading_vector(u8 angle);  // 0919:7e5f  AL = angle, returns AX
void boat_move();              // 0919:7ee8

// ---- controls (sim_controls.cpp): key actions, aiming, throttles, headings, fire keys
void end_mission();                     // 0919:0906
void aim_stern(u8 cl, u16 bx);          // 0919:0a22  CL = held keys, BX = rate index
void aim_stern_left(u16 bx);            // 0919:0a5b
void aim_stern_right(u16 bx);           // 0919:0a83
void aim_midship(u8 cl, u16 bx);        // 0919:0aab
void aim_midship_left(u16 bx);          // 0919:0ae4
void aim_midship_right(u16 bx);         // 0919:0b0c
void aim_bow(u8 cl, u16 bx);            // 0919:0b34
void aim_bow_left(u16 bx);              // 0919:0b6d
void aim_bow_right(u16 bx);             // 0919:0b95
void throttle_step(u16 si, u8 cl);      // 0919:0c6a  SI = engine, CL = held keys
void fire_station4();                   // 0919:0cf4
void fire_station3();                   // 0919:0d45
void fire_bow();                        // 0919:0dbd
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
void projectile_launch(u16 bx, u16 cx);                     // 0919:0ee2  BX = weapon, CH:CL = heading
void move_toward(u16 bx, u16 cx, u16 dx);                   // 0919:1fc3  BX = object offset, CX/DX = target
u16 spawn_enemy_wake(u16 bx, u16 si);                       // 0919:31f1  returns BX
u16 line_of_sight(u16 si, u16 cx);                          // 0919:348e  SI = 2*entry; returns CX (CH = 1: blocked)
SlotBxCx projectile_alloc();                                // 0919:37af  BX = slot, CX = 8*slot
void projectile_aim(u16 ax, u16 bx, u16 cx, u16 si);        // 0919:37c7  AL elevation AH weapon BX slot SI 8*slot
u8 elevation_add_clamped(u8 al);                            // 0919:3b3b  returns AL
void score_add(u8 dl);                                      // 0919:3b9f  DL = byte offset into B52E
u8 hit_test(u16 bx);                                        // 0919:3bbc  BX = entry; returns AH (1 = hit)
void terrain_structure_break(u8 ah, u16 si);                // 0919:3c51  AH = old kind, SI = object offset
u16 free_temp_object();                                     // 0919:6f2a  returns BX
void muzzle_flash_tick();                                   // 0919:81f8

// ---- camera (sim_camera.cpp)
void camera_pitch_bob();  // 0919:80e0

} // namespace gb
