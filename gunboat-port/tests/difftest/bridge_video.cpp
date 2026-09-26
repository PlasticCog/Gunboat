// Bridge entries: the graphics library, text, palette and pictures (video.md, platform.md §6-§7).
#include "bridge.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"

using namespace gb;

BRIDGE(gfx_set_mode) { r.ax = gfx_set_mode(s16(a[0])); }
BRIDGE(gfx_detect) { r.ax = u16(gfx_detect()); }
BRIDGE(gfx_saved_mode) { r.ax = u16(gfx_saved_mode()); }
BRIDGE(gfx_get_draw_seg) { r.ax = gfx_get_draw_seg(); }
BRIDGE(gfx_set_draw_page) { gfx_set_draw_page(s16(a[0])); r.ax = 0; }
BRIDGE(gfx_set_copy_page) { gfx_set_copy_page(s16(a[0])); r.ax = 0; }
BRIDGE(gfx_set_visible_page) { gfx_set_visible_page(s16(a[0])); r.ax = 0; }
BRIDGE(gfx_alloc_page) { r.ax = gfx_alloc_page(s16(a[0])); }
BRIDGE(gfx_free_page) { r.ax = gfx_free_page(s16(a[0])); }
BRIDGE(gfx_move_to) { gfx_move_to(s16(a[0]), s16(a[1])); r.ax = 0; }
BRIDGE(gfx_set_colour) { gfx_set_colour(s16(a[0])); r.ax = 0; }
BRIDGE(gfx_fill_rect) { gfx_fill_rect(a[0], a[1], a[2], a[3]); r.ax = 0; }
BRIDGE(gfx_clear_page) { gfx_clear_page(); r.ax = 0; }
BRIDGE(gfx_copy_rect_from_copy_page) { gfx_copy_rect_from_copy_page(a[0], a[1], a[2], a[3]); r.ax = 0; }
BRIDGE(gfx_copy_rect_to_copy_page) { gfx_copy_rect_to_copy_page(a[0], a[1], a[2], a[3]); r.ax = 0; }
BRIDGE(gfx_draw_bitmap) { gfx_draw_bitmap(a[0], a[1], a[2]); r.ax = 0; }
BRIDGE(gfx_read_bitmap) { gfx_read_bitmap(a[0], a[1], a[2]); r.ax = 0; }
BRIDGE(text_exit_clear) { text_exit_clear(); r.ax = 0; }

BRIDGE(far_normalize)
{
    const FarPtr p = far_normalize({a[0], a[1]});
    r.ax = p.off;
    r.dx = p.seg;
}
BRIDGE(text_set_colours) { text_set_colours(s16(a[0]), s16(a[1])); }
BRIDGE(text_goto_cell) { text_goto_cell(s16(a[0]), s16(a[1])); }
BRIDGE(text_goto) { text_goto(s16(a[0]), s16(a[1])); }
BRIDGE(text_draw_char) { text_draw_char(&ds_u8(a[0])); }
BRIDGE(pal_fade_out) { pal_fade_out(); }
BRIDGE(pal_fade_in) { pal_fade_in(); }
BRIDGE(pal_black) { pal_black(); }
BRIDGE(pal_apply) { pal_apply(); }
BRIDGE(picture_draw_vga) { picture_draw_vga(a[0], a[1], a[2]); }
