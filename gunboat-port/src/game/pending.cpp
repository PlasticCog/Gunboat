// Placeholders for functions not ported yet (pending.hpp): each stops with a clear message instead
// of doing something else.
#include "game/pending.hpp"

#include "host.hpp"

namespace gb {

void show_message_far(u16 id) { host_fatal("not ported yet: show_message (0919:1589), message %02Xh", id); }
void mission_run() { host_fatal("not ported yet: the mission (mission_run 05bd:000a)"); }

} // namespace gb
