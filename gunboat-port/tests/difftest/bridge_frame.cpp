// Bridge entries: the last functions under game_frame (test_frame.py).
#include "bridge.hpp"
#include "game/sim.hpp"
#include "render/render.hpp"

using namespace gb;

BRIDGE(boat_motion) { r.si = boat_motion(r.si); }
BRIDGE(crew_pilot) { crew_pilot(); }
BRIDGE(incoming_fire) { incoming_fire(r.si); }
BRIDGE(object_update) { object_update(); }
BRIDGE(object_frame) { object_frame(); }
BRIDGE(key_f4_reverse_course) { key_f4_reverse_course(); }
