// Placeholders for functions not ported yet (pending.hpp): each stops with a clear message instead
// of doing something else.
#include "game/pending.hpp"

#include "host.hpp"

namespace gb {

void mission_run() { host_fatal("not ported yet: the mission (mission_run 05bd:000a)"); }
void joystick_calibrate(u16) { host_fatal("not ported yet: joystick_calibrate (146e:000e)"); }

} // namespace gb
