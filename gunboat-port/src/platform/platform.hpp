#pragma once
// Platform (platform.md): timers, input, files, memory, LZW, text and small helpers, and the MSC 5.1
// runtime functions the game calls. One function per original function; the host layer (host.hpp)
// stands in for the hardware and DOS.
//
// Conventions (PORTING.md): MSC `int` = s16. A near data pointer is a DGROUP offset (`u16 x_ds`), a
// far pointer a FarPtr. Runtime functions carry a crt_ prefix (crt_fopen for fopen at 15ee:0306)
// so that they do not collide with the C++ library.
#include "mem.hpp"
#include "types.hpp"

namespace gb {

// ---- helpers (platform.md §8)
u16 random();  // 0000:0780

// A NUL-terminated string in DGROUP.
const char *ds_str(u16 off);

// ---- DOS memory: INT 21h 48h / 49h on the MCB chain in mem[] (dos.cpp)
void dos_heap_init();                   // one free block from HEAP_BOTTOM to HEAP_TOP (at start-up)
u16 dos_alloc(u16 paragraphs, u16 *err);  // 48h: the segment, or 0 with *err = 7 (arena) / 8 (no memory)
u16 dos_free(u16 seg);                  // 49h: 0, or the DOS error 9

// ---- DOS files: INT 21h on host handles 5..19 (dos.cpp)
s16 dos_open(u16 name_ds, const char *mode, bool create);  // 3Dh / 3Ch: the handle, or -1
s16 dos_open_name(const char *name, const char *mode, bool create);
s32 dos_tell(s16 fh);                                     // position of an open handle, or -1
u32 dos_lseek(s16 fh, s32 offset, u8 whence);             // 42h: the new position, or FFFFFFFFh
u16 dos_read_handle(s16 fh, FarPtr buf, u16 n, bool *ok);  // 3Fh: AX (the bytes, or the error code)
u16 dos_write_handle(s16 fh, FarPtr buf, u16 n, bool *ok); // 40h
bool dos_close_handle(s16 fh);                            // 3Eh
void dos_close_all();

// The DOS file wrappers (platform.md §4)
s16 dos_seek(s16 fh, u16 lo, u16 hi);    // 121b:050a  1 ok, 0 error
s16 dos_open_read(u16 name_ds);          // 121b:0524  handle or -1
u16 dos_file_size(s16 fh);               // 121b:0536  low word; the file is rewound
u16 dos_read(FarPtr buf, u16 n, s16 fh); // 121b:055c  AX = bytes read or the DOS error
void dos_close(s16 fh);                  // 121b:0575

// ---- MSC 5.1 runtime (dos.cpp; PORT models of the runtime, see each function)
u16 crt_getstream();                                    // 15ee:16d0
u16 crt_fopen(u16 name_ds, u16 mode_ds);                // 15ee:0306  FILE* (DGROUP offset) or 0
u16 crt_fread(FarPtr buf, u16 size, u16 count, u16 f);  // 15ee:0332
u16 crt_fwrite(FarPtr buf, u16 size, u16 count, u16 f); // 15ee:0524
s16 crt_fclose(u16 f);                                  // 15ee:023e
FarPtr crt_fmalloc(u16 size);                           // 15ee:06c1
void crt_ffree(FarPtr p);                               // 15ee:06ac
[[noreturn]] void crt_exit(s16 code);                   // 15ee:01a0

// ---- LZW (08e1)
s16 lzw_alloc();  // 08e1:018e  DOS 48h 300h paragraphs -> DS:1078; 1 ok, 0 fail
void lzw_free();  // 08e1:01aa  DOS 49h on DS:1078

} // namespace gb
