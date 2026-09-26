// The VGA palette routines (video.md §3), segment 121b: the first 32 colours of palette_3d go to the
// DAC through the buffer dac_buffer (121b:000E) and INT 10h AX=1012h.
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

// 121b:0581 dissolve_page1_to_0 (video.md §4): page 1 (page_segments[1]) onto page 0 in 64 steps;
// each step copies one pixel of every 8x8 block and waits for the next timer tick. PORT: only the
// VGA path (EED2 above 0Dh); the CGA/Hercules, EGA and Tandy paths are not ported.
void dissolve_page1_to_0()
{
    if (u8(ds_u16(DS_video_mode)) <= 0x0D) return;  // PORT: other adapters
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
        while (ds_u16(DS_tick_counter) == start) host_pump();
    }
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
