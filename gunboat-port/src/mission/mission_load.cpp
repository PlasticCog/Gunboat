// Mission loading (world.md §4, §5): mission_load reads the mission's files (the boat record, the
// region's world data, the mission record, TILE.BIN, the cockpit art, the palette, the lights and
// the maps) and calls mission_setup, which sets the mission's start state.
#include "mission/mission_load.hpp"

#include "game/flow.hpp"
#include "game/flow_util.hpp"
#include "game/sim.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "render/render.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

using namespace flow;

namespace {

// File names (DGROUP strings)
constexpr u16 S_DAT10 = 0x0B4E, S_DAT11 = 0x0B58;
constexpr u16 S_DAT2A = 0x0B62, S_DAT2B = 0x0B6C, S_DAT3A = 0x0B76, S_DAT3B = 0x0B80, S_DAT4A = 0x0B8A,
              S_DAT4B = 0x0B94, S_DAT1A = 0x0B9E, S_DAT1B = 0x0BA8;
constexpr u16 S_TILE = 0x0BB2, S_BD1 = 0x0BBB, S_BD2 = 0x0BC2, S_BD3 = 0x0BC9, S_BD4 = 0x0BD0, S_BD5 = 0x0BD7,
              S_CLIP = 0x0BDE;
constexpr u16 S_BG1 = 0x0BE6, S_BG2 = 0x0BED, S_BF1 = 0x0BF4, S_BF2 = 0x0BFB;         // bow
constexpr u16 S_B61 = 0x0C02, S_B62 = 0x0C09, S_BGR1 = 0x0C10, S_BGR2 = 0x0C18;       // stern
constexpr u16 S_BR1 = 0x0C20, S_BR2 = 0x0C27, S_BM1 = 0x0C2E, S_BM2 = 0x0C35,         // midship
              S_B61_MID = 0x0C3C, S_B62_MID = 0x0C43;
constexpr u16 S_TACTCOLR = 0x0C4A, S_LIGHTS = 0x0C57, S_MAP = 0x0C61;
constexpr u16 S_MPA = DS_map_sheet_names, S_MPB = u16(DS_map_sheet_names + 8);  // + 16 * region

// The muzzle flash counters as the cockpit last drew them (this frame's stern and midship F10B /
// F132, last frame's EA86 / ECA8; view_restore and view_present, world.md §3.3, hud.md §5).
constexpr u16 FLASH_A = 0xEA86, FLASH_B = 0xECA8, FLASH_C = 0xF10B, FLASH_D = 0xF132;

// A cockpit picture: the file into the far buffer whose pointer is at DS:far_ds, and its run count.
void art(u16 name, u16 far_ds, u16 runs_ds, u16 runs)
{
    file_load_far(name, ds_far(far_ds));
    ds_u16(runs_ds) = runs;
}

} // namespace

