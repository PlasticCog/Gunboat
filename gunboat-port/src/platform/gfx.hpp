#pragma once
// The graphics library (video.md; the same library as Test Drive III's, an older build). Each
// primitive dispatches on the library mode (gfx_mode_x2); the port implements the mode 13h paths
// and, where the game needs them outside VGA (start-up and exit in text mode), those too. Routines
// that always return 0 in the original are void here.
#include "types.hpp"

namespace gb {

u16 gfx_set_mode(s16 mode);         // 1555:0005  returns 0
s16 gfx_detect();                   // 1386:0004  11h..13h VGA/MCGA, 4/7/9/0Bh/0Dh..10h others
s16 gfx_saved_mode();               // 1469:0008
u16 gfx_get_draw_seg();             // 1432:0008
void gfx_set_draw_page(s16 page);   // 157d:000e
void gfx_set_copy_page(s16 page);   // 154e:000b
void gfx_set_visible_page(s16 page);// 1592:0004
u16 gfx_alloc_page(s16 page);       // 137e:0000  0 ok, 1 bad page, 7/8 DOS error
u16 gfx_free_page(s16 page);        // 142c:0009  0 ok, 1 not a RAM-page mode, 7/9 DOS error
void gfx_move_to(s16 x, s16 y);     // 147b:000c
void gfx_set_colour(s16 colour);    // 1543:000b
void gfx_fill_rect(u16 x0, u16 x1, u16 y0, u16 y1);                  // 14cf:0008
void gfx_clear_page();                                               // 15e2:0001
void gfx_copy_rect_from_copy_page(u16 x0, u16 x1, u16 y0, u16 y1);   // 1502:0001
void gfx_copy_rect_to_copy_page(u16 x0, u16 x1, u16 y0, u16 y1);     // 1522:000e
void gfx_draw_bitmap(u16 bits_ds, u16 bytes_per_row, u16 rows);      // 13e2:0002
void gfx_read_bitmap(u16 bits_ds, u16 bytes_per_row, u16 rows);      // 1432:000c
void text_exit_clear();                                              // 14ff:0001
void picture_draw(u16 src_ds, s16 runs, u16 width);                  // 1390:0000
void gfx_copy_rect(u16 x0, u16 x1, u16 y0, u16 y1, u16 dx, u16 dy_bottom, u16 src_page, u16 dst_page);  // 15a4:0006
void gfx_put_pixel(s16 x, s16 y);                                    // 14b5:000d
void ega_pal_set(u16 index, u16 value);                              // 14ae:0005
u16 gfx_set_display_offset(s16 x, s16 y);                            // 149f:0004  returns 0 (gfx_display.cpp)

} // namespace gb
