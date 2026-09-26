#pragma once
// VGA hardware model for mode 13h (video.md §5): the 320x200 byte screen lives in mem[] at
// A000:0000 (page 0); the DAC holds 256 6-bit RGB entries; the CRTC start address shifts the shown
// picture. The model composes the frame the host presents. Port code writes the DAC and CRTC through
// these functions where the original writes ports 3C8h/3C9h and 3D4h/3D5h or calls INT 10h AX=1010h/
// 1012h. Converted to C++ from the Test Drive III port's platform/vga.c (MIT; THIRD_PARTY.md).
#include "types.hpp"

namespace gb {

void vga_init();  // installs the frame source; DAC black, start 0

void vga_dac_write(u8 index, u8 r, u8 g, u8 b);  // 6-bit components, as port 3C9h
void vga_dac_read(u8 index, u8 *r, u8 *g, u8 *b);

// CRTC start address in bytes (mode 13h: register 0Ch/0Dh value x 4). The screen shows 64000
// bytes from there, wrapping within the 64 KB plane. Gunboat never changes it in VGA (video.md §1).
void vga_set_start(u16 byte_offset);
u16 vga_start();

} // namespace gb
