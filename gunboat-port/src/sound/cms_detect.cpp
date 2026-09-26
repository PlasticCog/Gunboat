// Creative sound card detection (sound.md §3.3), segment 1b5f: a Game Blaster (CMS) latch test, a
// Sound Blaster DSP reset and an OPL2 timer test at cms_base_port (220h). PORT: the modelled machine
// has none of them: their ports read FFh (hw.cpp), so cms_detect returns 0; its polls are bounded
// probes and do not pump the host.
//
// The routines add to DL only (ADD DL, n: no carry into DH), as the original.
#include "sound/sound.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u16 dl_add(u16 dx, u8 n) { return u16((dx & 0xFF00) | u8(u8(dx) + n)); }
u16 dl_sub(u16 dx, u8 n) { return u16((dx & 0xFF00) | u8(u8(dx) - n)); }
u16 base() { return ds_u16(DS_cms_base_port); }

} // namespace

// 1b5f:000e cms_detect (sound.md §3.3): AX = 1 Game Blaster (base+6 written, base+0Ah reads it back,
// for C6h and 39h), or 5 a Sound Blaster DSP (reset at base+6, AAh at base+0Ah; or command C6h
// answered 39h), plus 2 when an OPL2 at base+8 runs its timer 1; 0 = none.
u16 cms_detect()
{
    u16 bx = 0;
    u16 dx = dl_add(base(), 6);
    no_device_out(dx, 0xC6);
    dx = dl_add(dx, 4);
    no_device_out(dx, 0xC5);
    bool found = false;
    if (no_device_in(dx) == 0xC6) {
        dx = dl_sub(dx, 4);
        no_device_out(dx, 0x39);
        dx = dl_add(dx, 4);
        no_device_out(dx, 0x3A);
        if (no_device_in(dx) == 0x39) {
            bx = 1;
            found = true;
        }
    }
    if (!found) {  // 0039: the DSP reset
        dx = dl_add(base(), 6);
        no_device_out(dx, 1);
        for (int i = 0; i < 4; i++) no_device_in(dx);
        no_device_out(dx, 0);
        dx = dl_add(dx, 8);
        for (u16 cx = 0x14; cx; cx--) {
            if (no_device_in(dx) & 0x80) {
                dx = dl_sub(dx, 4);
                if (no_device_in(dx) == 0xAA) {
                    bx = 5;
                    found = true;
                    break;
                }
                dx = dl_add(dx, 4);
            }
        }
        if (!found && !cms_dsp_write(0xC6)) {
            u8 al = 0;
            if (!cms_dsp_read(al) && al == 0x39) bx = 5;
        }
    }
    // 0075: the OPL2 timer test
    cms_opl_write(0x0001);
    cms_opl_write(0x6004);
    cms_opl_write(0x8004);
    if (cms_opl_wait(0x00)) return bx;
    cms_opl_write(0xFF02);
    cms_opl_write(0x2104);
    if (cms_opl_wait(0xC0)) return bx;
    cms_opl_write(0x6004);
    cms_opl_write(0x8004);
    return u16(bx + 2);
}

// 1b5f:00b3 cms_opl_wait: up to 40h reads of the status (base+8) until its bits 5-7 equal AL's;
// carry on a time-out.
bool cms_opl_wait(u8 al)
{
    const u8 ah = al & 0xE0;
    const u16 dx = dl_add(base(), 8);
    for (u16 cx = 0x40; cx; cx--)
        if ((no_device_in(dx) & 0xE0) == ah) return false;
    return true;
}

// 1b5f:00d4 cms_opl_write: register AL, value AH (address base+8, data base+9), each followed by the
// delay.
void cms_opl_write(u16 ax)
{
    u16 dx = dl_add(base(), 8);
    no_device_out(dx, u8(ax));
    cms_opl_delay();
    dx = u16(dx + 1);  // INC DX (16-bit)
    no_device_out(dx, u8(ax >> 8));
    cms_opl_delay();
}

// 1b5f:00e7 cms_opl_delay: five reads of base+8.
void cms_opl_delay()
{
    const u16 dx = dl_add(base(), 8);
    for (int i = 0; i < 5; i++) no_device_in(dx);
}

// 1b5f:00f8 cms_dsp_read: up to 200h reads of base+0Eh until bit 7, then AL = base+0Ah; carry on a
// time-out (AL = the last status).
bool cms_dsp_read(u8 &al)
{
    u16 dx = dl_add(base(), 0x0E);
    for (u16 cx = 0x200; cx; cx--) {
        al = no_device_in(dx);
        if (al & 0x80) {
            dx = dl_sub(dx, 4);
            al = no_device_in(dx);
            return false;
        }
    }
    return true;
}

// 1b5f:0116 cms_dsp_write: up to 200h reads of base+0Ch until bit 7 is clear, then AL to it; carry
// on a time-out.
bool cms_dsp_write(u8 al)
{
    const u16 dx = dl_add(base(), 0x0C);
    for (u16 cx = 0x200; cx; cx--) {
        if (!(no_device_in(dx) & 0x80)) {
            no_device_out(dx, al);
            return false;
        }
    }
    return true;
}

} // namespace gb
