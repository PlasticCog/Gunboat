#pragma once
// Idioms the game-flow code of GB.EXE repeats inline (not functions of the original): the draw page
// with its copy in DS:007A, the LZW decode into DS:1094, and a full-width picture by video mode.
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb::flow {

constexpr u16 PIC = 0x1094;  // the decode buffer

inline void set_draw_page(u16 page)
{
    ds_u16(DS_draw_page) = page;
    gfx_set_draw_page(s16(page));
}

// lzw_decode_picture of the far buffer whose pointer is at DS:far_ds, into DS:1094.
inline void decode(u16 far_ds) { lzw_decode_picture(ds_far(far_ds), {PIC, DGROUP}); }

inline bool vga() { return ds_u16(DS_video_mode) == 0x13; }

// A full-width picture of `runs` runs: picture_draw_vga with its bottom row y in VGA, picture_draw at
// the pen otherwise.
inline void draw_full(u16 runs, u16 y)
{
    if (!vga()) picture_draw(PIC, s16(runs), 0x140);
    else picture_draw_vga(PIC, runs, y);
}

} // namespace gb::flow
