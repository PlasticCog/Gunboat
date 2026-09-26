#pragma once
// The 3D renderer (render3d.md): camera, terrain window and projection, terrain primitives, the
// visible-object list, the sprite cache and its blitter, spotlights, flash and shake. One function
// per original function (segment 0919 unless noted). The renderer is also game logic: the
// simulation reads its visible-object list, its shore-contact candidates and its ramming test.
//
// Most of these are assembly routines with register arguments: they take the registers they read
// (ES included, for the page or buffer they write) and return the registers their callers use.
// Only the VGA (mode 13h) paths are ported; the EGA, Tandy and CGA branches are parked and fatal
// (render_parked).
#include "types.hpp"

namespace gb {

struct AtanOut {
    u16 ax;  // AL = angle byte (256 per turn), AH = low 3 bits of the table value
    u16 cx;  // min(|dx|, |dy|)
    u16 dx;  // max(|dx|, |dy|)
};
struct CxDx {
    u16 cx, dx;
};
struct AxCx {
    u16 ax, cx;
};

[[noreturn]] void render_parked(const char *what);  // PORT: an EGA/Tandy/CGA path was reached

// ---- camera (camera.cpp)
AtanOut atan(u16 cx, u16 dx);        // 0919:3712  CX = dx, DX = dy
CxDx polar_small(u8 bl, u8 dl);      // 0919:8331  BL = angle, DL = distance
void terrain_rect_test(u16 cx, u16 dx);  // 0919:7c38  point in quarter units -> scratch B7E2
CxDx camera_position();              // 0919:8229  the camera in quarter units
void chase_view_collision();         // 0919:82de

// ---- terrain window, projection, sky and water (terrain.cpp)
void terrain_cells_update();                                // 0919:8408
void tile_load(u8 al);                                      // 0919:858f  AL = grid cell byte
u16 vertex_load(u16 es, u16 bx, u16 di, u16 cx, u16 si);    // 0919:8665  returns SI
AxCx route_rotate(u8 al, u8 cl);                            // 0919:8711  AL = x, CL = y
void terrain_save_view();                                   // 0919:728c
void order_reset();                                         // 0919:72a9
void order_sort();                                          // 0919:72c0
void terrain_setup();                                       // 0919:736c
u16 sky_water_vga(u16 es, u16 ax, u8 bl, u8 cl);            // 0919:74c0  returns DI
void water_marks_vga(u16 es, u16 ax, u16 bx, u16 cx, u16 dx, u16 di);  // 0919:7458
void project(u16 si, u16 ax);                               // 0919:7523  SI = first, AX = end
void shore_contact_test(u16 si);                            // 0919:79e1  SI = candidate (2i)
void shore_edge_test(u16 cx, u16 dx);                       // 0919:7a3e
void colour_remap(u16 es, u16 di, u16 dx, u16 si);          // 0919:3f8d  [DI, DX) through SI
void video_mode_setup();                                    // 0919:3fba

// ---- primitives, spotlights, flash and shake (draw.cpp)
void draw_group_b();                                  // 0919:763f
void draw_primitive(u16 es, u8 dl, u16 bx);           // 0919:767a  DL = mode, BX = vertex
void fill_triangle(u16 es, u16 ax, u16 cx, u16 dx);   // 0919:7802  AL/AH = rows, CX/DX = bearings
void span_vga_a(u16 es);                              // 0919:788e  [DS:D8F8] in VGA
void edge_setup(u16 es, u16 ax, u16 cx, u16 dx);      // 0919:7909
void span_vga_b(u16 es);                              // 0919:7943  [DS:D8FA] in VGA
void spotlights();                                    // 0919:7a89
void spotlight_beam(u16 ax, u8 bl);                   // 0919:7b17  AX = bearing, BL = elevation
void spotlight_beam_vga(u16 es, u16 bx, u16 dx);      // 0919:7bbd  BX = width table, DX = column
void palette_flash();                                 // 0919:2c35
void screen_shake_step();                             // 0919:2e57

// ---- the visible-object list and the sprite cache slots (objects.cpp)
void visible_list_rebuild();                          // 0919:69a9
void list_bubble();                                   // 0919:6c23
void sprite_lod_update();                             // 0919:6cad
u16 sprite_lod_entry(u16 si, u8 dl, u16 bx);          // 0919:6cfa  SI = entry + 1, DL = class; SI
void visible_project();                               // 0919:6d92
void sprite_slots_reset();                            // 0919:6f3d
void sprite_slot_alloc(u8 ah, u16 si);                // 0919:7017  AH = level, SI = entry + 1
void sprite_cache_invalidate();                       // 0919:5a9f  (far)
void list_quicksort();                                // 0919:8eb9
void list_quicksort_range(u16 first, u16 last);       // 0919:8ece  byte offsets (entry x 2)
void list_swap(u16 si, u16 di);                       // 0919:8f53
void list_bubble_range(u16 first, u16 last);          // 0919:8fa7

// ---- sprite images: the scaled cache and the blitter (sprites.cpp)
void sprite_prepare(u16 bx);                          // 0919:5acc  BX = entry
void sprite_cache_build();                            // 0919:5aeb
void blit_record(u16 bx);                             // 0919:5c71  BX = entry
void blit_place(u16 es, u16 bx, u16 si);              // 0919:5d5a
void blit_rows_vga(u8 al, u16 si);                    // 0919:5e66  AL = first row
u16 sprite_scale_rows(u16 es, u16 cx, u16 si, u16 di);      // 0919:5f4b  returns DI
u16 sprite_scale_rows_up(u16 es, u16 cx, u16 si, u16 di);   // 0919:5fc8
u16 sprite_scale_rows_up2(u16 es, u16 cx, u16 si, u16 di);  // 0919:6054
void sprite_view_angle(u16 bx, u16 dx);               // 0919:60e0  BX = entry, DX = distance
void sprite_scale_patterns();                         // 0919:6174
// The row scalers: BX = source row (DGROUP), SI = its row-type entry, CL = the type byte, DI = the
// output (ES); they return DI.
u16 sprite_row_box(u16 es, u16 bx, u16 si, u8 cl, u16 di);       // 0919:6244
u16 sprite_row_box_up(u16 es, u16 bx, u16 si, u8 cl, u16 di);    // 0919:6373
u16 sprite_row_box_up2(u16 es, u16 bx, u16 si, u8 cl, u16 di);   // 0919:64ac
u16 sprite_row_turn(u16 es, u16 bx, u8 cl, u16 di);              // 0919:65eb
u16 sprite_row_turn_up(u16 es, u16 bx, u8 cl, u16 di);           // 0919:66a7
u16 sprite_row_turn_up2(u16 es, u16 bx, u8 cl, u16 di);          // 0919:6764
u16 sprite_row_flat(u16 es, u16 bx, u8 cl, u16 di);              // 0919:6824
u16 sprite_row_flat_up(u16 es, u16 bx, u8 cl, u16 di);           // 0919:68a4
u16 sprite_row_flat_up2(u16 es, u16 bx, u8 cl, u16 di);          // 0919:6925

} // namespace gb
