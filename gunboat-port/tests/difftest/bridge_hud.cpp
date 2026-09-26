// Bridge entries: the cockpit, the view copies and the station screens (hud.md; world.md §3), the
// graphics library's line routines, the crack table and the buffer copies of segment 0000.
#include "bridge.hpp"
#include "game/flow.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"

using namespace gb;

// the panel (assembly: AX in, AX out)
BRIDGE(panel_redraw_all) { r.ax = panel_redraw_all(r.ax); }
BRIDGE(panel_blink) { r.ax = panel_blink(r.ax); }
BRIDGE(indicator_draw) { r.ax = indicator_draw(r.ax); }
BRIDGE(panel_switches_redraw) { r.ax = panel_switches_redraw(r.ax); }
BRIDGE(switch_draw) { r.ax = switch_draw(r.ax); }
BRIDGE(jet_indicator_draw) { r.ax = jet_indicator_draw(r.ax); }

// the pilot's gauges
BRIDGE(throttle_needles) { r.ax = throttle_needles(r.ax); }
BRIDGE(needle_draw)
{
    needle_draw(r.cx);
    r.ax = 0;
}
BRIDGE(jet_marker) { r.ax = jet_marker(r.ax); }
BRIDGE(radar_scope) { r.ax = radar_scope(r.ax); }
BRIDGE(radar_plot)
{
    radar_plot(u8(r.ax >> 8), r.cx);
    r.ax = 0;
}

// the view copies: the far routines take (src_page, dst_page); the VGA routines run with ES = the
// destination and DS = the source segment (the harness calls them with DS = DGROUP)
BRIDGE(view_copy_1) { view_copy_1(a[0], a[1]); }
BRIDGE(view_copy_2) { view_copy_2(a[0], a[1]); }
BRIDGE(view_copy_3) { view_copy_3(a[0], a[1]); }
BRIDGE(view_copy_4) { view_copy_4(a[0], a[1]); }
BRIDGE(view_copy_5) { view_copy_5(a[0], a[1]); }
BRIDGE(view_copy_6) { view_copy_6(a[0], a[1]); }
BRIDGE(view_copy_7) { view_copy_7(a[0], a[1]); }
BRIDGE(view_copy_8) { view_copy_8(a[0], a[1]); }
BRIDGE(view_copy_1_vga) { view_copy_1_vga(r.es, DGROUP); }
BRIDGE(view_copy_2_vga) { view_copy_2_vga(r.es, DGROUP); }
BRIDGE(view_copy_3_vga) { view_copy_3_vga(r.es, DGROUP); }
BRIDGE(view_copy_4_vga) { view_copy_4_vga(r.es, DGROUP); }
BRIDGE(view_copy_5_vga) { view_copy_5_vga(r.es, DGROUP); }
BRIDGE(view_copy_6_vga) { view_copy_6_vga(r.es, DGROUP); }
BRIDGE(view_copy_7_vga) { view_copy_7_vga(r.es, DGROUP); }
BRIDGE(view_copy_8_vga) { view_copy_8_vga(r.es, DGROUP); }
BRIDGE(view_copy_head_vga)
{
    const DiSi p = view_copy_head_vga(r.es, DGROUP);
    r.di = p.di;
    r.si = p.si;
    r.cx = 0;
}

// the station screens (05bd)
BRIDGE(map_screen) { map_screen(); }
BRIDGE(damage_report_screen) { damage_report_screen(); }
BRIDGE(assignment_screen) { assignment_screen(); }
BRIDGE(gun_sprites_capture) { gun_sprites_capture(); }
BRIDGE(gun_frame_draw) { gun_frame_draw(a[0]); }
BRIDGE(screen_clear) { screen_clear(); }
BRIDGE(station_screen_colours) { station_screen_colours(); }
BRIDGE(gun_panel_copy) { gun_panel_copy(a[0]); }
BRIDGE(window_cracks_draw) { window_cracks_draw(); }

// helpers of other segments
BRIDGE(gfx_line_to)
{
    gfx_line_to(s16(a[0]), s16(a[1]));
    r.ax = 0;
}
BRIDGE(gfx_fill_rect_clipped)
{
    gfx_fill_rect_clipped(s16(a[0]), s16(a[1]), s16(a[2]), s16(a[3]));
    r.ax = 0;
}
BRIDGE(crack_table_entry) { r.ax = crack_table_entry(a[0]); }
BRIDGE(far_to_near_copy) { far_to_near_copy({a[0], a[1]}, a[2], a[3]); }
BRIDGE(near_to_far_copy) { near_to_far_copy(a[0], {a[1], a[2]}, a[3]); }
