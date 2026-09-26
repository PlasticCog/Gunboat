// The station screens of segment 05bd (hud.md §5; world.md §3): the full-screen stations 5 (map),
// 7 (damage report) and 8 (assignment), the per-station reset (screen_clear), the gun art's
// captured sprites and frame, the gun panels and the window cracks.
#include "hud/hud.hpp"

#include "game/flow.hpp"
#include "game/flow_util.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

using flow::decode;
using flow::draw_full;
using flow::PIC;
using flow::set_draw_page;

namespace {

// The end the three full-screen stations share (05bd:1b6e, 1d7f, 1e96): back to page 0, the
// finished page copied from the copy page unless in single-page mode, and the palette.
void full_screen_present()
{
    set_draw_page(0);
    if (ds_u16(DS_single_page_mode) == 0) gfx_copy_rect_from_copy_page(0, 0x13F, 0, 0xC7);
    pal_apply_vga();
    ega_pal_apply();
}

// The clipboard (CLIP.LZ), shared by the damage report and the assignment screen (05bd:1bc4,
// 1dd8): decoded and drawn full-width to the bottom row C7h.
void clipboard_draw()
{
    decode(DS_clip_far);
    gfx_move_to(0, 0xC7);
    draw_full(ds_u16(DS_clip_runs), 0xC7);
}

} // namespace

// 05bd:19cc map_screen (hud.md §5, station 5): the frame MAP.LZ and the region's two map sheets
// (MPnA at (28h, 5Dh), MPnB at (28h, A5h), 240 pixels wide) on the view page, then shown. The
// EGA/Tandy/CGA palette entries are ega_pal_entry calls (nothing in VGA).
void map_screen()
{
    station_screen_colours();
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 4) ega_pal_entry(7, 0x200);
    else if (mode == 9 || mode == 0x0D) ega_pal_entry(0x13, 0x2FF);
    set_draw_page(ds_u16(DS_view_page));
    pal_black_vga();
    decode(DS_pictures_far);
    gfx_move_to(0, 0xC7);
    draw_full(ds_u16(DS_pictures_runs), 0xC7);
    const u16 mode2 = ds_u16(DS_video_mode);
    if (mode2 == 4) {
        ega_pal_entry(0, 0x200);
        ega_pal_entry(2, 0x255);
        ega_pal_entry(3, 0x255);
        ega_pal_entry(9, 0x255);
        ega_pal_entry(0x0A, 0x2FF);
        ega_pal_entry(0x0E, 0x200);
        ega_pal_entry(0x0F, 0x2FF);
        ega_pal_entry(0x1A, 0x2AA);
    } else if (mode2 == 9 || mode2 == 0x0D) {
        ega_pal_entry(0x1A, 0x2AA);
    }
    decode(DS_clip_1838_far);  // in a mission: MPnA.LZ (world.md §4)
    gfx_move_to(0x28, 0x5D);
    picture_draw(PIC, s16(ds_u16(DS_map_sheet_a_runs)), 0xF0);
    decode(DS_map_sheet_b_far);
    gfx_move_to(0x28, 0xA5);
    picture_draw(PIC, s16(ds_u16(DS_map_sheet_b_runs)), 0xF0);
    full_screen_present();
}

