// main and the ways out (game_flow.md §1-§2).
#include "game/flow.hpp"

#include "mission/mission.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Strings in DGROUP
constexpr u16 S_DATAC = 0x0042, S_RB_B = 0x004C, S_DATAB = 0x004F, S_RB_A = 0x0059, S_DATAA = 0x005C,
              S_CFG_RB = 0x0120, S_GUNBOAT_CFG = 0x0123;
constexpr u16 MSG_NO_MEMORY = 0x00A6, MSG_FILE_FAILED = 0x00CA, MSG_ROSTER = 0x00F4;
constexpr u16 INSERT_DISK_2 = 0x7835, INSERT_DISK_1 = 0xB4DC;  // text records (DAT6.DAT, DGROUP)

// The choice table: video mode by setup choice (VGA, EGA, CGA, Hercules, Tandy, exit).
constexpr u16 MODE_BY_CHOICE = 0x008C;
// The default setup choice by gfx_detect() result.
constexpr u16 CHOICE_BY_DETECT = 0x0092;

// PORT: the player's video card (the launcher, --video): the mode that replaces the one of
// GUNBOAT.CFG or of the setup questions (0: keep it). Host configuration, not game state.
u16 video_choice;

// The disk checks of main: the prompt comes up while the data file is missing. The port finds its
// files, so fopen succeeds and wait_key(1) returns at once.
void disk_check(u16 name, u16 mode, bool first_disk)
{
    u16 ok = 0;
    do {
        ds_u8(DS_disk_prompt) = 0;
        ds_u16(DS_flow_file) = crt_fopen(name, mode);
        if (ds_u16(DS_flow_file) == 0) {
            ds_u8(DS_disk_prompt) = 1;
            if (first_disk) {
                gfx_set_colour(0);
                gfx_fill_rect(0, 0x013F, 0, 0x000B);
                text_set_colours(0x0F, 0x00);
                print_records(INSERT_DISK_1, 0);
            } else {
                text_set_colours(0x0F, 0x04);
                print_records(INSERT_DISK_2, 0);
            }
            ok = 0;
        } else {
            crt_fclose(ds_u16(DS_flow_file));
            ok = 1;
        }
        wait_key(ok);
    } while (ok == 0);
}

// The practice missions (main 0000:0042): the station of the choice, the mission, back to the title.
void practice()
{
    ds_u8(DS_first_run) = 0;
    ds_u16(DS_phase) = 3;
    if (ds_u16(DS_flow_scratch) == 1) ds_u16(DS_station) = 2;       // gunnery
    else if (ds_u16(DS_flow_scratch) == 2) ds_u16(DS_station) = 4;  // grenade
    else ds_u16(DS_station) = 1;                                     // pilot
    gfx_set_copy_page(2);
    disk_check(S_DATAB, S_RB_B, false);
    mission_run();
    gfx_set_copy_page(1);
    engine_sound_off();
    disk_check(S_DATAA, S_RB_A, true);
    ds_u16(DS_phase) = 0;
}

} // namespace

// 0000:021e quit_to_dos (game_flow.md §1)
void quit_to_dos()
{
    music_stop();
    mem_free_all();
    gfx_free_page(1);
    if (ds_u16(DS_single_page_mode) == 0) gfx_free_page(2);
    gfx_set_mode(s16(ds_u16(DS_bios_video_mode)));
    text_exit_clear();
    engine_sound_off();
    if (ds_u16(DS_phase) != 0x00FF) kbd_restore();
    crt_exit(0);
}

// 0000:0276 fatal_exit (game_flow.md §1): as quit_to_dos, with a message; for code 1 the pages
// stay allocated, and INT 9 is restored even before kbd_install (the vector then saved is
// 0000:0000). PORT: the message goes to a message box, there is no text screen.
void fatal_exit(s16 code)
{
    music_stop();
    mem_free_all();
    if (code != 1) {
        gfx_free_page(1);
        if (ds_u16(DS_single_page_mode) == 0) gfx_free_page(2);
    }
    gfx_set_mode(s16(ds_u16(DS_bios_video_mode)));
    text_exit_clear();
    engine_sound_off();
    const u16 msg = code == 1 ? MSG_NO_MEMORY : code == 2 ? MSG_FILE_FAILED : code == 3 ? MSG_ROSTER : 0;
    if (msg) host_error_box(ds_str(msg));
    kbd_restore();
    crt_exit(code);
}

void config_set_video_choice(u16 mode) { video_choice = mode; }

// 0000:05fc config_print_mode, 0000:0620 config_print_yn: the setup screen's answers at text
// positions (17f0:0058 and printf). PORT: there is no text screen; nothing is shown.
void config_print_mode(u16) {}
void config_print_yn(u16) {}

