// The video card of the emulated machine (card.hpp): EGA, CGA, Tandy 1000 and Hercules.
#include "platform/card.hpp"

#include <algorithm>
#include <cstring>

#include "mem.hpp"

namespace gb {

namespace {

Machine machine = Machine::Vga;
CardState state;

constexpr u16 BDA = 0x0040;

// The 16 colours of a CGA/EGA (200-line) monitor: RGB and intensity, colour 6 brown.
constexpr u32 CGA_RGB[16] = {0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                             0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
constexpr u32 MONO_ON = 0xE8E8E8;  // a Hercules pixel that is on

u8 ror8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v >> n | v << (8 - n)) : v;
}

// An EGA attribute value in a 200-line mode: the monitor takes bits 0-2 as RGB and bit 4 as
// intensity (the EGA's secondary green pin becomes the CGA intensity).
u32 ega200(u8 attr) { return CGA_RGB[(attr & 7) | ((attr & 0x10) ? 8 : 0)]; }

} // namespace

void card_set_machine(Machine m) { machine = m; }
Machine card_machine() { return machine; }
CardState &card() { return state; }

void card_reset()
{
    std::memset(&state, 0, sizeof state);
    state.seq[2] = 0x0F;
    state.gc[7] = 0x0F;
    state.gc[8] = 0xFF;
    state.attr[0x12] = 0x0F;
}

void card_out(u16 port, u8 value)
{
    CardState &c = state;
    switch (port) {
    case 0x3C4: c.seq_index = value & 7; break;
    case 0x3C5: if (c.seq_index < 5) c.seq[c.seq_index] = value; break;
    case 0x3CE: c.gc_index = value & 0x0F; break;
    case 0x3CF: if (c.gc_index < 9) c.gc[c.gc_index] = value; break;
    case 0x3C0:
        if (!c.attr_flip) {
            c.attr_index = value & 0x3F;
        } else if ((c.attr_index & 0x1F) < 0x15) {
            c.attr[c.attr_index & 0x1F] = value;
        }
        c.attr_flip ^= 1;
        break;
    case 0x3D4: c.crtc_index = value & 0x1F; break;
    case 0x3D5: if (c.crtc_index < 0x19) c.crtc[c.crtc_index] = value; break;
    case 0x3D8: c.cga_mode = value; break;
    case 0x3D9: c.cga_colour = value; break;
    case 0x3DA: c.tandy_index = value & 0x1F; break;  // Tandy: the gate array's register (EGA: feature control)
    case 0x3DE: c.tandy[c.tandy_index & 0x1F] = value; break;
    case 0x3DF: c.tandy_page = value; break;
    case 0x3B4: c.herc_index = value & 0x1F; break;
    case 0x3B5: if (c.herc_index < 0x12) c.herc[c.herc_index] = value; break;
    case 0x3B8: c.herc_mode = value; break;
    case 0x3BF: c.herc_config = value; break;
    default: break;
    }
}

u8 card_in(u16 port)
{
    CardState &c = state;
    switch (port) {
    case 0x3DA:  // input status: the retrace bit alternates on each read, starting in the retrace
        c.attr_flip = 0;
        c.status ^= 1;
        return c.status ? 0x08 : 0x00;
    case 0x3BA:  // Hercules status: bit 7 (vertical sync) and bit 3 (the video dots) alternate the same way
        c.status ^= 1;
        return c.status ? 0x88 : 0x00;
    default: return 0xFF;
    }
}

u8 ega_read(u16 off)
{
    CardState &c = state;
    for (int p = 0; p < 4; p++) c.latch[p] = c.planes[p][off];
    if (c.gc[5] & 0x08) {  // read mode 1: the pixels that equal the colour compare (don't care planes)
        u8 r = 0xFF;
        for (int p = 0; p < 4; p++)
            if (c.gc[7] & (1 << p)) r &= u8(~(c.latch[p] ^ ((c.gc[2] & (1 << p)) ? 0xFF : 0x00)));
        return r;
    }
    return c.latch[c.gc[4] & 3];
}

