// The VGA palette routines (video.md §3), segment 121b: the first 32 colours of palette_3d go to the
// DAC through the buffer dac_buffer (121b:000E) and INT 10h AX=1012h. Also the pictures' runs, the
// page 1 -> 0 dissolve of every mode (§4) and the Hercules set-up.
#include "platform/platform.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/card.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

void upload_dac_buffer() { bios_dac_set_block(0, 0x20, {CS_dac_buffer, seg_of(CSSEG_dac_buffer)}); }

// palette_3d scaled by level/16 with rounding: (c * level + 8) >> 4 (MUL DL, 8-bit operands).
void scale_into_buffer(u8 level)
{
    for (u16 i = 0; i < 0x60; i++)
        seg_u8(CSSEG_dac_buffer, u16(CS_dac_buffer + i)) =
            u8((u16(ds_u8(u16(DS_palette_3d + i)) * level) + 8) >> 4);
}

// One step of a fade: wait for the next timer tick (tick_counter changes), then for the vertical
// retrace. PORT: the original polls port 3DAh until the retrace bit is set; the host waits for the
// start of the next retrace.
void wait_tick_and_retrace(u16 start)
{
    while (u8(ds_u16(DS_tick_counter) - start) < 1) host_pump();
    host_wait_vretrace();
}

} // namespace

// 121b:07ae pal_fade_out: levels 15 down to 0, one per timer tick.
void pal_fade_out()
{
    for (s16 level = 15; level >= 0; level--) {
        const u16 start = ds_u16(DS_tick_counter);
        scale_into_buffer(u8(level));
        upload_dac_buffer();
        wait_tick_and_retrace(start);
    }
}

// 121b:0804 pal_fade_in: levels 1 up to 16 (16 = the palette itself), one per timer tick.
void pal_fade_in()
{
    for (u8 level = 1; level != 0x11; level++) {
        const u16 start = ds_u16(DS_tick_counter);
        scale_into_buffer(level);
        upload_dac_buffer();
        wait_tick_and_retrace(start);
    }
}

// 121b:085d pal_black
void pal_black()
{
    for (u16 i = 0; i < 0x60; i++) seg_u8(CSSEG_dac_buffer, u16(CS_dac_buffer + i)) = 0;
    upload_dac_buffer();
}

// 121b:087f pal_apply
void pal_apply()
{
    for (u16 i = 0; i < 0x60; i++)
        seg_u8(CSSEG_dac_buffer, u16(CS_dac_buffer + i)) = ds_u8(u16(DS_palette_3d + i));
    upload_dac_buffer();
}

namespace {

// SHR CL,1 / STOSB / REP STOSW: an odd count's byte first, then words. PORT: a word at offset
// FFFFh puts its second byte after the segment (as DOSBox does; an 8086 wraps, a 286 faults); only
// a malformed picture gets there.
void store_run(u16 seg, u16 &di, u16 count, u8 colour)
{
    if (count & 1) mem_u8(seg, di++) = colour;
    for (count >>= 1; count; count--, di = u16(di + 2)) mem_u16(seg, di) = u16(colour << 8 | colour);
}

} // namespace

// 121b:08a8 picture_draw_vga (platform.md §6): (colour, count) runs from DS:src into the draw page
// (page_segments[draw_page]), from the start of row y_bottom; a run that passes the right edge
// continues at the start of the row above.
void picture_draw_vga(u16 src_ds, u16 runs, u16 y_bottom)
{
    const u16 seg = ds_u16(u16(DS_page_segments + 2 * ds_u16(DS_draw_page)));
    u16 si = src_ds;
    u16 di = u16(u32(y_bottom) * 0x140);
    u16 x = 0;
    u16 n = runs;
    do {
        const u8 colour = ds_u8(si);
        u16 count = ds_u8(u16(si + 1));
        si = u16(si + 2);
        x = u16(x + count);
        if (x > 0x140) {
            x = u16(x - 0x140);
            store_run(seg, di, u16(count - x), colour);
            di = u16(di - 0x280);
            count = x;
        }
        store_run(seg, di, count, colour);
    } while (--n);
}

namespace {

u8 ror8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v >> n | v << (8 - n)) : v;
}

u8 rol8(u8 v, unsigned n) { return ror8(v, 8 - (n & 7)); }

