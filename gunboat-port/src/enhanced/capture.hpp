#pragma once
// What the enhanced presentation reads from a finished 3D frame (src/enhanced/capture.cpp): a copy
// of DGROUP (camera, terrain vertices, draw order, visible objects), page 1's view window as the
// cockpit copied it, where each pixel of page 0 comes from in that window, and the sprites of the
// visible objects at their natural size. Captured by the host's frame hook (host_frame_pace, once per
// pass of a 3D station); the game's memory is left exactly as it was.
#include <SDL3/SDL_stdinc.h>

#include <vector>

#include "types.hpp"

namespace gb {

constexpr int VIEW_W = 256, VIEW_H = 64;       // the view window: page 1 x 40..295, y 64..127
constexpr int VIEW_X = 40, VIEW_Y = 64;
constexpr u16 NOT_VIEW = 0xFFFF;               // Scene::map: a page 0 pixel not copied from the view
constexpr int MAX_ENTRIES = 0xB5;              // the visible-object list (render3d.md §5.1)

// One part of a sprite image: w x h pixels, 0 = transparent.
struct SpritePart {
    int w = 0, h = 0;
    std::vector<u8> px;
};

// A visible object's image as sprite_cache_build makes it at size 17h (every column and row of the
// kind's data once: its natural size), for the view angle of this frame. part2 stands on part1,
// `shift` is the cache record's column ratio (+7).
struct SpriteImage {
    SpritePart part1, part2;
    u8 shift = 0;
};

struct Scene {
    bool valid = false;
    Uint64 time_ns = 0;
    u32 serial = 0;
    u8 ds[0x10000];                // DGROUP
    u8 window[VIEW_W * VIEW_H];    // page 1's view window after view_present (gun sprites on it)
    u16 map[64000];                // page 0 offset -> (window row << 8 | column), or NOT_VIEW
    u8 overlay[VIEW_W * VIEW_H];   // 1 where view_present drew over the view (the gun sprites)
    SpriteImage sprites[MAX_ENTRIES];
    bool has_sprite[MAX_ENTRIES];  // the entry is drawn this frame and has its image

    u8 u8_at(u16 off) const { return ds[off]; }
    u16 u16_at(u16 off) const { return u16(ds[off] | ds[u16(off + 1)] << 8); }
};

// Captures the frame now in memory into `sc` (the game's memory is restored afterwards).
void scene_capture(Scene &sc);

} // namespace gb
