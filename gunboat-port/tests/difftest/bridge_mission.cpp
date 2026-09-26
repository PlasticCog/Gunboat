// Bridge entries: the mission segment 05bd (world.md; hud.md §6).
#include "bridge.hpp"
#include "mission/mission.hpp"

using namespace gb;

// the present: C arguments (source page, destination page); AX is not compared (both callers ignore it)
BRIDGE(view_present) { r.si = view_present(a[0], a[1], r.si); }
