// The mission loop and the station screens (world.md §3, hud.md §5), segment 05bd: mission_run,
// the 3D stations' screens (pilot, bow, stern, midship, chase view) and view_restore.
#include "mission/mission.hpp"
#include "mission/mission_load.hpp"

#include "game/flow.hpp"
#include "game/flow_util.hpp"
#include "game/sim.hpp"
#include "host.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "render/render.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

using namespace flow;

namespace {

// A cockpit panel: the picture of the far buffer at DS:far_ds decoded into DS:1094 and drawn with
// its bottom-left corner at (x, C7h), `runs` (a word at DS:runs_ds) runs of `width` pixels.
void panel(u16 x, u16 far_ds, u16 runs_ds, u16 width)
{
    gfx_move_to(s16(x), 0xC7);
    decode(far_ds);
    picture_draw(PIC, s16(ds_u16(runs_ds)), width);
}

// A full-width cockpit picture: VGA draws it with picture_draw_vga at bottom row y.
void full_picture(u16 far_ds, u16 runs_ds, u16 y)
{
    decode(far_ds);
    if (ds_u16(DS_video_mode) != 0x13) picture_draw(PIC, s16(ds_u16(runs_ds)), 0x140);
    else picture_draw_vga(PIC, ds_u16(runs_ds), y);
}

// The pixels of one colour in a rectangle at the pen, captured as a bit mask (gfx_read_bitmap).
void grab(u16 dst, u16 bytes_per_row, u16 rows) { gfx_read_bitmap(dst, bytes_per_row, rows); }

void colour_of(u16 ds_byte) { gfx_set_colour(s16(ds_u8(ds_byte))); }

// The end of every 3D station's screen.
u16 station_end(u16 si, u16 message)
{
    si = view_restore(si);
    if (ds_u16(DS_single_page_mode) == 0) gfx_copy_rect_from_copy_page(0, 0x13F, 0, 0xC7);
    pal_apply_vga();
    show_message_far(message);
    return si;
}

} // namespace

// 05bd:048c pilot_screen (hud.md §5): the pilot's cockpit for the look direction (DS:F346): three
// panels of the BD pictures, the lamp and switch positions of the look (boat record D531..D5A3),
// the jet indicator, then the 3D view (view_restore) and "Pilot station" (message 29h).
u16 pilot_screen(u16 si)
{
    pal_black_vga();
    set_draw_page(ds_u16(DS_view_page));
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 9 || mode == 0x0D) {
        ega_pal_entry(0x14, 0x277);
        ega_pal_entry(0x15, 0x288);
    }
    switch (ds_u16(DS_look_direction)) {
    case 0:  // left
        ds_u16(0xD5A3) = 0x1118;
        ds_u16(0xD5A1) = 0x2418;
        ds_u16(0xD58B) = 0x1712;
        ds_u16(0xD58D) = 0x1012;
        ds_u16(0xD58F) = 0x1512;
        ds_u16(0xD593) = 0;
        ds_u16(0xD591) = 0;
        ds_u16(0xD531) = 0x1812;
        ds_u16(0xD533) = 0x1112;
        ds_u16(0xD535) = 0x1412;
        panel(0, 0xF0FC, 0xF136, 0x68);
        panel(0x68, 0xF102, 0xF138, 0x68);
        panel(0xD0, 0xF63C, 0xF100, 0x70);
        break;
    case 1:  // ahead
        ds_u16(0xD5A3) = 0x0418;
        ds_u16(0xD5A1) = 0x1718;
        ds_u16(0xD58B) = 0x0A12;
        ds_u16(0xD58D) = 0x0312;
        ds_u16(0xD58F) = 0x0812;
        ds_u16(0xD593) = 0;
        ds_u16(0xD591) = 0;
        ds_u16(0xD531) = 0x0B12;
        ds_u16(0xD533) = 0x0412;
        ds_u16(0xD535) = 0x0712;
        panel(0, 0xF102, 0xF138, 0x68);
        panel(0x68, 0xF63C, 0xF100, 0x70);
        panel(0xD8, 0xF622, 0xF0FA, 0x68);
        break;
    case 2:  // right
        ds_u16(0xD5A3) = 0x0018;
        ds_u16(0xD5A1) = 0x0A18;
        ds_u16(0xD58F) = 0;
        ds_u16(0xD58D) = 0;
        ds_u16(0xD58B) = 0;
        ds_u16(0xD591) = 0x2510;
        ds_u16(0xD593) = 0x2513;
        ds_u16(0xD535) = 0;
        ds_u16(0xD533) = 0;
        ds_u16(0xD531) = 0;
        panel(0, 0xF63C, 0xF100, 0x70);
        panel(0x70, 0xF622, 0xF0FA, 0x68);
        gfx_move_to(0xD8, 0xC7);
        decode(0xF61E);
        if (ds_u16(DS_video_mode) == 4) {
            ega_pal_entry(8, 0x200);
            ega_pal_entry(0x15, 0x2AA);
        }
        picture_draw(PIC, s16(ds_u16(0xF0F8)), 0x68);
        break;
    default: break;
    }
    ega_pal_apply();
    jet_indicator_draw(0);  // (its AX input only passes through; at the pilot's station it returns 0)
    return station_end(si, 0x29);
}

