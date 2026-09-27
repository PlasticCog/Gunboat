// The graphics library's display start (video.md §1): gfx_set_display_offset.
#include "platform/gfx.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/card.hpp"
#include "platform/vga.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// IN from the status port (the CRTC's port + 6: 3DAh, or 3BAh on a Hercules card) until bit 3
// (the vertical retrace; the video dots on a Hercules) is `set`. The card model's status bits
// alternate on each read (card.cpp), so a wait ends at its first or second read; each read still
// reaches the card (IN 3DAh resets the attribute controller's flip-flop). PORT: no host_pump per
// read: the wait is bounded by the model, and the time of a real retrace is the caller's
// host_wait_vretrace (a pump here would be a timer tick the original does not get there).
void wait_status(u16 port, bool set)
{
    while (bool(card_in(port) & 8) != set) {
    }
}

} // namespace

// 149f:0004 gfx_set_display_offset (video.md §1, render3d.md §7.2): the display starts at (x, y) of
// the screen page. The start address by the library's mode (jump table 149f:00cd), in the CRTC's
// units:
//  * CGA 4-6 (001a): (y >> 1) * 40 + (x >> 3) words (two banks of 100 rows of 40 words);
//  * Tandy 9 (002d): (y >> 2) * 80 + (x >> 2) (four banks of 50 rows of 80 words);
//  * 0Bh (0048) and 0Ch (0040, x doubled and y * 1.5 first): ((y >> 2) * 45) + (x >> 3) (Hercules:
//    four banks of 90-byte rows);
//  * EGA 0Dh-12h (005d): y * row_bytes + (x >> 3) + visible page * page_bytes (the product's low
//    word), and the pel panning x & 7;
//  * 13h (007c): y * 80 + (x >> 2);
//  * the text modes and 7, 8, 0Ah (00c9): nothing.
// Then (0089) the start goes to the BIOS's page offset 0040:004E and, after a vertical retrace has
// begun and ended (status port = the BIOS's CRTC port 0040:0063 + 6), to CRTC registers 0Ch/0Dh (OUT
// DX, AX at the CRTC port); with a pel panning (EGA) the next retrace is awaited and the attribute
// controller's register 13h (index 33h: the palette stays on) set to it. Returns 0.
// PORT: in mode 13h the CRTC is the VGA model's (vga_set_start) and the two retrace waits are one
// host_wait_vretrace; in the other modes the waits read the card's status port as the original
// does, after a host_wait_vretrace that keeps the pace of a real retrace.
u16 gfx_set_display_offset(s16 x, s16 y)
{
    const u16 ux = u16(x), uy = u16(y);
    u8 cl = 0xFF;  // the pel panning: none
    u16 start;
    switch (ds_u16(DS_gfx_mode_x2) >> 1) {
    case 4: case 5: case 6:  // 001a
        start = u16(u16((uy >> 1) * 0x28) + (ux >> 3));
        break;
    case 9:  // 002d
        start = u16(u16((uy >> 2) * 0x50) + (ux >> 2));
        break;
    case 0x0B:  // 0048
        start = u16(u16((uy >> 2) * 0x2D) + (ux >> 3));
        break;
    case 0x0C: {  // 0040
        const u16 ay = u16(u16(uy >> 1) + uy);
        const u16 dx2 = u16(ux << 1);
        start = u16(u16((ay >> 2) * 0x2D) + (dx2 >> 3));
        break;
    }
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12:  // 005d
        cl = u8(ux & 7);
        start = u16(u16(u32(uy) * ds_u16(DS_mode_row_bytes)) + (ux >> 3));
        start = u16(start + u16(u32(u16(s16(s8(ds_u8(DS_gfx_visible_page))))) * ds_u16(DS_mode_page_bytes)));
        break;
    case 0x13: {  // 007c
        start = u16(u16(u32(uy) * 0x50) + (ux >> 2));
        mem_u16(0x0040, 0x004E) = start;
        host_wait_vretrace();
        vga_set_start(u16(start << 2));
        return 0;
    }
    default:  // 00c9
        return 0;
    }
    mem_u16(0x0040, 0x004E) = start;  // 0089
    const u16 crtc = mem_u16(0x0040, 0x0063);
    const u16 status = u16((crtc & 0xFF00) | u8(u8(crtc) + 6));  // ADD DL, 6
    host_wait_vretrace();
    wait_status(status, true);
    wait_status(status, false);
    card_out16(crtc, u16((start & 0xFF00) | 0x0C));
    card_out16(crtc, u16(u8(start) << 8 | 0x0D));
    if (s8(cl) >= 0) {
        wait_status(status, true);
        const u16 attr = u16((crtc & 0xFF00) | 0xC0);  // MOV DL, C0h
        card_out(attr, 0x33);
        card_out(attr, cl);
    }
    return 0;
}

} // namespace gb
