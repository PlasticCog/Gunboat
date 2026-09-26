// Bridge entries: the palette routines of the graphics library and the game side of the video modes
// other than VGA (video.md §2-§4, test_modes_game.py). Routines already bridged elsewhere
// (ega_pal_set, ega_pal_apply, gfx_set_display_offset, text_draw_char, dissolve_page1_to_0, ...)
// are tested through those entries.
#include "bridge.hpp"
#include "platform/gfx.hpp"

using namespace gb;

BRIDGE(ega_pal_register) { ega_pal_register(a[0], a[1]); r.ax = 0; }
BRIDGE(gfx_set_ega_palette) { gfx_set_ega_palette(a[0]); r.ax = 0; }
BRIDGE(gfx_set_pal_reg) { gfx_set_pal_reg(s16(a[0]), a[1], a[2], a[3]); r.ax = 0; }
