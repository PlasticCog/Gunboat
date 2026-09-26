#pragma once
// Game flow (game_flow.md): start-up, title, HQ, roster, front end, debrief, and the file and memory
// helpers of segment 0000. One function per original function.
#include "mem.hpp"
#include "types.hpp"

namespace gb {

// ---- program (flow_main.cpp)
[[noreturn]] void game_main();           // 0000:0000 main
void config_load();                      // 0000:02fe
void config_print_mode(u16 n);           // 0000:05fc
void config_print_yn(u16 c);             // 0000:0620
[[noreturn]] void quit_to_dos();         // 0000:021e
[[noreturn]] void fatal_exit(s16 code);  // 0000:0276

// ---- files and memory (flow_files.cpp)
u16 name_hash_h1(u16 name_ds, u16 mult);        // 0000:0e62  (DX = 0)
u16 name_hash_h2(u16 name_ds, u16 unused);      // 0000:0eac  (DX = 0)
u32 name_hash(u16 name_ds);                     // 0000:0ef2  DX:AX
s16 archive_open(u16 name_ds);                  // 0000:0d74  a DOS handle at the entry, or 0
void file_load_near(u16 name_ds, u16 dst_ds);   // 0000:0648
void file_load_far(u16 name_ds, FarPtr dst);    // 0000:06b4
void mem_alloc_all();                           // 0000:0a4e
void mem_free_all();                            // 0000:0c7a

// ---- keys (flow_input.cpp)
void demo_next_key(u8 *key);    // 08e1:006e
void input_read_key(u16 *key);  // 0000:07a8
u16 wait_key(u16 n);            // 00f2:0e7a  the key, or n
void kbd_flush_key();           // 00f2:103e

// ---- screens (flow_screens.cpp)
u16 print_records(u16 base, u16 off);  // 00f2:0dbc
u16 print_text(u16 base, u16 off);     // 00f2:0e10
void print_chars(u16 base, u16 n);     // 00f2:0e44
void screen_present();                 // 00f2:0ece
u16 credits_text(u16 records);         // 00f2:0d3e
void pal_apply_vga();                  // 00f2:0eec
void pal_black_vga();                  // 00f2:0efa
void pal_fade_in_vga();                // 00f2:0f08
void pal_fade_out_vga();               // 00f2:0f16
void ega_pal_entry(u16 index, u16 value);  // 00f2:0f24
void ega_pal_apply();                  // 00f2:0f46
void ega_pal_init();                   // 00f2:0fea
void music_stop();                     // 00f2:119c
void music_start();                    // 00f2:1044

// ---- title (flow_title.cpp)
void menu_cursor_init();  // 020d:064a
u16 title_menu();         // 00f2:000e

// ---- headquarters (flow_hq.cpp), segment 020d
u16 hq_quiz();                                   // 020d:0008  0 right (always, in this GB.EXE)
u16 choice_menu(u16 timeout, u16 items, u16 deltas, u16 y0, u16 y1, u16 key_fe, u16 count,
                u16 key_fd);                     // 020d:0824  the index, FEh, FDh or FFh
void menu_cursor_move(u16 index, u16 items, u16 y0, u16 y1);  // 020d:09a0
void menu_cursor_draw(u16 y0, u16 y1);           // 020d:09cc
void menu_tick_mark(u16 x, u16 y, u16 y0, u16 y1);            // 020d:0a56
void roster_load();                              // 020d:0b28
void roster_save();                              // 020d:0bdc

// ---- the office (flow_office.cpp), segment 02d2
void office_face_draw();                         // 02d2:076c
void office_idle();                              // 02d2:07d2
void speech_clear();                             // 02d2:0994
void folder_draw(u16 c1, u16 c2);                // 02d2:09da
void folder_present();                           // 02d2:0bac
u16 wait_key_idle(u16 n);                        // 02d2:2c52  0 on a key, or n
void office_draw();                              // 02d2:2c9e
void office_restore();                           // 02d2:2df4
u16 bcd_inc(u16 x);                              // 02d2:2b4a
u16 bcd_add(u16 a, u16 b);                       // 02d2:2b72
u16 bcd_to_bin(u16 x);                           // 02d2:2bae
u16 bin_to_bcd(u16 x);                           // 02d2:2bea

// ---- the front end (flow_front.cpp), segment 02d2
void front_end();                                // 02d2:0008
u16 name_entry();                                // 02d2:0bd8
void roster_edit();                              // 02d2:106c
void roster_new_record(u16 slot);                // 02d2:123e
void personnel_files(u16 ret_state);             // 02d2:1306
void personnel_file_show(u16 slot);              // 02d2:13cc
void bcd_stats_print(u16 table, u16 cells);      // 02d2:1598
void byte_stats_print(u16 table, u16 cells);     // 02d2:1626  (no caller)
void pbr_specs(u16 ret_state);                   // 02d2:1712
void spec_sheet_draw(u16 ret_state);             // 02d2:1826
void mission_select();                           // 02d2:1b6e
void mission_folder_draw();                      // 02d2:1d04
void assignment_map(u16 ret_state);              // 02d2:1ec2
void map_draw(u16 ret_state);                    // 02d2:209a
void outfitting();                               // 02d2:22aa
u16 outfitting_draw();                           // 02d2:24ac  the pencil's y
void debrief();                                  // 02d2:273e
void roster_update();                            // 02d2:2a6c

} // namespace gb