// 0000:02fe config_load (game_flow.md §2): the video mode and the joystick from GUNBOAT.CFG, or
// from the text-mode setup questions; the mode set, page 1 (and page 2 outside VGA) allocated.
// PORT: without GUNBOAT.CFG the questions are answered with Enter (the detected mode, VGA, and the
// joystick if a gamepad answers the probes), since the port has no text screen.
void config_load()
{
    ds_u16(DS_bios_video_mode) = u16(gfx_saved_mode());
    const s16 detected = gfx_detect();
    ds_u16(DS_flow_scratch) = ds_u8(u16(CHOICE_BY_DETECT + detected));
    gfx_set_mode(s16(ds_u16(DS_bios_video_mode)));
    ds_u16(DS_flow_file) = crt_fopen(S_GUNBOAT_CFG, S_CFG_RB);
    if (ds_u16(DS_flow_file) != 0) {
        crt_fread(DS_video_mode, 2, 1, ds_u16(DS_flow_file));
        crt_fread(DS_joystick, 2, 1, ds_u16(DS_flow_file));
        crt_fread(DS_single_page_mode, 2, 1, ds_u16(DS_flow_file));
        crt_fclose(ds_u16(DS_flow_file));
    } else {
        // the mode question: the default shown, Enter accepts (PORT: Enter is the only key)
        config_print_mode(u16(ds_u16(DS_flow_scratch) + 1));
        if (ds_u16(DS_flow_scratch) == 5) quit_to_dos();
        // the joystick question: Y unless one of 11 probes of the stick times out; Enter accepts
        ds_u8(DS_name_buffer) = 'Y';
        for (s16 i = 0; i < 0x0B; i++)
            if (joystick_axis(1) == 0xFFFF) ds_u8(DS_name_buffer) = 'N';
        config_print_yn(ds_u8(DS_name_buffer));
        ds_u16(DS_joystick) = ds_u8(DS_name_buffer) == 'Y' ? 1 : 0;
        ds_u16(DS_video_mode) = ds_u8(u16(MODE_BY_CHOICE + ds_u16(DS_flow_scratch)));
    }
    if (video_choice) ds_u16(DS_video_mode) = video_choice;  // PORT: the player's video card
    if (ds_u16(DS_joystick) != 0) joystick_calibrate(1);
    if (ds_u16(DS_video_mode) == 0x0C) {  // Hercules, on CGA mode 4
        ds_u8(DS_hercules_mode) = 1;
        ds_u16(DS_video_mode) = 4;
        gfx_set_mode(4);
        hercules_setup();
    } else {
        ds_u8(DS_hercules_mode) = 0;
        gfx_set_mode(s16(ds_u16(DS_video_mode)));
        // PORT: mode 4: ega_pal_register(1, 0) (147c:000f), the CGA palette: not ported.
    }
    gfx_set_visible_page(0);
    ds_u16(DS_flow_scratch) = gfx_alloc_page(1);
    if (ds_u16(DS_flow_scratch) == 8) fatal_exit(1);
    gfx_set_copy_page(1);
    gfx_set_draw_page(1);
    ds_u16(u16(DS_page_segments + 2)) = gfx_get_draw_seg();
    gfx_set_draw_page(0);
    ds_u16(DS_page_segments) = gfx_get_draw_seg();
    if (ds_u16(DS_video_mode) == 0x13) ds_u16(DS_single_page_mode) = 1;
    u16 page2;
    if (ds_u16(DS_single_page_mode) == 0) {
        ds_u16(DS_view_page) = 2;
        ds_u16(DS_flow_scratch) = gfx_alloc_page(2);
        if (ds_u16(DS_flow_scratch) == 8) {
            gfx_free_page(1);
            fatal_exit(1);
        }
        gfx_set_draw_page(2);
        page2 = gfx_get_draw_seg();
    } else {
        ds_u16(DS_view_page) = 0;
        page2 = ds_u16(DS_page_segments);
    }
    ds_u16(u16(DS_page_segments + 4)) = page2;
    gfx_set_draw_page(0);
}

// 0000:0000 main (game_flow.md §1). (game_main: C++ reserves the name main.) The archive
// directory, the configuration, the keyboard and the far buffers; then the title, the practice
// missions and the campaign, forever (the ways out are quit_to_dos and fatal_exit).
void game_main()
{
    file_load_near(S_DATAC, DS_archive_directory);
    ds_u16(DS_phase) = 0x00FF;
    ds_u8(DS_first_run) = 1;
    config_load();
    ds_u16(DS_phase) = 0;
    kbd_install();
    mem_alloc_all();
    for (;;) {
        ds_u16(DS_flow_scratch) = title_menu();
        if (ds_u16(DS_flow_scratch) != 0) {
            practice();
            continue;
        }
        // report for duty: the headquarters, then the campaign
        ds_u16(DS_phase) = 1;
        ds_u16(DS_flow_scratch) = hq_quiz();
        if (ds_u16(DS_flow_scratch) != 0) {  // the quiz failed: gunnery practice
            ds_u16(DS_region) = 3;
            ds_u16(DS_practice_mode) = 1;
            ds_u16(DS_mission_number) = 1;
            ds_u16(DS_mission_type) = 1 + 0x18;
            ds_u8(DS_midship_weapon) = 0;
            ds_u8(DS_stern_weapon) = 0;
            ds_u8(DS_bow_weapon) = 0;
            ds_u8(DS_engines_upgraded) = 1;
            ds_u16(DS_sea_state) = 1;
            practice();
            continue;
        }
        for (;;) {
            ds_u16(DS_phase) = 2;
            front_end();
            ds_u16(DS_phase) = 3;
            ds_u16(DS_station) = 1;
            gfx_set_copy_page(2);
            mission_run();
            gfx_set_copy_page(1);
        }
    }
}

} // namespace gb