// 05bd:08be bow_screen (hud.md §5): the bow gunner's cockpit (the twin M2HB or the Minigun by
// B804), the gun panel, and the gun sprites captured from the drawn pictures, colour by colour.
u16 bow_screen(u16 si)
{
    set_draw_page(ds_u16(DS_view_page));
    pal_black_vga();
    gfx_move_to(0, 0x4F);
    if (ds_u8(0xB804) == 0) {
        ds_u16(0xD5A3) = 0x1018;
        ds_u16(0xD5A1) = 0x1518;
        const u16 mode = ds_u16(DS_video_mode);
        if (ds_u8(0xB7FC) == 0 && (mode == 0x0D || mode == 9)) {
            ds_u8(0xEEA0) = 8;
            ega_pal_entry(0x14, 0x277);
            ega_pal_entry(0x15, 0x288);
        }
    } else {
        ds_u16(0xD5A3) = 0x2212;
        ds_u16(0xD5A1) = 0x2512;
        ds_u16(0xD595) = 0x220F;
        ds_u16(0xD597) = 0x250F;
        ds_u16(0xD599) = 0;
        ds_u16(0xD59B) = 0x270F;
        ds_u16(0xD559) = 0x250E;
        ds_u16(0xD55B) = 0;
        ds_u16(0xD55D) = 0x270E;
        ds_u16(0xD55F) = 0x220E;
    }
    full_picture(0xF5C6, 0xEEA4, 0x4F);
    gun_panel_copy(ds_u8(0xD50E));
    gfx_move_to(0, 0xC7);
    full_picture(0xF5BE, 0xEE9E, 0xC7);
    ega_pal_apply();
    gun_sprites_capture();
    if (ds_u8(0xB804) == 0) {  // the twin M2HB
        gfx_move_to(0x38, 0x37);
        colour_of(0xEEA0);
        grab(0xEE94, 1, 5);
        gfx_move_to(0x100, 0x37);
        grab(0xECA9, 1, 5);
        gfx_move_to(0x58, 0x46);
        grab(0xF0F0, 1, 4);
        colour_of(0xF108);
        grab(0xF0F4, 1, 4);
        gfx_move_to(0x70, 0x4B);
        grab(0xE9FD, 3, 9);
        colour_of(0xF107);
        grab(0xE9E2, 3, 9);
        colour_of(0xECAE);
        grab(0xEA18, 3, 9);
        gfx_move_to(0x70, 0x3B);
        grab(0xEA69, 3, 9);
        colour_of(0xF107);
        grab(0xEA33, 3, 9);
        colour_of(0xF108);
        grab(0xEA4E, 3, 9);
        gfx_move_to(0xE0, 0x46);
        grab(0xF5E0, 1, 4);
        colour_of(0xF107);
        grab(0xF5DC, 1, 4);
        gfx_move_to(0xB8, 0x4B);
        grab(0xF2A4, 3, 9);
        colour_of(0xF108);
        grab(0xF2BF, 3, 9);
        colour_of(0xECAE);
        grab(0xF2DA, 3, 9);
        gfx_move_to(0xB8, 0x3B);
        grab(0xF32B, 3, 9);
        colour_of(0xF107);
        grab(0xF2F5, 3, 9);
        colour_of(0xF108);
        grab(0xF310, 3, 9);
        gfx_move_to(0x80, 0x36);
        colour_of(0xECB2);
        grab(0xF5E8, 3, 7);
        colour_of(0xECAF);
        grab(0xF5FD, 3, 7);
        gfx_move_to(0xA8, 0x36);
        grab(0xEEBB, 3, 7);
        colour_of(0xECB2);
        grab(0xEEA6, 3, 7);
    } else {  // the Minigun
        gfx_move_to(0xA8, 0x4B);
        colour_of(0xECAE);
        grab(0xF5DC, 1, 3);
        gfx_set_colour(0);
        grab(0xF5DF, 1, 3);
        gfx_move_to(0x90, 0x4B);
        grab(0xF0F4, 1, 4);
        colour_of(0xECAE);
        grab(0xF0F0, 1, 4);
        gfx_move_to(0x90, 0x47);
        grab(0xE9E2, 4, 0x12);
        colour_of(0xF10A);
        grab(0xEA2A, 4, 0x12);
        gfx_move_to(0x98, 0x47);
        colour_of(0xECB2);
        grab(0xF5E8, 2, 0x0A);
        colour_of(0xECAF);
        grab(0xF5FC, 2, 0x0A);
    }
    return station_end(si, 0x2A);
}

