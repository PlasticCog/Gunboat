// The graphics library's palette routines (video.md §2): the colour patterns of the fill routines
// (ega_pal_set) and the palette registers of the CGA, the Tandy 1000, the EGA and the VGA DAC
// (ega_pal_register, gfx_set_ega_palette, gfx_set_pal_reg), each by the library's mode. The BIOS
// services go through the BIOS model (bios.cpp), the ports through the card (card.hpp).
#include "platform/gfx.hpp"

#include "mem.hpp"
#include "platform/card.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u8 rol8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v << n | v >> (8 - n)) : v;
}

// The three 2-bit levels (primary bit | secondary bit << 1) of an EGA colour rgbRGB, as the
// mode 13h paths take them apart (147c:008c, 148c:0059): red bits 2 and 5, green 1 and 4, blue 0
// and 3.
struct Levels {
    u8 r, g, b;
};

Levels ega_levels(u8 v)
{
    return {u8((v >> 2 & 1) | (v >> 4 & 2)), u8((v >> 1 & 1) | (v >> 3 & 2)), u8((v & 1) | (v >> 2 & 2))};
}

u8 level_rgb(u8 level) { return ds_u8(u16(DS_ega_level_rgb + level)); }

} // namespace

// 14ae:0005 ega_pal_set (video.md §2): the fill pattern pair of colour index & 1Fh
// (colour_patterns, used by picture_hline) by the library's mode (jump table 14ae:0055):
//  * CGA 4-6, Tandy 9, Hercules 0Bh/0Ch (0025): the value's low byte for even rows, that byte
//    rotated left by the high byte for odd rows;
//  * EGA 0Dh-10h, 12h (0030): the low nibble and the high nibble (two colours);
//  * 11h (0042): bits 0 and 7;
//  * the text modes, 7, 8, 0Ah and 13h (0050): nothing.
// The caller gets AX = 0.
void ega_pal_set(u16 index, u16 value)
{
    const u16 di = u16(DS_colour_patterns + ((index & 0x1F) << 1));
    const u8 al = u8(value), ah = u8(value >> 8);
    switch (ds_u16(DS_gfx_mode_x2) >> 1) {
    case 4: case 5: case 6: case 9: case 0x0B: case 0x0C:  // 0025
        ds_u8(di) = al;
        ds_u8(u16(di + 1)) = rol8(al, ah);
        break;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x12:  // 0030
        ds_u8(di) = al & 0x0F;
        ds_u8(u16(di + 1)) = u8(al >> 4);
        break;
    case 0x11:  // 0042
        ds_u8(di) = al & 1;
        ds_u8(u16(di + 1)) = u8(al >> 7);
        break;
    default:  // 0050
        break;
    }
}

// 147c:000f ega_pal_register (video.md §2): one palette register by the library's mode (jump table
// 147c:00e5); the caller gets AX = 0.
//  * CGA 4/5 (0025): reg (0..5) picks a colour set from three tables: the background INT 10h
//    AH=0Bh BH=0 with cga_background_bits[reg] | value, the palette AH=0Bh BH=1 with
//    cga_palette_select[reg], then the mode register: the BIOS's copy 0040:0065 without bit 2, with
//    cga_mode_bits[reg], written to port 3D8h and back to 0040:0065;
//  * CGA 6 (0063): the colour INT 10h AH=0Bh BH=0 = value;
//  * Tandy 8-0Ah (006d): the video gate array's palette register 10h + reg = value (IN 3DAh first);
//  * EGA 0Dh-12h (0080): INT 10h AX=1000h, register reg = value;
//  * 13h (008c): DAC register reg = the EGA colour value rgbRGB as 6-bit levels (INT 10h AX=1010h).
void ega_pal_register(u16 reg, u16 value)
{
    switch (ds_u16(DS_gfx_mode_x2) >> 1) {
    case 4: case 5: {  // 0025
        bios_cga_palette(0, u8(ds_u8(u16(DS_cga_background_bits + reg)) | u8(value)));
        bios_cga_palette(1, ds_u8(u16(DS_cga_palette_select + reg)));
        const u8 al = u8((mem_u8(0, 0x0465) & 0xFB) | ds_u8(u16(DS_cga_mode_bits + reg)));
        card_out(0x3D8, al);
        mem_u8(0, 0x0465) = al;
        break;
    }
    case 6:  // 0063
        bios_cga_palette(0, u8(value));
        break;
    case 8: case 9: case 0x0A:  // 006d
        card_in(0x3DA);
        card_out(0x3DA, u8(u8(reg) | 0x10));
        card_out(0x3DE, u8(value));
        break;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12:  // 0080: BH = value, BL = reg
        bios_palette_reg(u8(reg), u8(value));
        break;
    case 0x13: {  // 008c: DH red, CH green, CL blue, BX = reg
        const Levels l = ega_levels(u8(value));
        bios_dac_set(reg, level_rgb(l.r), level_rgb(l.g), level_rgb(l.b));
        break;
    }
    default:  // 00df
        break;
    }
}

