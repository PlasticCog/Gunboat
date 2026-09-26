// The mission's present (hud.md §6; world.md §3.2-3.3): after each game_frame, the station's gun
// sprites are drawn over the new 3D view on the source page, then the view goes to the destination
// page through the station's cockpit openings: a rectangle for the rows below the sky that changed
// and one of the eight view copies (view_copy_1..8, package H).
//
// Only the VGA path of the copies is ported (hud/views.cpp); view_present itself has no video mode
// branch.
//
// PORT: the original returns whatever its last callee leaves in AX; both callers ignore it
// (view_restore clears AX, mission_run's key_dispatch reloads it), so the port returns nothing.
#include "mission/mission.hpp"

#include "hud/hud.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// The idiom each station repeats inline (05bd:201a, 209e, 211e, 2429, 27d5, 2c51): the first view
// row to copy is 40h below the smaller (higher on the screen) of this frame's and the last present's
// sky top, in 8 bits (a sky top from C0h up wraps to a low row); it stays in view_sky_top_prev until
// the end of the idiom. Returns it.
u8 view_rows_top()
{
    if (ds_u8(DS_view_sky_top) < ds_u8(DS_view_sky_top_prev)) ds_u8(DS_view_sky_top_prev) = ds_u8(DS_view_sky_top);
    ds_u8(DS_view_sky_top_prev) = u8(ds_u8(DS_view_sky_top_prev) + 0x40);
    return ds_u8(DS_view_sky_top_prev);
}

// The rows view_rows_top..bottom of the view window (x 28h..127h of page src) to the screen
// rectangle whose bottom row is dy (x 20h); then view_sky_top_prev = view_sky_top. The midship and
// stern stations first clamp view_sky_top to 21h (05bd:241d, 27c9), the bow does not.
void view_rows_copy(u8 bottom, u16 dy, u16 src_page, u16 dst_page)
{
    if (view_rows_top() <= bottom)
        gfx_copy_rect(0x28, 0x127, ds_u8(DS_view_sky_top_prev), bottom, 0x20, dy, src_page, dst_page);
    ds_u8(DS_view_sky_top_prev) = ds_u8(DS_view_sky_top);
}

// The pilot's two rectangles for one look direction (05bd:201a, 209e, 211e): rows view_rows_top..77h
// of the view columns a0..a1 and b0..b1 to x = da / db, bottom row 4Fh; then
// view_sky_top_prev = view_sky_top.
void pilot_rows_copy(u16 a0, u16 a1, u16 da, u16 b0, u16 b1, u16 db, u16 src_page, u16 dst_page)
{
    if (view_rows_top() <= 0x77) {
        gfx_copy_rect(a0, a1, ds_u8(DS_view_sky_top_prev), 0x77, da, 0x4F, src_page, dst_page);
        gfx_copy_rect(b0, b1, ds_u8(DS_view_sky_top_prev), 0x77, db, 0x4F, src_page, dst_page);
    }
    ds_u8(DS_view_sky_top_prev) = ds_u8(DS_view_sky_top);
}

// The draw page, as the routine sets it (mov [007A],ax; gfx_set_draw_page).
void draw_page(u16 page)
{
    ds_u16(DS_draw_page) = page;
    gfx_set_draw_page(s16(page));
}

void colour(u16 ds_colour) { gfx_set_colour(s16(ds_u8(ds_colour))); }

// The muzzle flash sprites of the midship and stern guns (05bd:221e, 2281, 25bb, 266c): at (A0h, 77h)
// the flash in muzzle_flash_colour and its white core.
void muzzle_flash_draw()
{
    gfx_move_to(0xA0, 0x77);
    colour(DS_muzzle_flash_colour);
    gfx_draw_bitmap(DS_gun_sprite_f2a4, 2, 0x0E);
    gfx_set_colour(0x0F);
    gfx_draw_bitmap(DS_gun_sprite_f2c0, 2, 0x0E);
}