// 05bd:0e9e stern_screen (hud.md §5): the stern gunner's cockpit (M129 grenade launcher or M60D by
// B806), the gun panel and the sprites.
u16 stern_screen(u16 si)
{
    ds_u16(0xD5A3) = 0x2016;
    ds_u16(0xD5A1) = 0x2316;
    set_draw_page(ds_u16(DS_view_page));
    pal_black_vga();
    gfx_move_to(0, 0x57);
    full_picture(0xF616, 0xEE9A, 0x57);
    gun_panel_copy(ds_u8(0xD50C));
    gun_sprites_capture();
    gfx_move_to(0x98, 0x45);
    colour_of(0xECAE);
    grab(0xE9E2, 2, 0x19);
    colour_of(0xF10A);
    grab(0xEA14, 2, 0x19);
    gfx_move_to(0x98, 0x43);
    colour_of(0xECAF);
    grab(0xF2A4, 2, 0x0E);
    colour_of(0xECB2);
    grab(0xF2C0, 2, 0x0E);
    if (ds_u8(0xB806) == 1) {
        gfx_move_to(0x98, 0x45);
        colour_of(0xF108);
        grab(0xF3A2, 2, 3);
        colour_of(0xF107);
        grab(0xF3A8, 2, 3);
    } else {
        colour_of(0xECAE);
        gfx_move_to(0xA8, 0x45);
        grab(0xF3A2, 1, 1);
        gfx_move_to(0x90, 0x45);
        grab(0xF5AE, 1, 1);
    }
    gfx_move_to(0, 0xC7);
    full_picture(0xF5D0, 0xEED8, 0xC7);
    return station_end(si, 0x28);
}

