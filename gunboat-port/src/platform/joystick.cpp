// The joystick (platform.md §3). The original times port 201h in software; PORT: the port reads
// the host's gamepad and turns its axes into the counts a PC game port gives, in the range the
// calibration (joystick_calibrate) then measures. Used only when the joystick is enabled
// (DS:F394, GUNBOAT.CFG); the shipped configuration has it off.
#include "platform/platform.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// PORT: axis -32768..32767 -> count 8..504 (centre 256), a typical game-port range.
u16 axis_count(s16 v) { return u16(256 + (s32(v) * 248) / 32768); }

} // namespace

// 146a:000b joystick_axis: the stick's x count. PORT: from the host gamepad (stick 1 only; the
// second stick reads as absent, FFFFh, as a PC with one joystick).
u16 joystick_axis(u16 stick)
{
    s16 x, y;
    u8 buttons;
    if (((stick - 1) & 1) != 0 || !host_joy_read(&x, &y, &buttons)) return 0xFFFF;
    return axis_count(x);
}

// 15ea:0003 joystick_axis_y: the stick's y count (PORT as joystick_axis).
u16 joystick_axis_y(u16 stick)
{
    s16 x, y;
    u8 buttons;
    if (((stick - 1) & 1) != 0 || !host_joy_read(&x, &y, &buttons)) return 0xFFFF;
    return axis_count(y);
}

// 15d7:000d joystick_button: the stick's two buttons (bits 0-1, set = pressed). PORT: host.
u16 joystick_button(u16 stick)
{
    s16 x, y;
    u8 buttons;
    if (((stick - 1) & 1) != 0 || !host_joy_read(&x, &y, &buttons)) return 0;
    return buttons & 3;
}

// 1473:0008 joystick_read (platform.md §3): *code = 0Dh while a button is down (else 0); *dir = a
// keypad-style direction 47h..51h from the axes against the calibration limits, or 0 in the middle.
// A stick whose joy_x_low is FFFFh is absent: both 0.
void joystick_read(u16 stick, u8 *code, u8 *dir)
{
    const u16 slot = u16(((stick - 1) & 1) << 1);
    if (ds_u16(u16(DS_joy_x_low + slot)) == 0xFFFF) {
        *code = 0;
        *dir = 0;
        return;
    }
    u16 al = joystick_button(stick) ? 0x0D : 0;
    const u8 c = u8(al);
    const u16 x = joystick_axis(stick);
    const u16 y = joystick_axis_y(stick);
    u8 ah = 0xFF;  // left
    if (!(s16(x) < s16(ds_u16(u16(DS_joy_x_low + slot))))) {
        ah = 1;  // right
        if (!(s16(x) > s16(ds_u16(u16(DS_joy_x_high + slot))))) ah = 0;
    }
    u8 d = 0xFC;  // up
    if (!(s16(y) < s16(ds_u16(u16(DS_joy_y_low + slot))))) {
        d = 4;  // down
        if (!(s16(y) > s16(ds_u16(u16(DS_joy_y_high + slot))))) d = 0;
    }
    if (d != 0 || ah != 0) d = u8(d + 0x4C + ah);
    *code = c;
    *dir = d;
}

} // namespace gb
