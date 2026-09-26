// The VGA palette routines (video.md §3), segment 121b: the first 32 colours of palette_3d go to the
// DAC through the buffer dac_buffer (121b:000E) and INT 10h AX=1012h.
#include "platform/platform.hpp"

#include "host.hpp"
#include "mem.hpp"
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

} // namespace gb