// 05bd:10f4 midship_screen (hud.md §5): the midship gunner's cockpit (the mortar, the M2HB or the
// M60D by B807), the gun panel and the sprites.
u16 midship_screen(u16 si)
{
    ds_u8(0xEEA1) = ds_u8(0xF108);
    if (ds_u8(0xB807) != 0) {
        ds_u16(0xD5A3) = 0x2016;
        ds_u16(0xD5A1) = 0x2316;
    } else {
        ds_u16(0xD5A3) = 0x2116;
        ds_u16(0xD5A1) = 0x2416;
    }
    set_draw_page(ds_u16(DS_view_page));
    pal_black_vga();
    gfx_move_to(0, 0x57);
    full_picture(0xF5CC, 0xEED0, 0x57);
    gun_panel_copy(ds_u8(0xD50F));
    gun_sprites_capture();
    gfx_move_to(0x98, 0x45);
    colour_of(0xECAE);
    grab(0xE9E2, 2, 0x19);
    colour_of(0xF10A);
    grab(0xEA14, 2, 0x19);
    gfx_move_to(0x98, 0x43);
    colour_of(0xECAF);
    grab(0xF2A4, 2, 0x0E);
    colour_of(0xECB2);
    grab(0xF2C0, 2, 0x0E);
    switch (ds_u8(0xB807)) {
    case 0:  // the mortar
        gfx_move_to(0xA8, 0x4B);
        colour_of(0xECAE);
        grab(0xF3A2, 1, 4);
        colour_of(0xF107);
        grab(0xF3A6, 1, 4);
        gfx_move_to(0x90, 0x4B);
        grab(0xF5B4, 1, 6);
        colour_of(0xECAE);
        grab(0xF5AE, 1, 6);
        break;
    case 1: {  // the M2HB
        const u16 mode = ds_u16(DS_video_mode);
        if (mode == 0x0D || mode == 9) ds_u8(0xEEA1) = 7;
        gfx_move_to(0xB0, 0x4B);
        colour_of(0xEEA1);
        grab(0xF3A2, 1, 5);
        gfx_move_to(0x88, 0x4B);
        colour_of(0xF107);
        grab(0xF5B3, 1, 5);
        colour_of(0xF106);
        grab(0xF5AE, 1, 5);
        break;
    }
    default:  // the M60D
        gfx_move_to(0x98, 0x45);
        colour_of(0xF108);
        grab(0xF3A2, 2, 3);
        colour_of(0xF107);
        grab(0xF3A8, 2, 3);
        break;
    }
    gfx_move_to(0, 0xC7);
    full_picture(0xF5C2, 0xEED4, 0xC7);
    return station_end(si, 0x2B);
}

// 05bd:07da chase_view_screen (world.md §3.2): the chase view: coming from a full-screen station
// the top rows are cleared and "Chase view" shown (message 25h); the text area below the view is
// cleared, the mission's score line printed (DS:D2DC) with its figures, then the view restored.
u16 chase_view_screen(u16 si)
{
    if (s16(ds_u16(0xF21C)) >= 5) {
        set_draw_page(0);
        gfx_set_colour(0);
        gfx_fill_rect(0, 0x13F, 0, 0x0B);
        text_set_colours(0x0F, 0);
        show_message_far(0x25);
    }
    set_draw_page(0);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13F, 0x0C, 0xC7);
    text_set_colours(4, 0);
    u16 off = print_records(0xD2DC, 0);
    text_set_colours(0x0F, 0);
    print_records(0xD2DC, off);
    bcd_stats_print(0xD35F, 0xD373);
    return view_restore(si);
}

// 05bd:144a view_restore (world.md §3.3): after a station screen: the world B data back into
// DGROUP (the full-screen stations use it as scratch), the sprite cache invalidated, the panel
// lamps and switches and the window cracks redrawn, one frame simulated and presented. SI is
// the caller's (the station screens pass mission_run's on), for game_frame.
u16 view_restore(u16 si)
{
    far_to_near_copy(ds_far(0xF5D4), 0x53A8, ds_u16(0xF396));
    set_draw_page(ds_u16(DS_view_page));
    text_set_colours(0x0F, 0);
    sprite_cache_invalidate();
    u16 ax = panel_redraw_all(0);  // sprite_cache_invalidate leaves AX = 0 (sprite_slots_reset)
    panel_switches_redraw(ax);
    window_cracks_draw();
    ds_u8(0xEA86) = ds_u8(0xF10B);
    ds_u8(0xECA8) = ds_u8(0xF132);
    ds_u8(0xF10B) = ds_u8(0xB83C);
    ds_u8(0xF132) = ds_u8(0xB839);
    game_frame(si);
    if (ds_u8(DS_chase_view) == 0) si = view_present(1, ds_u16(DS_view_page), si);
    set_draw_page(0);
    return si;
}

