// Bridge entries: mission loading (world.md §4, §5), the terrain pass (render3d.md §3) and the crew
// pilot's routes (simulation.md §4.5, §4.6). Register routines take the registers the original
// reads and return the ones its callers use (route_point: AX, CX, DX and SI; route_advance: DX).
#include "bridge.hpp"
#include "game/sim.hpp"
#include "mission/mission_load.hpp"
#include "render/render.hpp"

using namespace gb;

BRIDGE(mission_load) { mission_load(); }
BRIDGE(mission_setup) { mission_setup(); }
BRIDGE(terrain_frame) { terrain_frame(); }
BRIDGE(route_point)
{
    const RoutePoint p = route_point(r.cx, r.bx);
    r.ax = p.ax;
    r.cx = p.cx;
    r.dx = p.dx;
    r.si = p.si;
}
BRIDGE(route_advance) { r.dx = route_advance(r.dx, r.cx, r.bx); }
BRIDGE(route_find) { route_find(); }
BRIDGE(crew_pilot_decide) { crew_pilot_decide(); }