// 05bd:1ba4 damage_report_screen (hud.md §5, station 7): on the clipboard, the title and the labels,
// then for each (column, row, lamp) of damage_report_list (up to a 0, 0 entry) the lamp's condition
// word (condition_words[state + (lamp < 10h ? 4 : 0)]: components -OK/-HIT/-OUT, crew
// -OK/-HURT/-DEAD), the bilge line by leak_level (0 pumps off, 1 on, else "SHIP IS SINKING!" in red)
// and the mission line by the objective lamp (state 2: not done).
void damage_report_screen()
{
    set_draw_page(ds_u16(DS_view_page));
    pal_black_vga();
    station_screen_colours();
    clipboard_draw();
    text_set_colours(4, 7);
    print_records(DS_damage_report_title, 0);
    text_set_colours(0, 7);
    print_records(ds_u16(DS_damage_report_labels), 0);
    const u16 list = ds_u16(DS_damage_report_list);
    for (u16 i = 0;; i = u16(i + 3)) {
        const u16 si = u16(i + list);
        const u16 col = ds_u8(si), row = ds_u8(u16(si + 1));
        if (u16(row + col) == 0) break;
        text_goto_cell(s16(row), s16(col));
        const u16 lamp = ds_u8(u16(si + 2));
        u16 state = ds_u8(u16(DS_boat_record + lamp)) & 3;
        if (lamp < 0x10) state = u16(state + 4);
        print_text(ds_u16(u16(DS_condition_words + 2 * state)), 0);
    }
    text_set_colours(0, 7);
    const u8 leak = ds_u8(DS_leak_level);
    if (leak == 0) {
        print_records(DS_text_pumps_off, 0);
    } else if (leak == 1) {
        print_records(DS_text_pumps_on, 0);
    } else {
        text_set_colours(4, 7);
        print_records(DS_text_ship_sinking, 0);
    }
    text_set_colours(4, 7);
    if ((ds_u8(DS_objective_state) & 3) == 2) print_records(DS_text_mission_not_done, 0);
    else print_records(DS_text_mission_complete, 0);
    text_set_colours(0, 7);
    full_screen_present();
}

// 05bd:1dba assignment_screen (hud.md §5, station 8): on the clipboard, the title, the mission's
// text block (mission_text, world.md §6.2) and, for mission 1 outside the practice region, the
// practice note.
void assignment_screen()
{
    set_draw_page(ds_u16(DS_view_page));
    pal_black_vga();
    station_screen_colours();
    clipboard_draw();
    text_set_colours(4, 7);
    print_records(DS_assignment_title, 0);
    text_set_colours(0, 7);
    print_records(DS_mission_text, 0);
    if (ds_u16(DS_mission_number) == 1 && ds_u16(DS_region) != 3) print_records(DS_practice_note, 0);
    full_screen_present();
}

// 05bd:1ed0 gun_sprites_capture (hud.md §5): reads six 1-bit sprites (8 pixels wide) out of the gun
// station's art on the draw page, each where its pixels are in the colour given: the edges (45 rows)
// at (20h, 38h) / (118h, 38h) in gun_edge_mask_colour, the rails (30 rows) at (20h, 29h) /
// (118h, 29h) in gun_rail_colour, the frame pieces (32 rows) at (60h, 2Fh) / (58h, 2Fh) in
// gun_frame_mask_colour.
void gun_sprites_capture()
{
    gfx_move_to(0x20, 0x38);
    gfx_set_colour(ds_u8(DS_gun_edge_mask_colour));
    gfx_read_bitmap(DS_gun_edge_left, 1, 0x2D);
    gfx_move_to(0x118, 0x38);
    gfx_read_bitmap(DS_gun_edge_right, 1, 0x2D);
    gfx_move_to(0x20, 0x29);
    gfx_set_colour(ds_u8(DS_gun_rail_colour));
    gfx_read_bitmap(DS_gun_rail_left, 1, 0x1E);
    gfx_move_to(0x118, 0x29);
    gfx_read_bitmap(DS_gun_rail_right, 1, 0x1E);
    gfx_move_to(0x60, 0x2F);
    gfx_set_colour(ds_u8(DS_gun_frame_mask_colour));
    gfx_read_bitmap(DS_gun_frame_left, 1, 0x20);
    gfx_move_to(0x58, 0x2F);
    gfx_read_bitmap(DS_gun_frame_right, 1, 0x20);
}

