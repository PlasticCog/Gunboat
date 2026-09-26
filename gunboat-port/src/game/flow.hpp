#pragma once
// Game flow (game_flow.md): start-up, title, HQ, roster, front end, debrief, and the file and memory
// helpers of segment 0000. One function per original function.
#include "mem.hpp"
#include "types.hpp"

namespace gb {

// ---- program (flow_main.cpp)
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

} // namespace gb