void ega_write(u16 off, u8 value)
{
    CardState &c = state;
    const u8 mode = c.gc[5] & 3, function = (c.gc[3] >> 3) & 3, mask = c.gc[8];
    for (int p = 0; p < 4; p++) {
        if (!(c.seq[2] & (1 << p))) continue;
        if (mode == 1) {  // the latches
            c.planes[p][off] = c.latch[p];
            continue;
        }
        u8 d;
        if (mode == 2) d = (value & (1 << p)) ? 0xFF : 0x00;
        else if (c.gc[1] & (1 << p)) d = (c.gc[0] & (1 << p)) ? 0xFF : 0x00;  // set/reset
        else d = ror8(value, c.gc[3] & 7);
        switch (function) {
        case 1: d &= c.latch[p]; break;
        case 2: d |= c.latch[p]; break;
        case 3: d ^= c.latch[p]; break;
        default: break;
        }
        c.planes[p][off] = u8((d & mask) | (c.latch[p] & ~mask));
    }
}

u8 vmem_read(u16 seg, u16 off)
{
    const u32 a = lin(seg, off);
    if (machine == Machine::Ega && a >= 0xA0000 && a < 0xB0000) return ega_read(u16(a - 0xA0000));
    return mem[a];
}

void vmem_write(u16 seg, u16 off, u8 value)
{
    const u32 a = lin(seg, off);
    if (machine == Machine::Ega && a >= 0xA0000 && a < 0xB0000) ega_write(u16(a - 0xA0000), value);
    else mem[a] = value;
}

void card_bios_set_mode(u8 mode, bool clear)
{
    CardState &c = state;
    switch (machine) {
    case Machine::Ega:
        if (mode == 0x0D || mode == 0x0E) {
            c.seq[2] = 0x0F;
            c.seq[4] = 0x06;
            std::memset(c.gc, 0, sizeof c.gc);
            c.gc[6] = 0x05;
            c.gc[7] = 0x0F;
            c.gc[8] = 0xFF;
            for (int i = 0; i < 16; i++) c.attr[i] = u8(i < 8 ? i : 0x10 + (i - 8));
            c.attr[0x10] = 0x01;
            c.attr[0x11] = 0x00;
            c.attr[0x12] = 0x0F;
            c.attr[0x13] = 0x00;
            c.attr_flip = 0;
            c.crtc[0x0C] = c.crtc[0x0D] = 0;
            c.crtc[0x13] = mode == 0x0D ? 0x14 : 0x28;
            if (clear) std::memset(c.planes, 0, sizeof c.planes);
            std::memset(c.latch, 0, sizeof c.latch);
        }
        break;
    case Machine::Cga:
    case Machine::Hercules:
        if (mode >= 4 && mode <= 6) {
            c.cga_mode = mode == 6 ? 0x1E : mode == 5 ? 0x0E : 0x0A;
            c.cga_colour = mode == 6 ? 0x3F : 0x30;
            c.crtc[0x0C] = c.crtc[0x0D] = 0;
        }
        break;
    case Machine::Tandy:
        if (mode >= 8 && mode <= 0x0A) {
            c.cga_mode = mode == 9 ? 0x0B : 0x0A;
            for (int i = 0; i < 16; i++) c.tandy[0x10 + i] = u8(i);
            c.tandy[3] = mode == 9 ? 0x10 : 0x00;
            c.tandy_page = 0xF6;  // (32 KB video modes: the top pages of video memory)
            c.crtc[0x0C] = c.crtc[0x0D] = 0;
        } else if (mode >= 4 && mode <= 6) {
            c.cga_mode = mode == 6 ? 0x1E : 0x0A;
            c.cga_colour = 0x30;
        }
        break;
    default: break;
    }
}

void card_bios_cga_palette(u8 bh, u8 bl)
{
    CardState &c = state;
    if (bh == 0) c.cga_colour = u8((c.cga_colour & 0xE0) | (bl & 0x1F));  // background / border
    else c.cga_colour = u8((c.cga_colour & 0xDF) | ((bl & 1) << 5));     // palette 0 / 1
}

void card_bios_palette_reg(u8 index, u8 value)
{
    CardState &c = state;
    if (machine == Machine::Tandy) {
        if (index < 16) c.tandy[0x10 + index] = value & 0x0F;
    } else if (index < 16) {
        c.attr[index] = value;
    }
}

void card_bios_overscan(u8 value)
{
    if (machine == Machine::Tandy) state.tandy[2] = value & 0x0F;
    else state.attr[0x11] = value;
}

