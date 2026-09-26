// The title sequence and the main menu (game_flow.md §3).
#include "game/flow.hpp"

#include "game/pending.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Strings in DGROUP (file names)
constexpr u16 S_DAT6 = 0x0722, S_TITLCOLR = 0x072B, S_COPY = 0x0738, S_ACCO = 0x0740, S_TITLE1A = 0x0748,
              S_TITLE1B = 0x0753, S_TITLE1C = 0x075E, S_TITLE1D = 0x0769, S_FOOTIT1E = 0x0774,
              S_FOOTIT1F = 0x0780, S_TITLE3C = 0x078C, S_TITLE3B = 0x0797, S_TITLE3A = 0x07A2,
              S_TIT1COLR = 0x07AD, S_TITLE2A = 0x07BA, S_TITLE2B = 0x07C5, S_TITLE2C = 0x07D0,
              S_TITLE2D = 0x07DB, S_TIT2COLR = 0x07E6, S_TITLE3C_2 = 0x07F3, S_TITLE3B_2 = 0x07FE,
              S_TITLE3A_2 = 0x0809, S_TIT3COLR = 0x0814, S_PENCIL = 0x0A00;
constexpr u16 PIC = 0x1094;          // the decode buffer
constexpr u16 MENU_TEXTS = 0x6FB1;   // "REPORT FOR DUTY" 80h ... (DAT6.DAT)
constexpr u16 CREDITS = 0x6EB1;

// The four picture buffers the title uses (far buffers of mem_alloc_all).
FarPtr buf_a() { return ds_far(DS_pictures_far); }
FarPtr buf_b() { return ds_far(DS_midship_art2_far); }
FarPtr buf_c() { return ds_far(DS_clip_far); }
FarPtr buf_d() { return ds_far(DS_stern_art2_far); }

void decode(FarPtr p) { lzw_decode_picture(p, {PIC, DGROUP}); }

void set_draw_page(u16 page)
{
    ds_u16(DS_draw_page) = page;
    gfx_set_draw_page(s16(page));
}

bool vga() { return ds_u16(DS_video_mode) == 0x13; }

// A full-width picture: picture_draw_vga in VGA, picture_draw at the pen otherwise.
void draw_full(u16 runs, u16 y)
{
    if (!vga()) picture_draw(PIC, s16(runs), 0x140);
    else picture_draw_vga(PIC, runs, y);
}

} // namespace

// 020d:064a menu_cursor_init (game_flow.md §3): PENCIL.MPP drawn on page 1 at (128h, bottom 23h) and
// captured as colour masks for the menu cursor.
void menu_cursor_init()
{
    file_load_near(S_PENCIL, PIC);
    set_draw_page(1);
    gfx_move_to(0x128, 0x23);
    if (ds_u16(DS_video_mode) == 4) {
        // PORT: the CGA palette registers (ega_pal_set 14ae:0005) are not ported.
    }
    picture_draw(PIC, 0x56, 0x18);
    ega_pal_apply();
    gfx_move_to(0x128, 0x14);
    switch (ds_u16(DS_video_mode)) {
    case 9: case 0x0D: case 0x13:
        ds_u16(DS_title_colour_a) = 6;
        ds_u16(DS_title_colour_b) = 8;
        ds_u16(DS_title_colour_c) = 0x0C;
        ds_u16(DS_title_colour_d) = 1;
        ds_u16(DS_pencil_colour_tip) = 0x0F;
        break;
    case 4:
        ds_u16(DS_title_colour_a) = 0;
        ds_u16(DS_title_colour_b) = 1;
        ds_u16(DS_title_colour_c) = 2;
        ds_u16(DS_title_colour_d) = 2;
        ds_u16(DS_pencil_colour_tip) = 2;
        break;
    default:
        break;
    }
    gfx_set_colour(s16(ds_u16(DS_title_colour_a)));
    gfx_read_bitmap(DS_sprite_mask_a, 3, 0x15);
    gfx_set_colour(s16(ds_u16(DS_title_colour_b)));
    gfx_read_bitmap(DS_pencil_mask_b, 3, 0x15);
    gfx_set_colour(s16(ds_u16(DS_title_colour_c)));
    gfx_read_bitmap(DS_pencil_mask_c, 3, 0x15);
    gfx_set_colour(s16(ds_u16(DS_pencil_colour_tip)));
    gfx_move_to(0x128, 0x1C);
    gfx_read_bitmap(DS_pencil_mask_tip, 1, 8);
    gfx_move_to(0x128, 0x23);
    gfx_read_bitmap(DS_pencil_mask_point, 1, 7);
}

