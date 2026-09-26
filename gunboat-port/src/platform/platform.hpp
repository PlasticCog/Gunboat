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
u16 random();                // 0000:0780
u16 world_a_base();          // 121b:036e  6E54h
u16 pit_random(u16 base);    // 121b:0372

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
u16 crt_fread(u16 buf_ds, u16 size, u16 count, u16 f);  // 15ee:0332
u16 crt_fwrite(u16 buf_ds, u16 size, u16 count, u16 f); // 15ee:0524
s16 crt_fclose(u16 f);                                  // 15ee:023e
FarPtr crt_fmalloc(u16 size);                           // 15ee:06c1
void crt_ffree(FarPtr p);                               // 15ee:06ac
u16 crt_strcpy(u16 dst_ds, u16 src_ds);                // 15ee:0786  dst
s16 crt_strncmp(u16 s1_ds, u16 s2_ds, u16 n);           // 15ee:07d4
[[noreturn]] void crt_exit(s16 code);                   // 15ee:01a0

// ---- LZW (08e1)
s16 lzw_alloc();  // 08e1:018e  DOS 48h 300h paragraphs -> DS:1078; 1 ok, 0 fail
void lzw_free();  // 08e1:01aa  DOS 49h on DS:1078
void lzw_decode_picture(FarPtr src, FarPtr dst);  // 08e1:01bd
void lzw_decode_body(FarPtr src, FarPtr dst);     // 08e1:01db
u16 crack_table_entry(u16 index);                 // 08e1:016f  (a table lookup, not LZW)

// ---- BIOS model (bios.cpp): INT 10h video and INT 1Ah on the BIOS data area in mem[]
void bios_init();                                        // as DOS leaves it: text mode 3
void bios_set_mode(u8 al);                               // INT 10h AH=00h
u16 bios_get_mode(u8 *bh);                               // INT 10h AH=0Fh: AX
u16 bios_get_cursor(u8 page);                            // INT 10h AH=03h: DX
u16 bios_display_combination();                          // INT 10h AX=1A00h: BX
void bios_dac_set(u16 index, u8 r, u8 g, u8 b);          // INT 10h AX=1010h
void bios_dac_set_block(u16 first, u16 count, FarPtr table);  // INT 10h AX=1012h
u32 bios_ticks();                                        // INT 1Ah AH=00h
void bios_tick();                                        // the BIOS timer interrupt's count

// ---- timers and joystick (timer.cpp, joystick.cpp)
u16 bios_wait_ticks(s16 n);                          // 15d4:0007  returns 0
void timer_install();                                // 121b:0c9a
void timer_restore();                                // 121b:0cc4
void menu_timer_isr();                               // 121b:0ce2
void timer_interrupt();                              // PORT: the host's timer interrupt
void run_int8_handler(FarPtr vector);                // PORT: the INT 8 handler at `vector`
u16 joystick_axis(u16 stick);                        // 146a:000b
u16 joystick_axis_y(u16 stick);                      // 15ea:0003
u16 joystick_button(u16 stick);                      // 15d7:000d
void joystick_read(u16 stick, u8 *code, u8 *dir);    // 1473:0008

// ---- keyboard (kbd.cpp), segment 121b
void kbd_install();        // 121b:0a4d
void kbd_restore();        // 121b:0a88
void kbd_isr(u8 sc);       // 121b:0a9c  (sc = the port 60h byte)
bool kbd_isr_installed();  // PORT: INT 9 points at kbd_isr
void kbd_byte(u8 b);       // PORT: the keyboard controller (host keyboard bytes)
void kbd_focus_lost();     // PORT: releases the shift bits

// ---- text and pictures (text.cpp, pal.cpp), segment 121b
FarPtr far_normalize(FarPtr p);             // 121b:0380
void text_set_colours(s16 fg, s16 bg);      // 121b:0397
void text_goto_cell(s16 row, s16 col);      // 121b:03b0
void text_goto(s16 y, s16 col);             // 121b:03c7
void text_draw_char(const u8 *c);           // 121b:03d8  (&ds_u8(off) for a DGROUP character)
void pal_fade_out();                        // 121b:07ae
void pal_fade_in();                         // 121b:0804
void pal_black();                           // 121b:085d
void pal_apply();                           // 121b:087f
void picture_draw_vga(u16 src_ds, u16 runs, u16 y_bottom);  // 121b:08a8
void dissolve_page1_to_0();                 // 121b:0581

} // namespace gb