// 05bd:000a mission_run (world.md §3): the mission: its text, the screen colours, the scores and the
// fuel reset, the mission loaded, the starting station drawn; then the loop until station 9: a
// station's screen when the station, the chase view or the look direction changed, the map
// markers, one key, one frame simulated and presented, the key's action (a second key when time
// is compressed), one random() and, on the full-screen stations, the wait for 3 ticks. SI is
// a register the loop keeps: the text pointer after the copy, the map's objective offset, what
// key_dispatch leaves; game_frame and the station screens get it (caller_si).
void mission_run()
{
    ds_u16(DS_map_a_far) = u16(ds_u16(DS_bow_art1_far) + 0x0B54);  // offset only
    ds_u16(u16(DS_map_a_far + 2)) = ds_u16(u16(DS_bow_art1_far + 2));
    ds_u16(DS_small_far) = u16(ds_u16(DS_bow_art1_far) + 0x170C);
    ds_u16(u16(DS_small_far + 2)) = ds_u16(u16(DS_bow_art1_far + 2));
    engine_sound_off();
    u16 table;
    switch (ds_u16(DS_region)) {
    case 1: table = 0xB2EB; break;
    case 2: table = 0xB2FB; break;
    case 3: table = 0xB30B; break;
    default: table = 0xB2DB; break;
    }
    const u16 text = u16(ds_u16(u16(table + 2 * ds_u16(DS_mission_number))) + world_a_base());
    for (s16 i = 0; i < 0x1E0; i++) ds_u8(u16(0xECB4 + i)) = ds_u8(u16(text + i));
    u16 si = text;  // MOV SI, [BP-6] in the copy loop
    // the screen colours by video mode (hud.md §7): data only, all modes
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 4) {
        ds_u8(0xECAF) = 3; ds_u8(0xECB2) = 3; ds_u8(0xF146) = 2; ds_u8(0xF109) = 2; ds_u8(0xECAE) = 2;
        ds_u8(0xF106) = 3; ds_u8(0xF107) = 0; ds_u8(0xF108) = 2; ds_u8(0xF10A) = 0;
    } else if (mode == 9 || mode == 0x0D) {
        ds_u8(0xECAF) = 0x0C; ds_u8(0xECB2) = 0x0E; ds_u8(0xF146) = 7; ds_u8(0xF109) = 8; ds_u8(0xECAE) = 8;
        ds_u8(0xF106) = 0x0F; ds_u8(0xF107) = 7; ds_u8(0xF108) = 8;
        ds_u8(0xF10A) = ds_u8(0xB7FC) == 0 ? 0 : 8;
    } else if (mode == 0x13) {
        ds_u8(0xECAF) = 0x0C; ds_u8(0xECB2) = 0x0E; ds_u8(0xF146) = 0x10; ds_u8(0xF109) = 0x17; ds_u8(0xECAE) = 8;
        ds_u8(0xF106) = 0x14; ds_u8(0xF107) = 0x15; ds_u8(0xF108) = 0x16; ds_u8(0xF10A) = 0x17;
    }
    ds_u16(0xEED6) = ds_u16(DS_object_x);  // the start position (the map's marker)
    ds_u16(0xF0E4) = ds_u16(DS_object_y);
    for (u16 i = 0; i < 4; i++) ds_u8(u16(0x0B4A + i)) = 0;  // window cracks
    for (u16 i = 0; i < 0x0C; i++) ds_u16(u16(DS_score_words + 2 * i)) = 0;
    ds_u8(0xB545) = 3;  // in progress
    ds_u8(0xB544) = 0;
    ds_u8(0xB546) = 0;
    ds_u8(0xB542) = 0;
    ds_u8(0xB549) = 0;
    ds_u8(0xB800) = 0;
    ds_u8(0xB828) = 0;
    ds_u16(0xB80C) = 0xC544;  // fuel
    ds_u16(0xB80A) = 0xC544;
    mission_load();
    ds_u8(0xD69F) = 0;
    ds_u8(0xD96B) = 0;
    ds_u8(0xF5CA) = 0;
    screen_clear();
    ds_u16(0xF21C) = ds_u16(DS_station);
    ds_u16(0xF286) = ds_u16(DS_look_direction);
    ds_u8(0xEEA0) = ds_u8(0xF107);
    if (ds_u16(DS_station) == 1) si = pilot_screen(si);
    if (ds_u16(DS_station) == 2) si = bow_screen(si);
    if (ds_u16(DS_station) == 4) si = stern_screen(si);
    engine_sound_on();
    while (ds_u16(DS_station) != 9) {
        // PORT: the host's time and input for this pass (the original runs as fast as the PC draws;
        // host_frame_pace sets the 3D stations' frame rate; the tests count one tick here)
        if (s16(ds_u16(DS_station)) < 5) host_frame_pace();
        host_pump();
        const u16 start = ds_u16(DS_tick_counter);
        if (ds_u8(0xF5CA) != ds_u8(0xD96B) || ds_u16(0xF21C) != ds_u16(DS_station) ||
            ds_u16(0xF286) != ds_u16(DS_look_direction)) {
            ds_u8(0xEEA0) = ds_u8(0xF107);
            screen_clear();
            if (ds_u8(0xD96B) != 0) {
                si = chase_view_screen(si);
            } else {
                switch (ds_u16(DS_station)) {  // jump table 05bd:027E
                case 1: si = pilot_screen(si); break;
                case 2: si = bow_screen(si); break;
                case 3: si = midship_screen(si); break;
                case 4: si = stern_screen(si); break;
                case 5: map_screen(); break;
                case 6: si = chase_view_screen(si); break;
                case 7: damage_report_screen(); break;
                case 8: assignment_screen(); break;
                default: break;
                }
            }
            ds_u8(0xF5CA) = ds_u8(0xD96B);
            if (ds_u8(0xD96A) == 0) ds_u16(0xF21C) = ds_u16(DS_station);
            else ds_u8(0xD96A) = 0;
            ds_u16(0xF286) = ds_u16(DS_look_direction);
            set_draw_page(0);
        }
        if (ds_u16(DS_station) == 5) {  // the map: the objective and the boat, blinking
            gfx_set_colour(s16(ds_u16(DS_map_blink)++ & 3));
            if (ds_u16(DS_region) != 3) {
                si = u16(((ds_u16(DS_region) << 3) + ds_u16(DS_mission_number)) * 2);
                gfx_move_to(ds_u8(u16(0xB3C0 + si)), s16(u16(ds_u8(u16(0xB3C1 + si)) - 0x18)));
                gfx_draw_bitmap(DS_pencil_mask_tip, 1, 8);
            }
            if (ds_u16(DS_region) != 4) {
                const u8 row = u8(u8(ds_u16(DS_object_y) >> 7) << 1) ^ 0xFF;
                gfx_put_pixel(s16((ds_u16(DS_object_x) >> 6) + 0x18), u8(row + 0xB5));
                gfx_move_to(s16((ds_u16(DS_object_x) >> 6) + 0x15), u8(row + 0xB8));
            } else {  // (region 4 does not exist)
                const u8 row = u8(u8(ds_u16(0xF0E4) >> 7) << 1) ^ 0xFF;
                gfx_move_to(s16((ds_u16(0xEED6) >> 6) + 0x15), u8(row + 0xB8));
            }
            gfx_set_colour(s16((ds_u16(DS_map_blink) + 2) & 3));
            gfx_draw_bitmap(DS_pencil_mask_point, 1, 7);
        }
        input_read_key(&ds_u16(0xF39A));
        ds_u8(0xEA86) = ds_u8(0xF10B);
        ds_u8(0xECA8) = ds_u8(0xF132);
        ds_u8(0xF10B) = ds_u8(0xB83C);
        ds_u8(0xF132) = ds_u8(0xB839);
        game_frame(si);
        if (ds_u8(0xD96B) != 0) gfx_copy_rect(0x28, 0x127, 0x40, 0x7F, 0x28, 0x4B, 1, 0);
        else if (s16(ds_u16(DS_station)) < 5) si = view_present(1, 0, si);
        si = key_dispatch(si);
        if (ds_u16(DS_demo_mode) == 0 && ds_u8(0xB7F1) != 0) {  // time compressed: a second key
            input_read_key(&ds_u16(0xF39A));
            si = key_dispatch(si);
        }
        random();
        if (s16(ds_u16(DS_station)) >= 5) {
            for (;;) {  // at least 3 ticks per pass on the full-screen stations
                host_pump();
                if (u16(ds_u16(DS_tick_counter) - start) > 2) break;
                random();
            }
        }
    }
    gfx_set_display_offset(0, 0);
    ds_u16(DS_quiz_digit) = u16(ds_u16(DS_quiz_digit) + 0x14);
    sfx_play_far(0x0C);
    engine_sound_off();
}

} // namespace gb
