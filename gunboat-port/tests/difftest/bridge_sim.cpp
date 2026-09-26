// Bridge entries: simulation (simulation.md).
#include "bridge.hpp"
#include "game/sim.hpp"

using namespace gb;

BRIDGE(vec_scale) { r.ax = vec_scale(u8(r.ax)); }
BRIDGE(heading_vector) { r.ax = heading_vector(u8(r.ax)); }
BRIDGE(boat_move) { boat_move(); }
