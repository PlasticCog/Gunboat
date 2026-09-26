#pragma once
// Functions the ported code calls whose port is not in this tree yet (pending.cpp): each is fatal
// if reached, and goes away when its real port arrives.
#include "types.hpp"

namespace gb {

void show_message_far(u16 id);       // 0919:1589  the cockpit messages (a mission only)
u16 hq_quiz();                       // 020d:0008  the headquarters
void front_end();                    // 02d2:0008
void mission_run();                  // 05bd:000a
void joystick_calibrate(u16 stick);  // 146e:000e  (with a joystick enabled)

} // namespace gb
