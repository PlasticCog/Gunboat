#pragma once
// The enhanced 3D view (src/enhanced/view3d.cpp): the view of a captured frame drawn again at any
// resolution and for any camera between two captured frames. It follows the original renderer
// (render3d.md) step by step: the horizon from the pitch, sky, horizon line and water, the water
// marks, group B of the terrain, then group A and the sprites back to front in the frame's own order,
// then the spotlights (with the extended draw distance, the far cells go before group B); the
// projection is the original's (a cylinder: 128 bearing units per column,
// rows from the scale 7FFFh / distance) in floating point instead of bytes, and the sprites are the
// frame's own images at their natural size, scaled as the original scales them. Palette indices are
// drawn, so the palette effects (the flash, the spotlights' bit 3) stay the original's.
//
// Coordinates are page coordinates (x = column, y = row of the 320x200 page; the original view
// window is x 40..295, y 64..127). A target covers a rectangle of them at a scale.
#include "enhanced/capture.hpp"

namespace gb {

struct ViewTarget {
    u8 *px = nullptr;  // w x h palette indices
    int w = 0, h = 0;
    double ox = 0, oy = 0;  // page coordinates of the target's top-left corner
    double sx = 1, sy = 1;  // target pixels per page pixel
    // Rows of the target whose pixels [skip0, skip1) are not needed (hidden by the cockpit): the
    // rows outside [need_y0, need_y1).
    int need_y0 = 0, need_y1 = 1 << 30, skip0 = 0, skip1 = 0;
    bool far = true;  // draw the scene's far cells (the extended draw distance) when it has them
};

// Where the horizon line was drawn: its first page row (it is two rows of colour 8) and the sky and
// water colours around it.
struct ViewHorizon {
    double y = 0;
    u8 sky = 0, water = 0;
};

// Draws the view of `cur` (camera and objects interpolated from `prev` at t in 0..1 when prev is
// given and compatible) into the target.
ViewHorizon view3d_render(const Scene &cur, const Scene *prev, double t, const ViewTarget &target);

// Whether two captures can be interpolated (same station, look direction and view, a small move).
bool view3d_compatible(const Scene &a, const Scene &b);

// The view's projection, for drawing into it from outside (the impact debris, debris.hpp): the camera
// of `cur`, interpolated from `prev` at t as view3d_render does it. point() puts a world point at
// (x, y) quarter units and height h (0: the ground and the water; the eye is at 32) on the page and
// returns its scale (255 nearest, else 32767.5 / distance): h units are s / 256 page rows high.
struct ViewProjection {
    double qx = 0, qy = 0, view = 0, horizon = 0;
    double point(double x, double y, double h, double &px, double &py) const;
};
ViewProjection view3d_projection(const Scene &cur, const Scene *prev, double t);

} // namespace gb