// 05bd:14d6 mission_load (world.md §4): the flash bytes cleared and the look ahead; the boat record
// (DAT11.DAT in practice), the region's world A data (DGROUP) and B data (far, copied into DGROUP at
// 53A8), the mission record, TILE.BIN; mission_setup; the colour-remapped B data copied back; the
// cockpit art for the fitted weapons with the run counts the station screens decode; the engine
// sound switched on and off around a fade to black; the 3D palette (darker at night); LIGHTS.LZ
// decoded into DS:1094 and drawn to page 1; MAP.LZ and the region's two map sheets.
void mission_load()
{
    ds_u8(DS_flash_midship) = 0;
    ds_u8(DS_flash_stern) = 0;
    ds_u8(FLASH_A) = 0;
    ds_u8(FLASH_B) = 0;
    ds_u8(FLASH_C) = 0;
    ds_u8(FLASH_D) = 0;
    ds_u16(DS_look_direction) = 1;
    // two separate tests (F110 is read again after the first load)
    if (ds_u16(DS_practice_mode) == 0) file_load_near(S_DAT10, DS_boat_record);
    if (ds_u16(DS_practice_mode) != 0) file_load_near(S_DAT11, DS_boat_record);

    const u16 region = ds_u16(DS_region);
    u16 b_name;
    if (region == 0) {
        file_load_near(S_DAT2A, DS_world_a_data);
        b_name = S_DAT2B;
    } else if (region == 1) {
        file_load_near(S_DAT3A, DS_world_a_data);
        b_name = S_DAT3B;
    } else if (region == 2) {
        file_load_near(S_DAT4A, DS_world_a_data);
        b_name = S_DAT4B;
    } else {
        file_load_near(S_DAT1A, DS_world_a_data);
        b_name = S_DAT1B;
    }
    file_load_far(b_name, ds_far(DS_world_b_far));
    ds_u16(DS_world_b_size) = u16(ds_u32(DS_archive_entry_size));
    file_load_near(u16(DS_mission_record_names + u16(ds_u16(DS_region) * 9)), DS_mission_record);
    far_to_near_copy(ds_far(DS_world_b_far), DS_world_b_data, ds_u16(DS_world_b_size));
    file_load_far(S_TILE, ds_far(DS_tile_bin_offset));
    ds_u16(DS_tile_bin_size) = u16(ds_u32(DS_archive_entry_size));
    mission_setup();
    near_to_far_copy(DS_world_b_data, ds_far(DS_world_b_far), ds_u16(DS_world_b_size));

    art(S_BD1, DS_bd1_far, DS_bd1_runs, 0x0FA8);
    art(S_BD2, DS_bd2_far, DS_bd2_runs, 0x1177);
    art(S_BD3, DS_small_far, DS_map_b_runs, 0x0FAD);
    art(S_BD4, DS_bd4_far, DS_mp5b_runs, 0x10C7);
    art(S_BD5, DS_bd5_far, DS_bd5_runs, 0x1081);
    art(S_CLIP, DS_clip_far, DS_clip_runs, 0x11C7);
    if (ds_u8(DS_bow_weapon) == 0) {
        art(S_BG1, DS_bow_art1_far, DS_mp5a_runs, 0x0C43);
        art(S_BG2, DS_bow_art2_far, DS_bow_art2_runs, 0x2518);
    } else {
        art(S_BF1, DS_bow_art1_far, DS_mp5a_runs, 0x097C);
        art(S_BF2, DS_bow_art2_far, DS_bow_art2_runs, 0x2865);
    }
    if (ds_u8(DS_stern_weapon) == 1) {
        art(S_B61, DS_clip_0c1c_far, DS_stern_art1_runs, 0x0BEE);
        art(S_B62, DS_stern_art2_far, DS_stern_art2_runs, 0x2035);
    } else {
        art(S_BGR1, DS_clip_0c1c_far, DS_stern_art1_runs, 0x0CCB);
        art(S_BGR2, DS_stern_art2_far, DS_stern_art2_runs, 0x1EB2);
    }
    const u8 midship = ds_u8(DS_midship_weapon);
    if (midship == 0) {
        art(S_BM1, DS_map_a_far, DS_map_a_runs, 0x0CA2);
        art(S_BM2, DS_midship_art2_far, DS_midship_art2_runs, 0x29C4);
    } else if (midship == 1) {
        art(S_BR1, DS_map_a_far, DS_map_a_runs, 0x0D17);
        art(S_BR2, DS_midship_art2_far, DS_midship_art2_runs, 0x26EA);
    } else {
        art(S_B61_MID, DS_map_a_far, DS_map_a_runs, 0x0BEE);
        art(S_B62_MID, DS_midship_art2_far, DS_midship_art2_runs, 0x2035);
    }

    engine_sound_on();
    pal_fade_out_vga();
    engine_sound_off();
    file_load_near(S_TACTCOLR, DS_palette_3d);
    if (ds_u8(DS_daylight) == 0) {  // night: two ranges of palette bytes darker by 8
        for (u16 i = 0x15; i < 0x1B; i++) ds_u8(u16(DS_palette_3d + i)) = u8(ds_u8(u16(DS_palette_3d + i)) - 8);
        u16 i = 0x39;
        do {
            ds_u8(u16(DS_palette_3d + i)) = u8(ds_u8(u16(DS_palette_3d + i)) - 8);
            i++;
        } while (i < 0x48);
        ds_u16(u16(DS_palette_3d + 0x8E)) = 0x0200;  // DS:0952: colour 2Fh's green 0, blue 2
    }
    ega_pal_init();
    set_draw_page(1);
    file_load_far(S_LIGHTS, ds_far(DS_pictures_far));
    ds_u16(DS_pictures_runs) = 0x2EB5;
    decode(DS_pictures_far);  // into DS:1094, the terrain vertex arrays (filled later)
    gfx_move_to(0, 0xC7);
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 9 || mode == 0x0D) ega_pal_entry(0x14, 0x277);
    draw_full(ds_u16(DS_pictures_runs), 0xC7);
    ega_pal_apply();
    file_load_far(S_MAP, ds_far(DS_pictures_far));
    ds_u16(DS_pictures_runs) = 0x0F09;
    file_load_far(u16(S_MPA + u16(ds_u16(DS_region) << 4)), ds_far(DS_clip_1838_far));
    ds_u16(DS_map_sheet_a_runs) = ds_u16(u16(DS_map_sheet_sizes + u16(ds_u16(DS_region) << 2)));
    file_load_far(u16(S_MPB + u16(ds_u16(DS_region) << 4)), ds_far(DS_map_sheet_b_far));
    ds_u16(DS_map_sheet_b_runs) = ds_u16(u16(DS_map_sheet_sizes + 2 + u16(ds_u16(DS_region) << 2)));
}