// The ammunition belt of the midship weapon 2 and the stern weapon 1 (05bd:227a, 2665), a picture
// of page 1 (x C0h..12Fh, two frames: rows 9Dh..A8h and A9h..B4h) copied straight to page 0 at
// (18h, bottom row 67h), whatever the pages given: with this frame's flash latched, the muzzle flash
// sprites and the first frame, or the second on odd world passes when the last frame's flash was
// latched too; without it, the second frame while the last frame's flash is latched.
void ammo_belt_draw(u16 latch, u16 latch_prev)
{
    bool second;
    if (ds_u8(latch) != 0) {
        muzzle_flash_draw();
        second = ds_u8(latch_prev) != 0 && (ds_u8(DS_world_pass_counter) & 1) != 0;
    } else {
        if (ds_u8(latch_prev) == 0) return;
        second = true;
    }
    if (second) gfx_copy_rect(0xC0, 0x12F, 0xA9, 0xB4, 0x18, 0x67, 1, 0);
    else gfx_copy_rect(0xC0, 0x12F, 0x9D, 0xA8, 0x18, 0x67, 1, 0);
}

// The sight post of the midship and stern guns (05bd:21c3, 2553): 16 x 25 at (A0h, 79h), its dark
// layer and gun_rail_colour layer.
void sight_post_draw()
{
    gfx_move_to(0xA0, 0x79);
    colour(DS_gun_dark_colour);
    gfx_draw_bitmap(DS_gun_sprite_e9e2, 2, 0x19);
    colour(DS_gun_rail_colour);
    gfx_draw_bitmap(DS_gun_sprite_ea14, 2, 0x19);
}

// The pieces of the midship weapon 2 and the stern weapon 1 (05bd:233b, 271d): 16 x 3 at (A0h, 79h).
void gun_pieces_16x3()
{
    gfx_move_to(0xA0, 0x79);
    colour(DS_gun_shade_colour);
    gfx_draw_bitmap(DS_gun_sprite_f3a2, 2, 3);
    colour(DS_gun_edge_colour);
    gfx_draw_bitmap(DS_gun_sprite_f3a8, 2, 3);
}

// 05bd:219c: station 3 (midship).
u16 present_midship(u16 src_page, u16 dst_page, u16 si)
{
    draw_page(src_page);
    gun_frame_draw(u16(ds_u8(u16(DS_heading + 2)) - ds_u8(DS_heading) + 0x20));
    sight_post_draw();
    if (ds_u8(DS_flash_midship) != 0) muzzle_flash_draw();
    if (ds_u8(DS_midship_weapon) == 2) ammo_belt_draw(DS_flash_midship_latch, DS_flash_midship_latch_prev);
    const u8 weapon = ds_u8(DS_midship_weapon);
    if (weapon == 0) {                      // 05bd:2492
        gfx_move_to(0xB0, 0x7F);
        colour(DS_gun_dark_colour);
        gfx_draw_bitmap(DS_gun_sprite_f3a2, 1, 4);
        colour(DS_gun_edge_colour);
        gfx_draw_bitmap(DS_gun_sprite_f3a6, 1, 4);
        gfx_move_to(0x98, 0x7F);
        gfx_draw_bitmap(DS_gun_sprite_f5b4, 1, 6);
        colour(DS_gun_dark_colour);
        gfx_draw_bitmap(DS_gun_sprite_f5ae, 1, 6);
    } else if (weapon == 1) {               // 05bd:2388
        gfx_move_to(0xB8, 0x7F);
        colour(DS_midship_mount_colour);
        gfx_draw_bitmap(DS_gun_sprite_f3a2, 1, 5);
        gfx_move_to(0x90, 0x7F);
        colour(DS_gun_edge_colour);
        gfx_draw_bitmap(DS_gun_sprite_f5b3, 1, 5);
        colour(DS_gun_light_colour);
        gfx_draw_bitmap(DS_gun_sprite_f5ae, 1, 5);
    } else {                                // 05bd:233b
        gun_pieces_16x3();
    }
    draw_page(dst_page);
    if (ds_u8(DS_view_sky_top) > 0x21) ds_u8(DS_view_sky_top) = 0x21;
    view_rows_copy(0x6C, 0x38, src_page, dst_page);
    if (ds_u8(DS_midship_weapon) == 1) return view_copy_5(src_page, dst_page, si);
    return view_copy_6(src_page, dst_page, si);
}