// 05bd:2cde gun_frame_draw (hud.md §6): the gun frame for the gun's relative bearing (low byte of
// the argument). Within 30h of 40h, the left frame piece moves in from x = 20h + 2 * (30h - (b - 40h))
// with a second piece and fills behind it once far enough; within 30h above D0h, the right one
// likewise from the right; then the edge and rail sprites at both sides.
void gun_frame_draw(u16 bearing)
{
    gfx_set_colour(ds_u8(DS_gun_frame_colour));
    u16 v = u8(u8(bearing) - 0x40);
    if (s16(v) <= 0x30) {
        v = u16((0x30 - v) << 1);
        gfx_move_to(s16(v + 0x20), 0x7F);
        gfx_draw_bitmap(DS_gun_frame_left, 1, 0x20);
        if (s16(v) > 8) {
            gfx_move_to(s16(v + 0x18), 0x5F);
            gfx_draw_bitmap(DS_gun_frame_left, 1, 0x20);
            gfx_fill_rect(u16(v + 0x18), u16(v + 0x1F), 0x60, 0x7F);
            if (s16(v) > 0x10) gfx_fill_rect(0x28, u16(v + 0x18), 0x40, 0x7F);
        }
    }
    v = u8(u8(bearing) - 0xD0);
    if (s16(v) <= 0x30) {
        v = u16((v << 1) + 2);
        gfx_move_to(s16(u16(0x128 - v)), 0x7F);
        gfx_draw_bitmap(DS_gun_frame_right, 1, 0x20);
        if (s16(v) > 8) {
            gfx_move_to(s16(u16(0x130 - v)), 0x5F);
            gfx_draw_bitmap(DS_gun_frame_right, 1, 0x20);
            gfx_fill_rect(u16(0x130 - v), u16(0x137 - v), 0x60, 0x7F);
            if (s16(v) > 0x10) gfx_fill_rect(u16(0x138 - v), 0x127, 0x40, 0x7F);
        }
    }
    gfx_move_to(0x28, 0x6C);
    gfx_set_colour(ds_u8(DS_gun_edge_colour));
    gfx_draw_bitmap(DS_gun_edge_left, 1, 0x2D);
    gfx_move_to(0x120, 0x6C);
    gfx_draw_bitmap(DS_gun_edge_right, 1, 0x2D);
    gfx_move_to(0x28, 0x5D);
    gfx_set_colour(ds_u8(DS_gun_rail_colour));
    gfx_draw_bitmap(DS_gun_rail_left, 1, 0x1E);
    gfx_move_to(0x120, 0x5D);
    gfx_draw_bitmap(DS_gun_rail_right, 1, 0x1E);
}

// 05bd:2efc screen_clear (world.md §3.1): before a station screen is drawn: forgets the instruments'
// last drawn state (needles and levers FFh, jet marker 40h, and D645..D64A, D965/D966, D9AD for
// other parts of the frame) and blanks the message strip (rows 0..0Bh) of the view page.
void screen_clear()
{
    ds_u8(0xD965) = 0;  // DS:D965, D966: cleared (hud.md §6, the sight animation)
    ds_u8(0xD966) = 0;
    ds_u8(u16(DS_lever_last_x + 1)) = 0xFF;
    ds_u8(DS_lever_last_x) = 0xFF;
    ds_u8(0xD64A) = 3;     // DS:D64A = 3, D649 = FFh (simulation state, see world.md §8)
    ds_u8(0xD649) = 0xFF;
    ds_u16(u16(DS_throttle_needle_ends + 2)) = 0xFFFF;
    ds_u16(DS_throttle_needle_ends) = 0xFFFF;
    ds_u16(0xD647) = 0xFFFF;  // DS:D647, D645
    ds_u16(0xD645) = 0xFFFF;
    ds_u16(u16(DS_fuel_needle_ends + 2)) = 0xFFFF;
    ds_u16(DS_fuel_needle_ends) = 0xFFFF;
    ds_u8(DS_jet_marker_last) = 0x40;
    if (ds_u16(DS_station) != 0x0A) ds_u16(DS_terrain_rebuild) = 0xFFFF;
    gfx_set_colour(0);
    set_draw_page(ds_u16(DS_view_page));
    gfx_fill_rect(0, 0x13F, 0, 0x0B);
}