// The pixel of a step in a 2- or 4-bit packed page: src's bits under `mask`, dst's others
// (121b:05e8, 121b:0772).
void copy_masked(u16 src, u16 dst, u16 di, u8 mask)
{
    vmem_write(dst, di, u8((vmem_read(src, di) & mask) | (vmem_read(dst, di) & u8(~mask))));
}

void wait_next_tick(u16 start)
{
    while (ds_u16(DS_tick_counter) == start) host_pump();
}

// 121b:059d: CGA (4 pixels a byte, two banks of 80-byte rows): the step's byte in each 8-pixel
// block (2 bytes) is dissolve_order >> 2, its pixel dissolve_order & 3 (dissolve_cga_masks); a
// dissolve_rows entry with bit 3 set (an odd row y * 40) is moved to the odd bank (+ 1FD8h).
void dissolve_cga()
{
    for (u16 step = 0; step < 0x40; step++) {
        const u8 al = ds_u8(u16(DS_dissolve_order + step));
        u16 di = ds_u16(u16(DS_dissolve_rows + 2 * (step & 7)));
        if (di & 8) di = u16(di + 0x1FD8);
        di = u16(di + (al >> 2));
        const u8 mask = ds_u8(u16(DS_dissolve_cga_masks + (al & 3)));
        const u16 src = ds_u16(u16(DS_page_segments + 2)), dst = ds_u16(DS_page_segments);
        const u16 start = ds_u16(DS_tick_counter);
        ds_u8(DS_scratch_b7e3) = 0x19;
        do {
            ds_u8(DS_scratch_b7e2) = 0x28;
            do {
                copy_masked(src, dst, di, mask);
                di = u16(di + 2);
            } while (--ds_u8(DS_scratch_b7e2));
            di = u16(di + 0xF0);
        } while (--ds_u8(DS_scratch_b7e3));
        wait_next_tick(start);
    }
}

// 121b:0717: Tandy (2 pixels a byte, four banks of 160-byte rows): the byte dissolve_order >> 1 of
// each 4-byte block, its pixel dissolve_order & 1 (dissolve_tandy_masks); the row y = dissolve_rows
// / 40 (DIV) goes to bank y & 3, one row group down for y >= 4.
void dissolve_tandy()
{
    for (u16 step = 0; step < 0x40; step++) {
        const u8 order = ds_u8(u16(DS_dissolve_order + step));
        u16 di = u16(order >> 1);
        const u8 y = u8(div16_8(ds_u16(u16(DS_dissolve_rows + 2 * (step & 7))), 0x28));
        if (y & 4) di = u16(di + 0xA0);
        di = u16(di + ((y & 3) << 13));
        const u8 mask = ds_u8(u16(DS_dissolve_tandy_masks + (order & 1)));
        const u16 src = ds_u16(u16(DS_page_segments + 2)), dst = ds_u16(DS_page_segments);
        const u16 start = ds_u16(DS_tick_counter);
        ds_u8(DS_scratch_b7e3) = 0x19;
        do {
            ds_u8(DS_scratch_b7e2) = 0x28;
            do {
                copy_masked(src, dst, di, mask);
                di = u16(di + 4);
            } while (--ds_u8(DS_scratch_b7e2));
            di = u16(di + 0xA0);
        } while (--ds_u8(DS_scratch_b7e3));
        wait_next_tick(start);
    }
}