// 05bd:252c: station 4 (stern).
u16 present_stern(u16 src_page, u16 dst_page, u16 si)
{
    draw_page(src_page);
    gun_frame_draw(u16(ds_u8(u16(DS_heading + 3)) - ds_u8(DS_heading) + 0x20));
    sight_post_draw();
    if (ds_u8(DS_flash_stern_latch) != 0 && ds_u8(DS_stern_weapon) == 0) {  // 05bd:25bb
        muzzle_flash_draw();
        // the grenade rack, a picture of page 1 (rows 30h..3Ch) straight to page 0 at (10h, bottom row
        // 77h): x 88h..DDh when the latched counter is 1, else E0h..137h
        if (ds_u8(DS_flash_stern_latch) == 1) gfx_copy_rect(0x88, 0xDD, 0x30, 0x3C, 0x10, 0x77, 1, 0);
        else gfx_copy_rect(0xE0, 0x137, 0x30, 0x3C, 0x10, 0x77, 1, 0);
    }
    if (ds_u8(DS_stern_weapon) == 1) ammo_belt_draw(DS_flash_stern_latch, DS_flash_stern_latch_prev);
    if (ds_u8(DS_stern_weapon) == 1) {      // 05bd:271d
        gun_pieces_16x3();
    } else {                                // 05bd:276a
        colour(DS_gun_dark_colour);
        gfx_move_to(0xB0, 0x79);
        gfx_draw_bitmap(DS_gun_sprite_f3a2, 1, 1);
        gfx_move_to(0x98, 0x79);
        gfx_draw_bitmap(DS_gun_sprite_f5ae, 1, 1);
    }
    draw_page(dst_page);
    if (ds_u8(DS_view_sky_top) > 0x21) ds_u8(DS_view_sky_top) = 0x21;
    view_rows_copy(0x6C, 0x38, src_page, dst_page);
    if (ds_u8(DS_stern_weapon) != 0) return view_copy_6(src_page, dst_page, si);
    return view_copy_8(src_page, dst_page, si);
}

// One barrel of the bow weapon 0 (05bd:28bd, 29e8): the muzzle flash while its counter runs (the
// white part and the flash colour at (flash_x, 7Ah)), the piece at (piece_x, 7Ah), then the barrel's
// three layers at (barrel_x, 7Fh), in its recoil frame (51h bytes further) while the flash runs.
void bow_barrel_draw(u16 flash_counter, u16 flash_x, u16 flash_white, u16 flash_red, u16 piece_x, u16 piece_a,
                     u16 piece_b, u16 barrel_x, u16 shade, u16 edge, u16 dark)
{
    u16 recoil;  // [bp-2]
    if (ds_u8(flash_counter) != 0) {
        recoil = 0x51;
        gfx_move_to(s16(flash_x), 0x7A);
        gfx_set_colour(0x0F);
        gfx_draw_bitmap(flash_white, 3, 7);
        colour(DS_muzzle_flash_colour);
        gfx_draw_bitmap(flash_red, 3, 7);
    } else {
        recoil = 0;
    }
    gfx_move_to(s16(piece_x), 0x7A);
    colour(DS_gun_edge_colour);
    gfx_draw_bitmap(piece_a, 1, 4);
    colour(DS_gun_shade_colour);
    gfx_draw_bitmap(piece_b, 1, 4);
    gfx_move_to(s16(barrel_x), 0x7F);
    gfx_draw_bitmap(u16(recoil + shade), 3, 9);
    colour(DS_gun_edge_colour);
    gfx_draw_bitmap(u16(recoil + edge), 3, 9);
    colour(DS_gun_dark_colour);
    gfx_draw_bitmap(u16(recoil + dark), 3, 9);
}

