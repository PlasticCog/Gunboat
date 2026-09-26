// The graphics library's display start (video.md §1): gfx_set_display_offset, mode 13h path.
#include "platform/gfx.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/vga.hpp"
#include "symbols.hpp"

namespace gb {

// 149f:0004 gfx_set_display_offset (video.md §1, render3d.md §7.2): the display starts at (x, y)
// of the screen page. Mode 13h (dispatch 149f:007c): CRTC start = y * 80 + x / 4, stored in the
// BIOS page offset 0040:004E too, written after a vertical retrace; returns 0. The text modes and
// modes 8/0Ah only return 0 (dispatch 149f:00c9). PORT: the CRTC is the VGA model's (the original
// writes registers 0Ch/0Dh at the BIOS's CRTC port 0040:0063 after waiting for the retrace to
// start and to end: host_wait_vretrace); the EGA, CGA, Tandy and Hercules paths are parked.
u16 gfx_set_display_offset(s16 x, s16 y)
{
    switch (ds_u16(DS_gfx_mode_x2)) {
    case 0x00: case 0x02: case 0x04: case 0x06: case 0x0E: case 0x10: case 0x14:
        return 0;
    case 0x26: {
        const u16 start = u16(u16(u16(y) * 0x50) + (u16(x) >> 2));
        mem_u16(0x0040, 0x004E) = start;
        host_wait_vretrace();
        vga_set_start(u16(start << 2));
        return 0;
    }
    default:
        host_fatal("gfx_set_display_offset: library mode %u is not ported (EGA/Tandy/CGA parked)",
                   unsigned(ds_u16(DS_gfx_mode_x2) >> 1));
    }
}

} // namespace gb