// 121b:0682: EGA (planar, 40-byte rows; pages 0 and 1 in the card's memory): map mask 0Fh, write
// mode 0, set/reset on all planes, function replace; for each block the pixel dissolve_order (its
// bit dissolve_ega_masks[order]) is read from page 1 plane by plane (read map 3..0) into a colour,
// which becomes the set/reset value, its bit the bit mask, and a read-and-write (AND 8) of page 0
// stores it. No timer wait: the EGA dissolve runs as fast as the machine. The registers are left
// so (scratch_b7e2 is not used).
void dissolve_ega()
{
    card_out16(0x3C4, 0x0F02);
    card_out16(0x3CE, 0x0005);
    card_out16(0x3CE, 0x0F01);
    card_out16(0x3CE, 0x0003);
    const u16 dst = ds_u16(DS_page_segments);
    for (u16 step = 0; step < 0x40; step++) {
        const u8 cl = ds_u8(u16(DS_dissolve_order + step));
        u16 di = ds_u16(u16(DS_dissolve_rows + 2 * (step & 7)));
        const u8 ch = ds_u8(u16(DS_dissolve_ega_masks + cl));
        ds_u8(DS_scratch_b7e3) = 0x19;
        do {
            const u16 src = ds_u16(u16(DS_page_segments + 2));
            for (u8 bh = 0x28; bh; bh--) {
                card_out16(0x3CE, 0x0304);
                u8 bl = u8(vmem_read(src, di) & ch);
                card_out16(0x3CE, 0x0204);
                bl |= ror8(u8(vmem_read(src, di) & ch), 1);
                card_out16(0x3CE, 0x0104);
                bl |= ror8(u8(vmem_read(src, di) & ch), 2);
                card_out16(0x3CE, 0x0004);
                const u8 colour = rol8(u8(ror8(u8(vmem_read(src, di) & ch), 3) | bl), cl);
                card_out16(0x3CE, u16(colour << 8 | 0x00));
                card_out16(0x3CE, u16(ch << 8 | 0x08));
                vmem_write(dst, di, u8(vmem_read(dst, di) & 0x08));
                di++;
            }
            di = u16(di + 0x118);
        } while (--ds_u8(DS_scratch_b7e3));
    }
}

// 121b:0621: VGA (a byte a pixel, 320-byte rows): the pixel dissolve_order of the row
// dissolve_rows * 8.
void dissolve_vga()
{
    for (u16 step = 0; step < 0x40; step++) {
        const u16 col = ds_u8(u16(DS_dissolve_order + step));
        u16 di = u16((ds_u16(u16(DS_dissolve_rows + 2 * (step & 7))) << 3) + col);
        const u16 src = ds_u16(u16(DS_page_segments + 2)), dst = ds_u16(DS_page_segments);
        const u16 start = ds_u16(DS_tick_counter);
        ds_u8(DS_scratch_b7e3) = 0x19;
        do {
            ds_u8(DS_scratch_b7e2) = 0x28;
            do {
                mem_u8(dst, di) = mem_u8(src, di);
                di = u16(di + 8);
            } while (--ds_u8(DS_scratch_b7e2));
            di = u16(di + 0x8C0);
        } while (--ds_u8(DS_scratch_b7e3));
        wait_next_tick(start);
    }
}

} // namespace

// 121b:0581 dissolve_page1_to_0 (video.md §4): page 1 (page_segments[1]) onto page 0
// (page_segments[0]) in 64 steps; each step copies one pixel of every 8x8 block (25 rows of 40
// blocks), the pixel at column dissolve_order[step] of row dissolve_rows[step & 7] / 40 in its
// block, and (except on the EGA) waits for the next timer tick. By the game's mode (EED2's low
// byte): 4 and 0Ch CGA, 0Dh EGA, above 0Dh VGA, the others (9) Tandy.
void dissolve_page1_to_0()
{
    const u8 mode = u8(ds_u16(DS_video_mode));
    if (mode == 4) dissolve_cga();
    else if (mode == 0x0D) dissolve_ega();
    else if (mode > 0x0D) dissolve_vga();
    else if (mode == 0x0C) dissolve_cga();
    else dissolve_tandy();
}

// 121b:0902 hercules_setup (game_flow.md §2): the Hercules card in graphics mode showing page 1
// (B800h): configuration 3 (graphics and page 1 allowed), mode 0 (video off), page 1 cleared (4000h
// words), the 12 CRTC registers from the table at DS:DA2D (40 characters of 2 bytes a row, 100 rows
// of 3 scan lines: the CGA-format picture's two banks and a blank third one), then mode 8Ah
// (graphics, video on, page 1). ES and DI are kept.
void hercules_setup()
{
    card_out(0x3BF, 3);
    card_out(0x3B8, 0);
    for (u32 i = 0; i < 0x4000; i++) mem_u16(0xB800, u16(2 * i)) = 0;
    for (u16 si = 0; si < 12; si++) {
        card_out(0x3B4, u8(si));
        card_out(0x3B5, ds_u8(u16(0xDA2D + si)));
    }
    card_out(0x3B8, 0x8A);
}

} // namespace gb
