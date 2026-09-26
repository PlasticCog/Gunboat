// Placeholders for functions not ported yet (pending.hpp): each stops with a clear message instead
// of doing something else.
#include "game/pending.hpp"

#include "host.hpp"

namespace gb {

void mission_run() { host_fatal("not ported yet: the mission (mission_run 05bd:000a)"); }

// F4 "Pilot, reverse course." (simulation.md §3.3, reached through key_dispatch at a 3D station): its
// port waits for route_point (0919:8754) and route_advance (0919:87e8).

} // namespace gb