// 00f2:000e title_menu (game_flow.md §3): on the first run (or in the demo) the intro: COPY, ACCO,
// the moving sprite, TITLE1 with the music, TITLE2, the credits; then the menu on TITLE3. Returns the
// choice: 0 report for duty, 1 gunnery, 2 grenade, 3 pilot practice.
u16 title_menu()
{
    ds_u16(DS_menu_idle) = 0;
    ds_u8(DS_sfx_device_tandy) = 0;
    engine_sound_on();
    pal_fade_out_vga();
    engine_sound_off();
    ds_u16(DS_practice_mode) = 0;
    ds_u8(DS_text_transparent) = 0;
    file_load_near(S_DAT6, 0x6E54);
    const bool intro = ds_u16(DS_demo_mode) == 1 || ds_u8(DS_first_run) == 1;
    if (intro) {
        file_load_near(S_TITLCOLR, DS_palette_3d);
        ega_pal_init();
        set_draw_page(0);
        if (ds_u8(DS_first_run) == 1) {
            file_load_far(S_COPY, buf_a());
            decode(buf_a());
            gfx_move_to(0, 0xC7);
            if (ds_u16(DS_video_mode) == 4) {
                ega_pal_entry(4, 0x2AA);
                ega_pal_entry(7, 0x255);
                ega_pal_entry(8, 0x200);
                ega_pal_entry(0x0F, 0x2FF);
            }
            picture_draw(PIC, 0x1B5F, 0x140);
            if (ds_u16(DS_video_mode) == 4) ega_pal_apply();
        } else {
            gfx_set_colour(0);
            gfx_fill_rect(0, 0x13F, 0, 0xC7);
        }
        engine_sound_on();
        pal_fade_in_vga();
        engine_sound_off();
        if (ds_u8(DS_first_run) == 1) wait_key(0x16C);
        if (ds_u16(DS_demo_mode) == 1) ds_u8(DS_first_run) = 1;
        file_load_far(S_ACCO, buf_a());
        decode(buf_a());
        set_draw_page(1);
        gfx_move_to(0, 0xC7);
        if (ds_u16(DS_video_mode) == 4) {
            ega_pal_entry(1, 0x255);
            ega_pal_entry(3, 0x255);
            ega_pal_entry(4, 0x2AA);
            ega_pal_entry(7, 0x255);
            ega_pal_entry(9, 0x2AA);
            ega_pal_entry(0x0C, 0x2FF);
            ega_pal_entry(0x0F, 0x2FF);
        }
        picture_draw(PIC, 0x0885, 0x140);
        engine_sound_on();
        screen_present();
        engine_sound_off();
        if (vga()) {
            file_load_far(S_TITLE1A, buf_a());
            file_load_far(S_TITLE1B, buf_b());
            file_load_far(S_TITLE1C, buf_c());
            file_load_far(S_TITLE1D, buf_d());
        } else {
            file_load_far(S_FOOTIT1E, buf_c());
            file_load_far(S_FOOTIT1F, buf_a());
        }
    } else {
        file_load_far(S_TITLE3C, buf_a());
        file_load_far(S_TITLE3B, buf_b());
        file_load_far(S_TITLE3A, buf_c());
    }

    gfx_move_to(0, 0x41);
    switch (ds_u16(DS_video_mode)) {
    case 4:
        ds_u16(DS_title_colour_a) = 0;
        ds_u16(DS_title_colour_b) = 2;
        ds_u16(DS_title_colour_c) = 3;
        ds_u16(DS_title_colour_d) = 0;
        break;
    case 9: case 0x0D: case 0x13:
        ds_u16(DS_title_colour_a) = 0;
        ds_u16(DS_title_colour_b) = 4;
        ds_u16(DS_title_colour_c) = 0x0C;
        ds_u16(DS_title_colour_d) = 1;
        break;
    default:
        break;
    }

    if (ds_u8(DS_first_run) == 1) {
        // the sprite that runs across the ACCO picture, captured at (0, 41h) in three colour layers
        gfx_set_colour(s16(ds_u16(DS_title_colour_a)));
        gfx_read_bitmap(DS_sprite_mask_a, 1, 4);
        gfx_set_colour(s16(ds_u16(DS_title_colour_b)));
        gfx_read_bitmap(DS_sprite_mask_b, 2, 4);
        gfx_set_colour(s16(ds_u16(DS_title_colour_c)));
        gfx_read_bitmap(DS_sprite_mask_c, 2, 4);
        engine_sound_on();
        ds_u16(DS_tick_counter) = 0;
        for (s16 x = 0; x < 0x118; x = s16(x + 3)) {
            // one step per timer tick; random() on every poll that still sees the old tick (the
            // original tests first, 00f2:0460 -> 03d8, and calls random at 03d0 while it waits)
            const u16 tick = ds_u16(DS_tick_counter);
            while (tick == ds_u16(DS_tick_counter)) {
                host_pump();
                random();
            }
            gfx_move_to(x, 0x41);
            gfx_set_colour(s16(ds_u16(DS_title_colour_c)));
            gfx_draw_bitmap(DS_sprite_mask_c, 2, 4);
            gfx_set_colour(s16(ds_u16(DS_title_colour_b)));
            gfx_draw_bitmap(DS_sprite_mask_b, 2, 4);
            gfx_set_colour(s16(ds_u16(DS_title_colour_a)));
            gfx_draw_bitmap(DS_sprite_mask_a, 1, 4);
        }
        wait_key(0x40);
        decode(buf_a());
        if (vga()) pal_fade_out_vga();
        else set_draw_page(1);
        file_load_near(S_TIT1COLR, DS_palette_3d);
        ega_pal_init();
        if (vga()) {
            picture_draw_vga(PIC, 0x210C, 0x31);
            decode(buf_b());
            picture_draw_vga(PIC, 0x1ED4, 0x63);
            decode(buf_c());
            picture_draw_vga(PIC, 0x1832, 0x95);
            decode(buf_d());
            picture_draw_vga(PIC, 0x1A23, 0xC7);
            pal_fade_in_vga();
        } else {
            gfx_move_to(0, 0xC7);
            picture_draw(PIC, 0x2ABC, 0x140);
            decode(buf_c());
            gfx_move_to(0, 0x60);
            picture_draw(PIC, 0x2E98, 0x140);
            screen_present();
        }
        engine_sound_off();
        music_start();
        ds_u16(DS_title_delay) = 1;
        file_load_far(S_TITLE2A, buf_a());
        file_load_far(S_TITLE2B, buf_b());
        file_load_far(S_TITLE2C, buf_c());
        file_load_far(S_TITLE2D, buf_d());
        wait_key(0x64);
        decode(buf_a());
        gfx_move_to(0, 0x3F);
        if (vga()) pal_fade_out_vga();
        else set_draw_page(1);
        file_load_near(S_TIT2COLR, DS_palette_3d);
        ega_pal_init();
        draw_full(0x22DA, 0x3F);
        decode(buf_b());
        gfx_move_to(0, 0x63);
        draw_full(0x1977, 0x63);
        decode(buf_c());
        gfx_move_to(0, 0x94);
        draw_full(0x1A3C, 0x94);
        decode(buf_d());
        gfx_move_to(0, 0xC7);
        draw_full(0x16D6, 0xC7);
        set_draw_page(0);
        if (vga()) pal_fade_in_vga();
        else screen_present();
        text_set_colours(0x0F, 0x00);
        file_load_far(S_TITLE3C_2, buf_a());
        file_load_far(S_TITLE3B_2, buf_b());
        file_load_far(S_TITLE3A_2, buf_c());
        credits_text(CREDITS);
    }

    // the menu background: TITLE3 on page 1, copied to the screen
    ds_u8(DS_text_transparent) = 1;
    set_draw_page(1);
    decode(buf_c());
    gfx_move_to(0, 0x59);
    if (ds_u8(DS_first_run) != 1) engine_sound_on();
    else pal_fade_out_vga();
    file_load_near(S_TIT3COLR, DS_palette_3d);
    ega_pal_init();
    draw_full(0x2CA7, 0x59);
    decode(buf_b());
    gfx_move_to(0, 0xC7);
    draw_full(0x15D2, 0xC7);
    decode(buf_a());
    gfx_move_to(0xA0, 0xC7);
    switch (ds_u16(DS_video_mode)) {
    case 4:
        ega_pal_entry(0x10, 0x2FF);
        ega_pal_entry(0x12, 0x255);
        ega_pal_entry(0x1A, 0x2AA);
        ega_pal_entry(0x1E, 0x2AA);
        break;
    case 9: case 0x0D:
        ega_pal_entry(0x10, 0x2FF);
        ega_pal_entry(0x11, 0x277);
        ega_pal_entry(0x12, 0x277);
        ega_pal_entry(0x14, 0x266);
        ega_pal_entry(0x15, 0x266);
        ega_pal_entry(0x16, 0x266);
        ega_pal_entry(0x17, 0x277);
        ega_pal_entry(0x1A, 0x277);
        ega_pal_entry(0x1C, 0x277);
        ega_pal_entry(0x1F, 0x288);
        break;
    default:
        break;
    }
    picture_draw(PIC, 0x1ECB, 0xA0);
    set_draw_page(0);
    gfx_copy_rect_to_copy_page(0, 0x13F, 0, 0xC7);
    pal_fade_in_vga();
    ega_pal_apply();
    u16 debounce = 0, blink = 0;
    menu_cursor_init();
    ds_u16(DS_pencil_colour) = ds_u16(DS_title_colour_a);
    kbd_flush_key();
    ds_u16(DS_title_colour_a) = ds_u16(DS_video_mode) == 4 ? 2 : 0;

    // the menu: a 2 x 2 grid of items, the selection blinking
    while (s16(ds_u16(DS_practice_mode)) < 0x80) {
        blink ^= 1;  // XOR of the low byte
        if (debounce != 0) debounce--;
        u16 key = 0;
        input_read_key(&key);
        if (key != 0) {
            ds_u16(DS_menu_idle) = 0;
            if (debounce == 0) {
                debounce = 2;
                switch (u16(key - 0x91)) {  // jump table 00f2:0AF0
                case 0: ds_u16(DS_practice_mode) = 0; break;            // 7 Home
                case 1: ds_u16(DS_practice_mode) &= 1; break;           // 8 Up
                case 2: ds_u16(DS_practice_mode) = 1; break;            // 9 PgUp
                case 3: ds_u16(DS_practice_mode) &= 2; break;           // 4 Left
                case 5: ds_u8(DS_practice_mode) |= 1; break;            // 6 Right
                case 6: ds_u16(DS_practice_mode) = 2; break;            // 1 End
                case 7: ds_u8(DS_practice_mode) |= 2; break;            // 2 Down
                case 8: ds_u16(DS_practice_mode) = 3; break;            // 3 PgDn
                default: break;                                         // 5, other keys
                }
            }
        }
        if (ds_u16(DS_menu_idle) == 0x113) key = 0x44;  // idle: the demo
        if (key == 0x0D || key == 0x44 || key == 0x64) {
            ds_u16(DS_region) = 3;
            if (key == 0x44 || key == 0x64) {  // 'D': the demo
                ds_u16(DS_e_toggle) = 1;
                ds_u16(DS_practice_mode) = 1;
                ds_u16(DS_mission_number) = 1;
                ds_u16(DS_demo_mode) = 1;
                ds_u16(DS_demo_script_pos) = 8;
                ds_u8(DS_demo_countdown) = 0;
                ds_u8(DS_demo_next) = 0;
            } else if (ds_u16(DS_practice_mode) == 1) {
                ds_u16(DS_mission_number) = 1;
            } else if (ds_u16(DS_practice_mode) == 2) {
                ds_u16(DS_mission_number) = 2;
            } else {
                ds_u16(DS_mission_number) = 0;
            }
            ds_u16(DS_mission_type) = u16((ds_u16(DS_region) << 3) + ds_u16(DS_mission_number));
            ds_u8(DS_midship_weapon) = 0;
            ds_u8(DS_stern_weapon) = 0;
            ds_u8(DS_bow_weapon) = 0;
            ds_u8(DS_engines_upgraded) = 1;
            ds_u16(DS_sea_state) = ds_u16(DS_practice_mode) == 3 ? 3 : 1;
            ds_u16(DS_practice_mode) = u16(ds_u16(DS_practice_mode) + 0x80);
            blink = 1;
        }
        set_draw_page(0);
        static const u16 cell[4][2] = {{0x0B, 0x01}, {0x0B, 0x15}, {0x0D, 0x01}, {0x0D, 0x15}};
        u16 off = 0;
        for (u16 item = 0; item < 4; item++) {
            const u16 colour =
                (ds_u8(DS_practice_mode) & 0x7F) == item && blink == 1 ? 0x0F : ds_u16(DS_title_colour_a);
            text_set_colours(s16(colour), 7);
            text_goto_cell(s16(cell[item][0]), s16(cell[item][1]));
            off = print_text(MENU_TEXTS, off);
        }
        bios_wait_ticks(2);
        ds_u16(DS_menu_idle)++;
    }

    ds_u16(DS_title_colour_a) = 0;
    ds_u16(DS_practice_mode) &= 0x7F;
    if (ds_u8(DS_first_run) == 1) music_stop();
    engine_sound_off();
    ds_u8(DS_text_transparent) = 0;
    return ds_u16(DS_practice_mode);
}

} // namespace gb