// 0919:3d78 mission_setup (world.md §5): the far-object thinning word (a random number with 4 to 8
// bits set), the demo's start position, the sprite cache segments, the view row table; the boat's
// and the crew's start state (engines, throttles, headings, guns, reloads, bob, timers); the
// practice start (engines running at a throttle by practice mode); the missile speed of the region;
// the crew pilot searching for the route; the mission's objective objects; the practice targets
// (every authored object becomes a "sleezy lawyer", kind 16h, flags 0); the throttle limit; the
// clock and deadline of the mission type and the time of day; the colour remap of the world data.
void mission_setup()
{
    u16 r;
    for (;;) {
        r = random();
        u8 bits = 0;
        for (u16 b = r; b != 0; b = u16(b << 1))
            if (b & 0x8000) bits++;
        if (bits >= 4 && bits <= 8) break;
    }
    ds_u16(DS_far_object_mask) = r;
    if (ds_u8(DS_demo_mode) != 0) {  // the demo starts at a fixed place (byte test)
        ds_u16(DS_object_x) = 0x0B00;
        ds_u16(DS_object_y) = 0x0FF0;
    }
    ds_u16(DS_sprite_segment_a) =
        u16(u16((ds_u16(DS_sprite_cache_a_far) >> 4) + 1) + ds_u16(u16(DS_sprite_cache_a_far + 2)));
    ds_u16(DS_sprite_segment_b) =
        u16(u16((ds_u16(DS_sprite_cache_b_far) >> 4) + 1) + ds_u16(u16(DS_sprite_cache_b_far + 2)));
    video_mode_setup();
    ds_u8(0xD67D) = 1;  // DS:D67D..D67F: the crew gunners' sweep memories (simulation.md §6.2)
    ds_u8(0xD67E) = 1;
    ds_u8(0xD67F) = 1;
    ds_u8(DS_boat_lost_pending) = 0;
    ds_u8(DS_shake_counter) = 0;
    ds_u8(DS_fast_forward_key) = 0;
    ds_u8(DS_leak_level) = 0;
    ds_u8(DS_water_level) = 0;
    ds_u8(DS_time_compression) = 0;
    ds_u8(DS_engine_state) = 0;
    ds_u8(u16(DS_engine_state + 1)) = 0;
    ds_u8(DS_throttle) = 0;
    ds_u8(u16(DS_throttle + 1)) = 0;
    ds_u8(DS_heading) = 0;                         // hull
    ds_u8(DS_heading_fraction) = 0;
    ds_u8(u16(DS_heading + 1)) = 0;                // bow gun
    ds_u8(u16(DS_heading_fraction + 1)) = 0;
    ds_u8(u16(DS_heading + 2)) = 0x80;             // midship gun
    ds_u8(u16(DS_heading_fraction + 2)) = 0;
    ds_u8(u16(DS_heading + 3)) = 0x80;             // stern gun
    ds_u8(u16(DS_heading_fraction + 3)) = 0;
    ds_s8(DS_bob_velocity) = 0;
    ds_u8(DS_crew_pilot_state) = 0;
    ds_u16(DS_fire_at_will) = 0;
    ds_u16(DS_visible_count) = 0;
    ds_u8(DS_screen_shake) = 0;
    ds_u8(DS_incoming_count) = 0;
    ds_u8(0xD649) = 0xFF;  // DS:D649 (also set by screen_clear; world.md §8)
    ds_u8(DS_daylight) = 0xFF;  // forces time_of_day to set the colours
    ds_u8(DS_pitch_reference) = 0x80;
    ds_u8(u16(DS_bob_position + 1)) = 0x7F;  // DS:B82C, the bob position's high byte (camera pitch)
    ds_u8(DS_jet_angle) = 0x40;
    ds_u8(DS_reload_stern) = 8;
    ds_u8(DS_reload_midship) = 0x30;
    ds_u16(DS_quiz_digit) = u16(ds_u16(DS_quiz_digit) - 6);
    ds_u8(DS_elevation_stern) = 0xC0;
    ds_u8(DS_elevation_midship) = 0xC0;
    ds_u8(DS_elevation_bow) = 0xC0;
    ds_u8(DS_bob_countdown) = 8;
    ds_u8(DS_bob_period) = 8;
    u8 throttle = 8;
    const u8 practice = u8(ds_u16(DS_practice_mode));  // the low byte (BL)
    if (practice != 0) {
        if (practice == 1) throttle = 0x46;
        else if (practice == 2) throttle = 0x37;
        ds_u8(DS_engine_state) = 1;
        ds_u8(u16(DS_engine_state + 1)) = 1;
        ds_u8(DS_throttle) = throttle;
        ds_u8(u16(DS_throttle + 1)) = throttle;
    }
    ds_u8(DS_crew_throttle_target) = throttle;
    for (u16 i = 0; i < 0x20; i += 2) ds_u16(u16(DS_projectile_timer + i)) = 0;
    for (u16 i = 0; i < 8; i += 2) ds_u16(u16(DS_incoming_timer + i)) = 0;
    const u8 region = ds_u8(DS_region);
    ds_u8(DS_missile_speed) = ds_u8(u16(DS_missile_speed_table + region));
    ds_u8(DS_crew_pilot_state) = 2;  // search the route
    ds_u8(DS_route_prev_index) = 0;
    ds_u8(DS_route_link) = 0;
    ds_u8(DS_branch_command) = region == 0 ? 1 : 2;
    ds_u8(DS_route_direction) = 1;
    // row 5 * mission (in 8 bits) of the mission table
    const u8 mission = ds_u8(DS_mission_number);
    const u16 row = u16(u8(u8(mission << 2) + mission) << 1);
    for (u16 i = 0; i < 0x0A; i += 2)
        ds_u16(u16(DS_objective_objects + i)) = ds_u16(u16(DS_mission_table + row + i));
    if (ds_u8(DS_practice_targets) != 0) {
        u16 bx = u16(u16(ds_u16(DS_authored_object_count) << 1) - 2);
        do {  // objects count - 1 down to 36 (the test is after the store)
            ds_u16(u16(DS_object_word + bx)) = 0x16;
            bx = u16(bx - 2);
        } while (bx >= 0x48);
    }
    const u8 limit = ds_u8(DS_engines_upgraded) != 0 ? 0x67 : 0x3B;
    ds_u8(DS_throttle_max) = limit;
    ds_u8(u16(DS_throttle_max + 1)) = limit;
    const u16 t = u16(ds_u16(DS_mission_type) << 2);
    ds_u8(DS_clock_hours) = ds_u8(u16(DS_mission_times + t));
    ds_u8(DS_clock_minutes) = ds_u8(u16(DS_mission_times + t + 1));
    ds_u8(DS_deadline_hours) = ds_u8(u16(DS_mission_times + t + 2));
    ds_u8(DS_deadline_minutes) = ds_u8(u16(DS_mission_times + t + 3));
    time_of_day();
    // The colour remap of the A data's range [2]..[4] through the table at its end, then of the B
    // data's range [0]..[2] through the same table (SI is not reloaded: the A data's table, kept).
    const u16 a_start = u16(ds_u16(u16(DS_world_a_data + 2)) + DS_world_a_data);
    const u16 a_end = u16(ds_u16(u16(DS_world_a_data + 4)) + DS_world_a_data);
    colour_remap(DGROUP, a_start, a_end, a_end);
    const u16 b_start = u16(ds_u16(DS_world_b_data) + DS_world_b_data);
    const u16 b_end = u16(ds_u16(u16(DS_world_b_data + 2)) + DS_world_b_data);
    colour_remap(DGROUP, b_start, b_end, a_end);
}

} // namespace gb