// 05bd:2f6a station_screen_colours: the palette registers of the full-screen stations in CGA (mode
// 4) and in Tandy / EGA (9, 0Dh), through ega_pal_entry; nothing in VGA.
void station_screen_colours()
{
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 4) {
        ega_pal_entry(4, 0x2FF);
        ega_pal_entry(7, 0x2FF);
        ega_pal_entry(8, 0x200);
        ega_pal_entry(0x0E, 0x2FF);
        ega_pal_entry(0x0F, 0x255);
        ega_pal_entry(0x13, 0x255);
        ega_pal_entry(0x14, 0x2AA);
        ega_pal_entry(0x15, 0x2BB);
        ega_pal_entry(0x16, 0x2AA);
        ega_pal_entry(0x17, 0x200);
        ega_pal_entry(0x19, 0x2AA);
        ega_pal_entry(0x1A, 0x255);
    } else if (mode == 9 || mode == 0x0D) {
        ega_pal_entry(0x13, 0x2EE);
        ega_pal_entry(0x14, 0x2FF);
        ega_pal_entry(0x15, 0x277);
        ega_pal_entry(0x16, 0x278);
        ega_pal_entry(0x17, 0x200);
        ega_pal_entry(0x19, 0x266);
        ega_pal_entry(0x1A, 0x266);
    }
}

// 05bd:30ae gun_panel_copy (hud.md §5): the gun station's side panels from page 1 (rows 0..20h) to
// the bottom row 2Ch of the draw page: parts 1 the left one (100h..11Fh to x 0), 2 the right one
// (120h..13Fh to x 120h) and then the left one. Nothing in chase view.
void gun_panel_copy(u16 parts)
{
    if (ds_u8(DS_chase_view) != 0) return;
    if (parts != 1) {
        if (parts != 2) return;
        gfx_copy_rect(0x120, 0x13F, 0, 0x20, 0x120, 0x2C, 1, ds_u16(DS_draw_page));
    }
    gfx_copy_rect(0x100, 0x11F, 0, 0x20, 0, 0x2C, 1, ds_u16(DS_draw_page));
}

// 05bd:3114 window_cracks_draw (hud.md §5; simulation.md §8.6): at the stations 1-4 (not in chase
// view), for each set bit of window_cracks[station] the crack picture (8 x 7 from page 1 at
// 100h + 8 * picture, rows 21h..27h) at the crack's cell from crack_table: 8 cracks per station from
// entry 0 (pilot), 18h / 30h (bow, by the bow weapon), 78h / 60h (midship), 48h / 60h (stern). At
// the pilot's station the panorama moves the cracks by 68h pixels for the side views; a column off
// the screen is skipped.
void window_cracks_draw()
{
    const u16 station = ds_u16(DS_station);
    if (s16(station) >= 5) return;
    if (ds_u8(DS_chase_view) != 0) return;
    u16 index;
    switch (station) {
    case 2: index = ds_u8(DS_bow_weapon) == 0 ? 0x18 : 0x30; break;
    case 3: index = ds_u8(DS_midship_weapon) == 0 ? 0x78 : 0x60; break;
    case 4: index = ds_u8(DS_stern_weapon) == 0 ? 0x48 : 0x60; break;
    default: index = 0; break;
    }
    for (u8 mask = 1; mask != 0; mask = u8(mask << 1), index = u16(index + 3)) {
        if (!(ds_u8(u16(DS_window_cracks + ds_u16(DS_station))) & mask)) continue;
        const u16 v = crack_table_entry(index);
        const u16 y = u16(((v >> 8) & 0x3F) << 3);
        const u16 picture = u16(((v >> 14) & 3) << 3);
        u16 x = u16(((v & 0x3F) << 3) - 8);
        if (ds_u16(DS_station) == 1 && ds_u16(DS_look_direction) == 0) x = u16(x + 0x68);
        if (ds_u16(DS_station) == 1 && ds_u16(DS_look_direction) == 2) x = u16(x - 0x68);
        if (x >= 0x140) continue;
        gfx_copy_rect(u16(picture + 0x100), u16(picture + 0x107), 0x21, 0x27, x, u16(y - 1), 1, ds_u16(DS_draw_page));
    }
}

} // namespace gb