// 148c:000d gfx_set_ega_palette (video.md §2): the 16 palette registers from the low bytes of 16
// words at DS:table by the library's mode (jump table 148c:00cb); the caller gets AX = 0.
//  * Tandy 8-0Ah (001e): each through the gate array (IN 3DAh, OUT 3DAh 10h + i, OUT 3DEh);
//  * EGA 0Dh-12h (003d): copied to ega_palette_regs (code-segment data, whose 17th byte, the
//    overscan, is never written), then INT 10h AX=1002h;
//  * 13h (0059): the EGA colours as 6-bit levels into ega_palette_rgb, then DAC 0..15 (INT 10h
//    AX=1012h);
//  * the others (00c5): nothing.
void gfx_set_ega_palette(u16 table_ds)
{
    switch (ds_u16(DS_gfx_mode_x2) >> 1) {
    case 8: case 9: case 0x0A: {  // 001e
        u16 si = table_ds;
        for (u16 cx = 0; cx < 0x10; cx++) {
            card_in(0x3DA);
            card_out(0x3DA, u8(cx | 0x10));
            card_out(0x3DE, ds_u8(si));
            si = u16(si + 2);
        }
        break;
    }
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12:  // 003d
        for (u16 i = 0; i < 0x10; i++)
            seg_u8(CSSEG_ega_palette_regs, u16(CS_ega_palette_regs + i)) = ds_u8(u16(table_ds + 2 * i));
        bios_palette_all({CS_ega_palette_regs, seg_of(CSSEG_ega_palette_regs)});
        break;
    case 0x13: {  // 0059
        u16 di = CS_ega_palette_rgb;
        for (u16 i = 0; i < 0x10; i++) {
            const Levels l = ega_levels(ds_u8(u16(table_ds + 2 * i)));
            seg_u8(CSSEG_ega_palette_rgb, di++) = level_rgb(l.r);
            seg_u8(CSSEG_ega_palette_rgb, di++) = level_rgb(l.g);
            seg_u8(CSSEG_ega_palette_rgb, di++) = level_rgb(l.b);
        }
        bios_dac_set_block(0, 0x10, {CS_ega_palette_rgb, seg_of(CSSEG_ega_palette_rgb)});
        break;
    }
    default:  // 00c5
        break;
    }
}

// 157d:0078 gfx_set_pal_reg (video.md §2): one palette register from the colour components r, g,
// b by the library's mode; the caller would get AX = 0. Dead code: GB.EXE never calls it, and its
// jump table (CS:00BC + mode * 2) is addressed for CS = 1584h, i.e. called as 1584:0008 (the table
// 157d:012c); as 157d:0078 it would jump through code bytes. Ported as called at 1584:0008.
//  * Tandy 8-0Ah (0094): bit 0 of r, g, b as RGB (bits 2, 1, 0), a negative index sets the
//    intensity (bit 3) and is negated; the gate array's register 10h + index (IN 3DAh first);
//  * EGA 0Dh/0Eh (00c1): the same with the intensity in bit 4, INT 10h AX=1000h;
//  * EGA 0Fh/10h (00df): 2-bit levels as rgbRGB (r bits 0/1 -> 5/2, g -> 4/1, b -> 3/0), AX=1000h;
//  * 11h-13h (011c): the DAC register index = (r, g, b) (INT 10h AX=1010h);
//  * the others (0126): nothing.
void gfx_set_pal_reg(s16 index, u16 r, u16 g, u16 b)
{
    u16 bx = u16(index);
    const u16 mode = u16(ds_u16(DS_gfx_mode_x2) >> 1);
    switch (mode) {
    case 8: case 9: case 0x0A:
    case 0x0D: case 0x0E: {  // 0094, 00c1
        u8 cl = u8((b & 1) | (g & 1) << 1 | (r & 1) << 2);
        const bool tandy = mode <= 0x0A;
        if (s16(bx) < 0) {
            cl |= tandy ? 0x08 : 0x10;
            bx = u16(-s16(bx));
        }
        if (tandy) {
            card_in(0x3DA);
            card_out(0x3DA, u8(u8(bx) | 0x10));
            card_out(0x3DE, cl);
        } else {
            bios_palette_reg(u8(bx), cl);
        }
        break;
    }
    case 0x0F: case 0x10: {  // 00df
        const u8 cl = u8((r & 1) << 5 | (r & 2) << 1 | (g & 1) << 4 | (g & 2) | (b & 1) << 3 | (b & 2) >> 1);
        bios_palette_reg(u8(bx), cl);
        break;
    }
    case 0x11: case 0x12: case 0x13:  // 011c: DH = r, CH = g, CL = b
        bios_dac_set(bx, u8(r), u8(g), u8(b));
        break;
    default:  // 0126
        break;
    }
}

} // namespace gb
