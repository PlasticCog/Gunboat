#pragma once
// The video card of the emulated machine for the modes other than VGA's 13h (video.md §6): an EGA,
// a CGA, a Tandy 1000 or a Hercules card, chosen by the player (the machine GB.EXE runs on). It is
// hardware, not game memory, so it lives here rather than in mem[] (like the VGA DAC in vga.cpp):
//
//  * EGA (mode 0Dh): four 64 KB planes the CPU reaches at A000h only through the sequencer (map
//    mask) and the graphics controller (set/reset, rotate and function, read map, write and read
//    modes, bit mask, latches); the attribute controller maps the 4-bit pixels to colours.
//  * CGA (mode 04h): B800h in mem[] (plain memory), the mode control and colour select registers.
//  * Tandy 1000 (mode 09h): B800h in mem[], the video gate array's palette registers, the page
//    register.
//  * Hercules: B000h/B800h in mem[] shown as a monochrome bitmap by the Hercules CRTC, mode and
//    configuration registers (GB.EXE draws in CGA format and programs the CRTC to show it).
//
// Ported code reaches the card where the original executes OUT/IN on its ports (card_out,
// card_in) and where it reads or writes EGA memory (ega_read, ega_write). The differential tests run
// the same model for the original code (tests/difftest/cardmodel.py); keep the two in step.
#include "types.hpp"

namespace gb {

enum class Machine : u8 { Vga, Ega, Cga, Tandy, Hercules };

// The whole card state, flat (the tests copy it to and from the Python model): the EGA planes and
// the registers of every card.
struct CardState {
    u8 planes[4][0x10000];
    u8 latch[4];
    u8 seq_index, seq[5];            // sequencer 3C4h/3C5h: 2 = map mask
    u8 gc_index, gc[9];              // graphics controller 3CEh/3CFh
    u8 attr_flip, attr_index, attr[0x15];  // attribute controller 3C0h (flip 0: an index comes next)
    u8 crtc_index, crtc[0x19];       // colour CRTC 3D4h/3D5h (EGA, CGA, Tandy)
    u8 cga_mode, cga_colour;         // 3D8h mode control, 3D9h colour select
    u8 tandy_index, tandy[0x20];     // Tandy video gate array: 3DAh index, 3DEh data (10h-1Fh palette)
    u8 tandy_page;                   // 3DFh: CRT and CPU pages
    u8 herc_index, herc[0x12];       // Hercules CRTC 3B4h/3B5h
    u8 herc_mode, herc_config;       // 3B8h mode control, 3BFh configuration
    u8 status;                       // the vertical retrace toggle of the status ports (3DAh, 3BAh)
};

void card_set_machine(Machine m);
Machine card_machine();
void card_reset();  // power-on state (planes zero, registers as the BIOS leaves them before a mode)
CardState &card();

// OUT / IN on a video port. Ports the model does not know are accepted (out) or read as FFh (in).
void card_out(u16 port, u8 value);
u8 card_in(u16 port);
// OUT DX, AX: AL to the port, AH to the next one (an index and its data).
inline void card_out16(u16 port, u16 ax)
{
    card_out(port, u8(ax));
    card_out(u16(port + 1), u8(ax >> 8));
}

// The CPU's access to EGA memory at A000:off (write modes 0-2, read modes 0-1, latches).
u8 ega_read(u16 off);
void ega_write(u16 off, u8 value);

// A byte at seg:off as the CPU of this machine reaches it: on an EGA machine the A000:0000-FFFF
// window is the card's memory (ega_read/ega_write), everything else is mem[] (RAM pages, CGA,
// Tandy and Hercules memory). Code of the other video modes that reads or writes a page whose
// segment may be the screen's uses these instead of mem_u8.
u8 vmem_read(u16 seg, u16 off);
void vmem_write(u16 seg, u16 off, u8 value);

// The BIOS's part: the registers and memory a mode set leaves (INT 10h AH=00h on this card), and
// the palette services (AH=0Bh CGA palette, AX=1000h/1001h/1002h EGA/Tandy palette registers).
void card_bios_set_mode(u8 mode, bool clear);
void card_bios_cga_palette(u8 bh, u8 bl);
void card_bios_palette_reg(u8 index, u8 value);
void card_bios_overscan(u8 value);

// The picture the card shows now as XRGB8888; returns false when the card shows nothing of its own
// (VGA: vga.cpp composes). w x h up to 720 x 348.
bool card_compose(u32 *xrgb, int *w, int *h);

} // namespace gb
