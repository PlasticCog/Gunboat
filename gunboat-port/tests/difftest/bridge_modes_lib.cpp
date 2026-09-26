// Bridge entries: the graphics library's routines in the EGA, CGA and Tandy modes that have no entry
// in bridge_video.cpp (video.md §7). The library's C routines are bridged there and in bridge_hud.cpp.
#include "bridge.hpp"
#include "platform/gfx.hpp"

using namespace gb;

// 1390:006e picture_hline: near, ES = the draw page (picture_draw loads it), AX = x0, BX = x1 (no
// register output its callers use).
BRIDGE(picture_hline) { picture_hline(r.es, r.ax, r.bx); }
