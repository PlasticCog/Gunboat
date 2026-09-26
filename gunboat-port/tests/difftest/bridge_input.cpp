// Bridge entries: keyboard, timer and input (platform.md §1-§3).
#include "bridge.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"

using namespace gb;

BRIDGE(kbd_install) { kbd_install(); }
BRIDGE(kbd_restore) { kbd_restore(); }
BRIDGE(kbd_isr) { kbd_isr(u8(a[0])); }  // the test passes the port 60h byte as a[0]
// The joystick (platform.md §3): the axis and button reads are host-replaced (PORT); the tests
// stub the original's with the same mapping (test_input.joystick_stubs).
BRIDGE(joystick_calibrate) { r.ax = joystick_calibrate(a[0]); }
BRIDGE(joystick_read) { joystick_read(a[0], &ds_u8(a[1]), &ds_u8(a[2])); }
