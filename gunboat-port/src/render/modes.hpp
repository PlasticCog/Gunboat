#pragma once
// The renderer's routines for the video modes other than VGA (render3d.md §1.2, video.md §6): the
// CGA twins (0919:4056-47cf; Hercules draws through them too), the EGA twins (0919:48da-4eb6) and
// the Tandy twins (0919:4f96-5756) of the VGA routines in render.hpp. Each takes the registers its
// VGA twin takes unless its comment says otherwise. One file per mode: mode_cga.cpp, mode_ega.cpp,
// mode_tandy.cpp.
#include "types.hpp"

namespace gb {

// ---- CGA (mode 4), mode_cga.cpp
void blit_rows_cga(u8 al, u16 si);                                        // 0919:4056
void spotlight_beam_cga(u16 es, u16 bx, u16 dx);                          // 0919:44f0
u16 sky_water_cga(u16 es, u16 ax, u8 bl, u8 cl);                          // 0919:4587  AX unused
void water_marks_cga(u16 es, u16 ax, u16 bx, u16 cx, u16 dx, u16 di);     // 0919:4639  AX, DX unused
void span_cga_a(u16 es);                                                  // 0919:46e8
void span_cga_b(u16 es);                                                  // 0919:47cf

// ---- EGA (mode 0Dh), mode_ega.cpp (render3d.md §1.4; ported)
void blit_rows_ega(u8 al, u16 si);                                        // 0919:48da
void ega_gc_setup();                                                      // 0919:4989  (the view copies')
void spotlight_beam_ega(u16 es, u16 bx, u16 dx);                          // 0919:4c18
u16 sky_water_ega(u16 es, u16 ax, u8 bl, u8 cl);                          // 0919:4ce2  AH = sky; DI
void water_marks_ega(u16 es, u16 ax, u16 bx, u16 cx, u16 dx, u16 di);     // 0919:4d64  AL only; DX unused
void span_ega_a(u16 es);                                                  // 0919:4dfa
void span_ega_b(u16 es);                                                  // 0919:4eb6

// ---- Tandy (mode 9), mode_tandy.cpp
void blit_rows_tandy(u8 al, u16 si);                                      // 0919:4f96
void spotlight_beam_tandy(u16 es, u16 bx, u16 dx);                        // 0919:5494
u16 sky_water_tandy(u16 es, u16 ax, u8 bl, u8 cl);                        // 0919:5529
void water_marks_tandy(u16 es, u16 ax, u16 bx, u16 cx, u16 dx, u16 di);   // 0919:55da
void span_tandy_a(u16 es);                                                // 0919:5693
void span_tandy_b(u16 es);                                                // 0919:5756

} // namespace gb
