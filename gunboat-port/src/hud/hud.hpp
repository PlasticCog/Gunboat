#pragma once
// The cockpit and the full-screen stations (hud.md; world.md §3): the panel lamps and switches, the
// pilot's gauges and radar, the view copies of view_present, the station screens of segment 05bd.
// One function per original function.
//
// Near assembly routines take the registers they read and return AX as they leave it: their callers
// (game_frame, the key handlers, view_restore) pass AX from one routine to the next, and the
// panel routines store AH into scratch_b7e3, so the value is game state (PORTING.md).
#include "types.hpp"

namespace gb {

// ---- the panel (panel.cpp), hud.md §2
u16 panel_redraw_all(u16 ax);       // 0919:0000  far; mode 40h for lamps 0..1Dh (AH kept); AX out
u16 panel_blink(u16 ax);            // 0919:000e  AH kept for the blink calls; AX out
u16 indicator_draw(u16 ax);         // 0919:0027  AL = mode | id, AH = value; AX out
u16 panel_switches_redraw(u16 ax);  // 0919:016a  far; mode 40h for switches 0..10h; AX out
u16 switch_draw(u16 ax);            // 0919:0178  AL = mode | switch, AH = value; AX out
u16 jet_indicator_draw(u16 ax);     // 0919:0f77  far; AX out

// ---- the pilot's gauges (gauges.cpp), hud.md §3
u16 throttle_needles(u16 ax);       // 0919:2324  AX out
void needle_draw(u16 cx);           // 0919:2704  CX = the new end point (dx, dy); AX = 0 after
u16 jet_marker(u16 ax);             // 0919:2786  AX out
u16 radar_scope(u16 ax);            // 0919:2810  AX out
void radar_plot(u8 ah, u16 cx);     // 0919:2932  AH = bearing, CX = distance; AX = 0 after, BX kept

// ---- the view copies of view_present (views.cpp), hud.md §6. The far routines take the source
// and destination page numbers (pages from page_segments); the VGA routines the segments (ES, DS).
// They return the SI the copy leaves (the far routines keep DS and ES only).
u16 view_copy_1(u16 src_page, u16 dst_page, u16 si);  // 0919:8a32
u16 view_copy_2(u16 src_page, u16 dst_page, u16 si);  // 0919:8ad5
u16 view_copy_3(u16 src_page, u16 dst_page, u16 si);  // 0919:8b43
u16 view_copy_4(u16 src_page, u16 dst_page, u16 si);  // 0919:8bf9
u16 view_copy_5(u16 src_page, u16 dst_page, u16 si);  // 0919:8cd3
u16 view_copy_6(u16 src_page, u16 dst_page, u16 si);  // 0919:8d45
u16 view_copy_7(u16 src_page, u16 dst_page, u16 si);  // 0919:8db7
u16 view_copy_8(u16 src_page, u16 dst_page, u16 si);  // 0919:8e47
u16 view_copy_1_vga(u16 es, u16 ds);          // 0919:8a72
u16 view_copy_2_vga(u16 es, u16 ds);          // 0919:8b15
u16 view_copy_3_vga(u16 es, u16 ds);          // 0919:8b83
u16 view_copy_4_vga(u16 es, u16 ds);          // 0919:8c39
u16 view_copy_5_vga(u16 es, u16 ds);          // 0919:8d13
u16 view_copy_6_vga(u16 es, u16 ds);          // 0919:8d85
u16 view_copy_7_vga(u16 es, u16 ds);          // 0919:8df7
u16 view_copy_8_vga(u16 es, u16 ds);          // 0919:8e87
struct DiSi {
    u16 di, si;
};
DiSi view_copy_head_vga(u16 es, u16 ds);       // 0919:8e29  returns DI, SI (CX = 0)
// The twins of the other video modes (hud/views_ega.cpp, views_tandy.cpp, views_cga.cpp; the
// addresses are in each view_copy_N's dispatch): same registers, SI returned.
u16 view_copy_1_ega(u16 es, u16 ds);
u16 view_copy_2_ega(u16 es, u16 ds);
u16 view_copy_3_ega(u16 es, u16 ds);
u16 view_copy_4_ega(u16 es, u16 ds);
u16 view_copy_5_ega(u16 es, u16 ds);
u16 view_copy_6_ega(u16 es, u16 ds);
u16 view_copy_7_ega(u16 es, u16 ds);
u16 view_copy_8_ega(u16 es, u16 ds);
u16 view_copy_1_tandy(u16 es, u16 ds);
u16 view_copy_2_tandy(u16 es, u16 ds);
u16 view_copy_3_tandy(u16 es, u16 ds);
u16 view_copy_4_tandy(u16 es, u16 ds);
u16 view_copy_5_tandy(u16 es, u16 ds);
u16 view_copy_6_tandy(u16 es, u16 ds);
u16 view_copy_7_tandy(u16 es, u16 ds);
u16 view_copy_8_tandy(u16 es, u16 ds);
u16 view_copy_1_cga(u16 es, u16 ds);
u16 view_copy_2_cga(u16 es, u16 ds);
u16 view_copy_3_cga(u16 es, u16 ds);
u16 view_copy_4_cga(u16 es, u16 ds);
u16 view_copy_5_cga(u16 es, u16 ds);
u16 view_copy_6_cga(u16 es, u16 ds);
u16 view_copy_7_cga(u16 es, u16 ds);
u16 view_copy_8_cga(u16 es, u16 ds);

// ---- the station screens (screens.cpp), segment 05bd, hud.md §5
void map_screen();                  // 05bd:19cc  station 5
void damage_report_screen();        // 05bd:1ba4  station 7
void assignment_screen();           // 05bd:1dba  station 8
void gun_sprites_capture();         // 05bd:1ed0
void gun_frame_draw(u16 bearing);   // 05bd:2cde  the gun's bearing relative to the hull (+20h, -60h)
void screen_clear();                // 05bd:2efc
void station_screen_colours();      // 05bd:2f6a
void gun_panel_copy(u16 parts);     // 05bd:30ae
void window_cracks_draw();          // 05bd:3114

} // namespace gb
