// Bridge entries: the 3D renderer (render3d.md) and gfx_set_display_offset (video.md). Register
// routines take the registers the original reads (ES included) and return the ones its callers use.
#include "bridge.hpp"
#include "platform/gfx.hpp"
#include "platform/vga.hpp"
#include "render/render.hpp"

using namespace gb;

// camera.cpp
BRIDGE(atan)
{
    const AtanOut o = atan(r.cx, r.dx);
    r.ax = o.ax;
    r.cx = o.cx;
    r.dx = o.dx;
}
BRIDGE(polar_small)
{
    const CxDx o = polar_small(u8(r.bx), u8(r.dx));
    r.cx = o.cx;
    r.dx = o.dx;
}
BRIDGE(terrain_rect_test) { terrain_rect_test(r.cx, r.dx); }
BRIDGE(camera_position)
{
    const CxDx o = camera_position();
    r.cx = o.cx;
    r.dx = o.dx;
}
BRIDGE(chase_view_collision) { chase_view_collision(); }

// terrain.cpp
BRIDGE(terrain_cells_update) { terrain_cells_update(); }
BRIDGE(tile_load) { tile_load(u8(r.ax)); }
BRIDGE(vertex_load) { r.si = vertex_load(r.es, r.bx, r.di, r.cx, r.si); }
BRIDGE(route_rotate)
{
    const AxCx o = route_rotate(u8(r.ax), u8(r.cx));
    r.ax = o.ax;
    r.cx = o.cx;
}
BRIDGE(terrain_save_view) { terrain_save_view(); }
BRIDGE(order_reset) { order_reset(); }
BRIDGE(order_sort) { order_sort(); }
BRIDGE(terrain_setup) { terrain_setup(); }
BRIDGE(sky_water_vga) { r.di = sky_water_vga(r.es, r.ax, u8(r.bx), u8(r.cx)); }
BRIDGE(water_marks_vga) { water_marks_vga(r.es, r.ax, r.bx, r.cx, r.dx, r.di); }
BRIDGE(project) { project(r.si, r.ax); }
BRIDGE(shore_contact_test) { shore_contact_test(r.si); }
BRIDGE(shore_edge_test) { shore_edge_test(r.cx, r.dx); }
BRIDGE(colour_remap) { colour_remap(r.es, r.di, r.dx, r.si); }
BRIDGE(video_mode_setup) { video_mode_setup(); }

// draw.cpp
BRIDGE(draw_group_b) { draw_group_b(); }
BRIDGE(draw_primitive) { draw_primitive(r.es, u8(r.dx), r.bx); }
BRIDGE(fill_triangle) { fill_triangle(r.es, r.ax, r.cx, r.dx); }
BRIDGE(span_vga_a) { span_vga_a(r.es); }
BRIDGE(edge_setup) { edge_setup(r.es, r.ax, r.cx, r.dx); }
BRIDGE(span_vga_b) { span_vga_b(r.es); }
BRIDGE(spotlights) { spotlights(); }
BRIDGE(spotlight_beam) { spotlight_beam(r.ax, u8(r.bx)); }
BRIDGE(spotlight_beam_vga) { spotlight_beam_vga(r.es, r.bx, r.dx); }
BRIDGE(palette_flash) { palette_flash(); }
BRIDGE(screen_shake_step) { screen_shake_step(); }

// objects.cpp
BRIDGE(visible_list_rebuild) { visible_list_rebuild(); }
BRIDGE(list_bubble) { list_bubble(); }
BRIDGE(sprite_lod_update) { sprite_lod_update(); }
BRIDGE(sprite_lod_entry) { r.si = sprite_lod_entry(r.si, u8(r.dx), r.bx); }
BRIDGE(visible_project) { visible_project(); }
BRIDGE(sprite_slots_reset) { sprite_slots_reset(); }
BRIDGE(sprite_slot_alloc) { sprite_slot_alloc(u8(r.ax >> 8), r.si); }
BRIDGE(sprite_cache_invalidate) { sprite_cache_invalidate(); }
BRIDGE(list_quicksort) { list_quicksort(); }
BRIDGE(list_quicksort_range) { list_quicksort_range(a[1], a[0]); }  // pushed: first, then last
BRIDGE(list_swap) { list_swap(r.si, r.di); }
BRIDGE(list_bubble_range) { list_bubble_range(a[1], a[0]); }

// sprites.cpp
BRIDGE(sprite_prepare) { sprite_prepare(r.bx); }
BRIDGE(sprite_cache_build) { sprite_cache_build(); }
BRIDGE(blit_record) { blit_record(r.bx); }
BRIDGE(blit_place) { blit_place(r.es, r.bx, r.si); }
BRIDGE(blit_rows_vga) { blit_rows_vga(u8(r.ax), r.si); }
BRIDGE(sprite_scale_rows) { r.di = sprite_scale_rows(r.es, r.cx, r.si, r.di); }
BRIDGE(sprite_scale_rows_up) { r.di = sprite_scale_rows_up(r.es, r.cx, r.si, r.di); }
BRIDGE(sprite_scale_rows_up2) { r.di = sprite_scale_rows_up2(r.es, r.cx, r.si, r.di); }
BRIDGE(sprite_view_angle) { sprite_view_angle(r.bx, r.dx); }
BRIDGE(sprite_scale_patterns) { sprite_scale_patterns(); }
BRIDGE(sprite_row_box) { r.di = sprite_row_box(r.es, r.bx, r.si, u8(r.cx), r.di); }
BRIDGE(sprite_row_box_up) { r.di = sprite_row_box_up(r.es, r.bx, r.si, u8(r.cx), r.di); }
BRIDGE(sprite_row_box_up2) { r.di = sprite_row_box_up2(r.es, r.bx, r.si, u8(r.cx), r.di); }
BRIDGE(sprite_row_turn) { r.di = sprite_row_turn(r.es, r.bx, u8(r.cx), r.di); }
BRIDGE(sprite_row_turn_up) { r.di = sprite_row_turn_up(r.es, r.bx, u8(r.cx), r.di); }
BRIDGE(sprite_row_turn_up2) { r.di = sprite_row_turn_up2(r.es, r.bx, u8(r.cx), r.di); }
BRIDGE(sprite_row_flat) { r.di = sprite_row_flat(r.es, r.bx, u8(r.cx), r.di); }
BRIDGE(sprite_row_flat_up) { r.di = sprite_row_flat_up(r.es, r.bx, u8(r.cx), r.di); }
BRIDGE(sprite_row_flat_up2) { r.di = sprite_row_flat_up2(r.es, r.bx, u8(r.cx), r.di); }

// gfx_display.cpp
BRIDGE(gfx_set_display_offset) { r.ax = gfx_set_display_offset(s16(a[0]), s16(a[1])); }
// Test aid (not a function of the original): the VGA model's display start in bytes.
BRIDGE(probe_vga_start) { r.ax = vga_start(); }