bool card_compose(u32 *xrgb, int *w, int *h)
{
    const CardState &c = state;
    const u8 bios_mode = mem_u8(BDA, 0x49);
    *w = 320;
    *h = 200;
    switch (machine) {
    case Machine::Vga: return false;
    case Machine::Ega: {
        if (bios_mode != 0x0D) break;
        const u16 start = u16(c.crtc[0x0C] << 8 | c.crtc[0x0D]);
        const u16 pitch = u16(c.crtc[0x13] * 2);
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                const u16 off = u16(start + y * pitch + (x >> 3));
                const int bit = 7 - (x & 7);
                u8 idx = 0;
                for (int p = 0; p < 4; p++) idx |= u8(((c.planes[p][off] >> bit) & 1) << p);
                xrgb[y * 320 + x] = ega200(c.attr[idx & c.attr[0x12] & 0x0F]);
            }
        return true;
    }
    case Machine::Cga: {
        if (bios_mode < 4 || bios_mode > 5) break;
        const u16 start = u16((c.crtc[0x0C] << 8 | c.crtc[0x0D]) * 2);
        const u8 bright = (c.cga_colour & 0x10) ? 8 : 0;
        const bool pal1 = (c.cga_colour & 0x20) || (c.cga_mode & 0x04);
        const u8 colours[4] = {u8(c.cga_colour & 0x0F), u8((pal1 ? 3 : 2) | bright), u8((pal1 ? 5 : 4) | bright),
                               u8((pal1 ? 7 : 6) | bright)};
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                const u16 off = u16(start + (y & 1) * 0x2000 + (y >> 1) * 80 + (x >> 2));
                const u8 v = u8(mem_u8(0xB800, u16(off & 0x3FFF)) >> (6 - 2 * (x & 3)) & 3);
                xrgb[y * 320 + x] = CGA_RGB[colours[v]];
            }
        return true;
    }
    case Machine::Tandy: {
        if (bios_mode != 9) break;
        const u16 start = u16((c.crtc[0x0C] << 8 | c.crtc[0x0D]) * 2);
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                const u16 off = u16(start + (y & 3) * 0x2000 + (y >> 2) * 160 + (x >> 1));
                const u8 v = u8(mem_u8(0xB800, u16(off & 0x7FFF)) >> ((x & 1) ? 0 : 4) & 0x0F);
                xrgb[y * 320 + x] = CGA_RGB[c.tandy[0x10 + v] & 0x0F];
            }
        return true;
    }
    case Machine::Hercules: {
        // Graphics mode (allowed by configuration bit 0) with the video enabled (mode bit 3; page 1
        // at B800h if configuration bit 1 allows it): a character clock shows two bytes (16
        // pixels); scan line RA of character row r reads byte ((RA & 3) << 13) | ((MA & 0FFFh) << 1)
        // | b, MA = start (R12/R13) + r * R1 + character (the 6845's MA0-MA11 and RA0-RA1 as the
        // card wires them). hercules_setup's CRTC: R1 = 28h (640 pixels), R6 = 64h character rows of
        // R9 + 1 = 3 scan lines (300 lines: the CGA memory's even and odd banks, then bank 2 at
        // B800:4000, which it clears and nothing draws); the start moves with the display offset.
        const bool graphics = (c.herc_mode & 0x02) && (c.herc_config & 0x01);
        if (!graphics || !(c.herc_mode & 0x08)) break;  // text mode or video off: nothing of the game
        const u16 seg = ((c.herc_mode & 0x80) && (c.herc_config & 0x02)) ? 0xB800 : 0xB000;
        const int chars = std::clamp(int(c.herc[1]), 1, 45);
        const int lines = int(c.herc[9] & 0x1F) + 1;
        const int rows = std::clamp(int(c.herc[6] & 0x7F) * lines, 1, 348);
        const u16 start = u16((c.herc[12] & 0x3F) << 8 | c.herc[13]);
        *w = chars * 16;
        *h = rows;
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < *w; x++) {
                const u16 ma = u16(start + (y / lines) * chars + (x >> 4));
                const u16 off = u16(((y % lines) & 3) << 13 | (ma & 0x0FFF) << 1 | ((x >> 3) & 1));
                xrgb[y * *w + x] = (mem_u8(seg, off) >> (7 - (x & 7)) & 1) ? MONO_ON : 0;
            }
        return true;
    }
    }
    std::fill(xrgb, xrgb + 320 * 200, 0u);
    return true;
}

} // namespace gb
