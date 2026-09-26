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

} // namespace gb
