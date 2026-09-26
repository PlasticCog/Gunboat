// Bridge entries: keyboard, timer and input (platform.md §1-§3).
#include "bridge.hpp"
#include "platform/platform.hpp"

using namespace gb;

BRIDGE(kbd_install) { kbd_install(); }
BRIDGE(kbd_restore) { kbd_restore(); }
BRIDGE(kbd_isr) { kbd_isr(u8(a[0])); }  // the test passes the port 60h byte as a[0]
