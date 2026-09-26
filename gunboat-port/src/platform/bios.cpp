// The BIOS services the game calls (INT 10h video, INT 1Ah clock), modelled on the BIOS data area in
// mem[] (segment 0040h) as a VGA BIOS keeps it. PORT: a model of the machine, not a translation:
// only what GB.EXE uses and what can be observed in memory. The Unicorn tests run the same model
// (tests/difftest/biosmodel.py); keep the two in step.
#include "platform/platform.hpp"

#include <cstring>

#include "mem.hpp"
#include "platform/vga.hpp"

namespace gb {

namespace {

constexpr u16 BDA = 0x0040;
constexpr u16 BDA_MODE = 0x49, BDA_COLS = 0x4A, BDA_PAGE_SIZE = 0x4C, BDA_PAGE_START = 0x4E, BDA_CURSOR = 0x50,
              BDA_CURSOR_SHAPE = 0x60, BDA_PAGE = 0x62, BDA_TICKS = 0x6C;

// Columns and page size by mode 0..13h (0 = no such mode).
constexpr u8 mode_cols[0x14] = {40, 40, 80, 80, 40, 40, 80, 80, 0, 0, 0, 0, 0, 40, 80, 80, 80, 80, 80, 40};
constexpr u16 mode_page_size[0x14] = {0x0800, 0x0800, 0x1000, 0x1000, 0x4000, 0x4000, 0x4000, 0x1000, 0, 0,
                                      0,      0,      0,      0x2000, 0x4000, 0x8000, 0x8000, 0xA000, 0xA000, 0xFA00};

void fill_words(u16 seg, u16 words, u16 value)
{
    for (u32 i = 0; i < words; i++) mem_u16(seg, u16(2 * i)) = value;
}

} // namespace

void bios_init()
{
    std::memset(mp(BDA, 0x49), 0, 0x6C + 5 - 0x49);
    mem_u8(BDA, BDA_MODE) = 3;
    mem_u16(BDA, BDA_COLS) = 80;
    mem_u16(BDA, BDA_PAGE_SIZE) = 0x1000;
    mem_u16(BDA, BDA_CURSOR_SHAPE) = 0x0607;
    // the BIOS timer and keyboard handlers (the IBM PC entry points)
    mem_u16(0, 8 * 4) = 0xFEA5;
    mem_u16(0, 8 * 4 + 2) = 0xF000;
    mem_u16(0, 9 * 4) = 0xE987;
    mem_u16(0, 9 * 4 + 2) = 0xF000;
}

// INT 10h AH=00h: the mode number; the screen is cleared unless bit 7 is set (text modes with blank
// characters, graphics modes with zeros); cursors and the active page go to 0. PORT: the DAC keeps
// its colours, except mode 13h, where the BIOS loads its default palette; the model loads black (the
// game uploads its colours before it shows anything), as the Test Drive III port does.
void bios_set_mode(u8 al)
{
    const u8 mode = al & 0x7F;
    const bool clear = !(al & 0x80);
    mem_u8(BDA, BDA_MODE) = mode;
    if (mode < 0x14 && mode_cols[mode]) {
        mem_u16(BDA, BDA_COLS) = mode_cols[mode];
        mem_u16(BDA, BDA_PAGE_SIZE) = mode_page_size[mode];
    }
    mem_u16(BDA, BDA_PAGE_START) = 0;
    for (u16 i = 0; i < 8; i++) mem_u16(BDA, u16(BDA_CURSOR + 2 * i)) = 0;
    mem_u8(BDA, BDA_PAGE) = 0;
    if (clear) {
        if (mode <= 3) fill_words(0xB800, 0x2000, 0x0720);
        else if (mode == 7) fill_words(0xB000, 0x2000, 0x0720);
        else if (mode <= 6) fill_words(0xB800, 0x2000, 0);
        else if (mode >= 0x0D && mode <= 0x13) fill_words(0xA000, 0x8000, 0);
    }
    if (mode == 0x13) {
        for (int i = 0; i < 256; i++) vga_dac_write(u8(i), 0, 0, 0);
        vga_set_start(0);
    }
}

// INT 10h AH=0Fh: AX = columns << 8 | mode; *bh = the active page.
u16 bios_get_mode(u8 *bh)
{
    if (bh) *bh = mem_u8(BDA, BDA_PAGE);
    return u16(mem_u8(BDA, BDA_COLS) << 8 | mem_u8(BDA, BDA_MODE));
}

// INT 10h AH=03h: DX = row << 8 | column of the page's cursor (the shape goes to CX, unused).
u16 bios_get_cursor(u8 page) { return mem_u16(BDA, u16(BDA_CURSOR + 2 * (page & 7))); }

// INT 10h AX=1A00h: the display combination: AL = 1Ah (supported), BX = 0008h (VGA colour).
u16 bios_display_combination() { return 0x0008; }

// INT 10h AX=1010h / 1012h: DAC registers.
void bios_dac_set(u16 index, u8 r, u8 g, u8 b) { vga_dac_write(u8(index), r, g, b); }
void bios_dac_set_block(u16 first, u16 count, FarPtr table)
{
    for (u16 i = 0; i < count; i++) {
        const FarPtr p = far_add(table, u16(3 * i));
        vga_dac_write(u8(first + i), far_u8(p, 0), far_u8(p, 1), far_u8(p, 2));
    }
}

// INT 1Ah AH=00h: the timer tick count (0040:006C), advanced by the BIOS timer interrupt.
u32 bios_ticks() { return mem_u32(BDA, BDA_TICKS); }

// The BIOS timer interrupt's count (the game's timer handlers chain to it every few interrupts).
void bios_tick()
{
    u32 t = mem_u32(BDA, BDA_TICKS) + 1;
    if (t >= 0x1800B0) {  // midnight
        t = 0;
        mem_u8(BDA, 0x70) = 1;
    }
    mem_u32(BDA, BDA_TICKS) = t;
}

} // namespace gb
