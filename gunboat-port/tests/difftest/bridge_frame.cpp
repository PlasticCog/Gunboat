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

// The top of the mission (mission_run.cpp, sim_frame.cpp): SI is the loop's register.
#include "mission/mission.hpp"
BRIDGE(game_frame) { game_frame(r.si); }
BRIDGE(pilot_screen) { r.si = pilot_screen(r.si); }
BRIDGE(bow_screen) { r.si = bow_screen(r.si); }
BRIDGE(stern_screen) { r.si = stern_screen(r.si); }
BRIDGE(midship_screen) { r.si = midship_screen(r.si); }
BRIDGE(chase_view_screen) { r.si = chase_view_screen(r.si); }
BRIDGE(view_restore) { r.si = view_restore(r.si); }
BRIDGE(mission_run) { mission_run(); }
