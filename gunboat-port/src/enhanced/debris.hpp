#pragma once
// Impact debris, an enhancement (settings "Impact debris"): small particles where the shots land, by
// what they hit: sparks off metal, wood chips, blood, stone chips, sand off sandbags; on the ground a
// splash on water, grass bits on green, and dust in the ground's own colour elsewhere. The game tells
// the host where each shot lands and what it hits (host_shot_landed, host_object_hit); the particles
// live in world coordinates and real time, and are drawn into the enhanced 3D view (present.cpp) with
// its projection, before the cockpit is laid over it. The game's memory is only read.
#include "enhanced/view3d.hpp"

namespace gb {

void debris_install();  // hooks the host's events
void debris_clear();    // no particles (after a quickload)
// The shot that just landed was stopped at (x, y) quarter units, height h (a hill: gameplay.cpp): its
// debris flies from there, as of the ground drawn there.
void debris_move_last(double x, double y, double h);

// Draws the live particles into a rendered region of the view: `target` as rendered (its palette
// indices, and its depth when it has one: the particles behind a hill are hidden), `rgb` its pixels (XRGB, `pitch` bytes a row) made from them with `pal`; `water` the
// scene's water colour. A new impact inside the region takes its ground's look from the pixel there.
void debris_draw(const ViewProjection &proj, const ViewTarget &target, u32 *rgb, int pitch, const u32 *pal,
                 u8 water);

} // namespace gb