// 05bd:2836: station 2 (bow).
u16 present_bow(u16 src_page, u16 dst_page, u16 si)
{
    draw_page(src_page);
    gun_frame_draw(u16(ds_u8(u16(DS_heading + 1)) - ds_u8(DS_heading) - 0x60));
    if (ds_u8(DS_bow_weapon) == 0) {        // two barrels
        gfx_move_to(0x40, 0x6B);
        colour(DS_gun_edge_colour);
        gfx_draw_bitmap(DS_gun_sprite_ee94, 1, 5);
        gfx_move_to(0x108, 0x6B);
        gfx_draw_bitmap(DS_gun_sprite_eca9, 1, 5);
        bow_barrel_draw(DS_flash_bow, 0x88, DS_gun_sprite_f5e8, DS_gun_sprite_f5fd, 0x60, DS_gun_sprite_f0f0,
                        DS_gun_sprite_f0f4, 0x78, DS_gun_sprite_e9fd, DS_gun_sprite_e9e2, DS_gun_sprite_ea18);
        bow_barrel_draw(u16(DS_flash_bow + 1), 0xB0, DS_gun_sprite_eea6, DS_gun_sprite_eebb, 0xE8, DS_gun_sprite_f5dc,
                        DS_gun_sprite_f5e0, 0xC0, DS_gun_sprite_f2bf, DS_gun_sprite_f2a4, DS_gun_sprite_f2da);
    } else {                                // 05bd:2b0c
        gfx_move_to(0xB0, 0x7F);
        colour(DS_gun_dark_colour);
        gfx_draw_bitmap(DS_gun_sprite_f5dc, 1, 3);
        gfx_set_colour(0);
        gfx_draw_bitmap(DS_gun_sprite_f5df, 1, 3);
        gfx_move_to(0x98, 0x7F);
        gfx_draw_bitmap(DS_gun_sprite_f0f4, 1, 4);
        colour(DS_gun_dark_colour);
        gfx_draw_bitmap(DS_gun_sprite_f0f0, 1, 4);
        gfx_move_to(0x98, 0x7B);
        gfx_draw_bitmap(DS_gun_sprite_e9e2, 4, 0x12);
        colour(DS_gun_rail_colour);
        gfx_draw_bitmap(DS_gun_sprite_ea2a, 4, 0x12);
        if (ds_u8(DS_flash_bow) != 0) {
            gfx_move_to(0xA0, 0x7B);
            gfx_set_colour(0x0F);
            gfx_draw_bitmap(DS_gun_sprite_f5e8, 2, 0x0A);
            colour(DS_muzzle_flash_colour);
            gfx_draw_bitmap(DS_gun_sprite_f5fc, 2, 0x0A);
        }
    }
    draw_page(dst_page);  // 05bd:2c42
    // view_rows_copy inline, without the clamp of view_sky_top and with the bottom row by the weapon
    const u8 top = view_rows_top();
    if (ds_u8(DS_bow_weapon) == 0) {
        if (top <= 0x6B) gfx_copy_rect(0x28, 0x127, ds_u8(DS_view_sky_top_prev), 0x6B, 0x20, 0x37, src_page, dst_page);
    } else {
        if (top <= 0x6C) gfx_copy_rect(0x28, 0x127, ds_u8(DS_view_sky_top_prev), 0x6C, 0x20, 0x38, src_page, dst_page);
    }
    ds_u8(DS_view_sky_top_prev) = ds_u8(DS_view_sky_top);
    if (ds_u8(DS_bow_weapon) == 0) return view_copy_4(src_page, dst_page, si);
    return view_copy_7(src_page, dst_page, si);
}

} // namespace

// 05bd:1fd4 view_present (hud.md §6; world.md §3.2-3.3): the present of the 3D stations 1-4. The
// pilot (1) gets the view through the windscreen of the look direction (0 left, 1 ahead, 2 right);
// the gun stations first draw the gun frame and their weapon's sprites (sight, barrels, muzzle
// flashes by the flash counters) on the source page, then copy. Other stations and look directions:
// nothing. Returns SI: the view copy's end offset (the copies do not keep SI), else the caller's.
u16 view_present(u16 src_page, u16 dst_page, u16 si)
{
    switch (ds_u16(DS_station)) {
    case 1:
        switch (ds_u16(DS_look_direction)) {
        case 0:
            pilot_rows_copy(0x78, 0xF7, 0x80, 0xF8, 0x127, 0x110, src_page, dst_page);
            return view_copy_1(src_page, dst_page, si);
        case 1:
            pilot_rows_copy(0x28, 0xA7, 0x18, 0xA8, 0x127, 0xA8, src_page, dst_page);
            return view_copy_2(src_page, dst_page, si);
        case 2:
            pilot_rows_copy(0x28, 0x57, 0x00, 0x58, 0xD7, 0x40, src_page, dst_page);
            return view_copy_3(src_page, dst_page, si);
        default:
            return si;
        }
    case 2: return present_bow(src_page, dst_page, si);
    case 3: return present_midship(src_page, dst_page, si);
    case 4: return present_stern(src_page, dst_page, si);
    default: return si;
    }
}

} // namespace gb
