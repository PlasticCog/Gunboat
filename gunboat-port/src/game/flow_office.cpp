// The office of the front end (game_flow.md §6), segment 02d2: the officer's face and the office
// animation, the speech area, the folders, and the BCD helpers.
#include "game/flow.hpp"
#include "game/flow_util.hpp"

#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

using namespace flow;

namespace {
constexpr u16 S_GENA = 0x0B1D, S_FOLDER_CODE = 0x0B06;  // "GENA.LZ", "4x7x"
}

// 02d2:076c office_face_draw: the officer's face frame by the mood (office_mood & 7) from the page-1
// sprite sheet, unless a folder hides the office.
void office_face_draw()
{
    const u16 row = u16((ds_u16(DS_office_mood) & 3) * 5 + 0x10);
    const u16 col = u16((ds_u16(DS_office_mood) & 4) * 6);
    if (ds_u8(DS_office_hidden) == 0)
        gfx_copy_rect(u16(col + 0xC0), u16(col + 0xD7), row, u16(row + 3), 0x108, 0x33, 1, 0);
}

// 02d2:07d2 office_idle: once per iteration of every front-end input loop. Random frames of the two
// background figures (held for one call) and of the officer (a blink, held 1, or another
// expression, held 8); every 32nd blink on an even random number lowers the mood.
void office_idle()
{
    u16 r = random();
    set_draw_page(0);
    if (ds_u16(DS_office_figures_hold) != 0) {
        ds_u16(DS_office_figures_hold)--;
    } else {
        gfx_copy_rect(0x40, 0x4F, 0x24, 0x25, 0x98, 0x1F, 1, 0);
        gfx_copy_rect(0x50, 0x5F, 0x24, 0x25, 0xC8, 0x1D, 1, 0);
        if ((r & 0x074A) == 0) {
            if ((u8(r) & 1) == 0) gfx_copy_rect(0x40, 0x4F, 0x26, 0x27, 0x98, 0x1F, 1, 0);
            else gfx_copy_rect(0x50, 0x5F, 0x26, 0x27, 0xC8, 0x1D, 1, 0);
            ds_u16(DS_office_figures_hold) = 1;
        }
    }
    if (ds_u16(DS_office_face_hold) != 0) {
        ds_u16(DS_office_face_hold)--;
        return;
    }
    gfx_copy_rect(0, 0x1F, 0x20, 0x23, 0x100, 0x24, 1, 0);
    if ((r & 0x01B4) == 0) {
        gfx_copy_rect(0x20, 0x3F, 0x24, 0x27, 0x100, 0x24, 1, 0);
        ds_u16(DS_office_face_hold) = 1;
        if ((r & 0x01B5) != 0) return;
        ds_u16(DS_office_blink_count)++;
        if ((ds_u8(DS_office_blink_count) & 0x1F) != 0) return;
        if (ds_u16(DS_office_mood) != 0) ds_u16(DS_office_mood)--;
        office_face_draw();
        return;
    }
    if ((r & 0x2D68) != 0) return;
    const u16 x = u16((s16((r & 3) + 1) / 2) << 5);
    r = u16((((r + 1) & 1) << 2) + 0x20);
    gfx_copy_rect(x, u16(x + 0x1F), r, u16(r + 3), 0x100, 0x24, 1, 0);
    ds_u16(DS_office_face_hold) = 8;
}

// 02d2:0994 speech_clear: the text area (grey rows 90h..BFh, black rows C0h..C7h) on the draw page.
void speech_clear()
{
    gfx_set_colour(7);
    gfx_fill_rect(0, 0x13F, 0x90, 0xBF);
    gfx_set_colour(0);
    gfx_fill_rect(0, 0x13F, 0xC0, 0xC7);
}

// 02d2:09da folder_draw: FOLDER.LZ on page 1 with the code "4<c1>7<c2>" at row 7, column 31.
// Leaves the draw page at 1.
void folder_draw(u16 c1, u16 c2)
{
    decode(DS_bd2_far);  // FOLDER.LZ
    set_draw_page(1);
    gfx_move_to(0, 0xC7);
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 4) {
        ega_pal_entry(0, 0x200);
        ega_pal_entry(1, 0x255);
        ega_pal_entry(4, 0x2FF);
        ega_pal_entry(7, 0x2FF);
        ega_pal_entry(8, 0x2FF);
        ega_pal_entry(9, 0x2AA);
        ega_pal_entry(0x0F, 0x2AA);
        ega_pal_entry(0x10, 0x2AA);
        ega_pal_entry(0x11, 0x277);
        ega_pal_entry(0x13, 0x2AA);
        ega_pal_entry(0x1A, 0x255);
        ega_pal_entry(0x1C, 0x266);
    } else if (mode == 9 || mode == 0x0D) {
        ega_pal_entry(0x10, 0x2FF);
        ega_pal_entry(0x11, 0x288);
        ega_pal_entry(0x13, 0x288);
        ega_pal_entry(0x1A, 0x222);
        ega_pal_entry(0x1C, 0x2AA);
    }
    draw_full(0x0FD7, 0xC7);
    ega_pal_apply();
    crt_strcpy(DS_folder_code, S_FOLDER_CODE);
    ds_u8(u16(DS_folder_code + 1)) = u8(c1);
    ds_u8(u16(DS_folder_code + 3)) = u8(c2);
    text_set_colours(0, 7);
    text_goto_cell(7, 0x1F);
    print_chars(DS_folder_code, 4);
}

// 02d2:0bac folder_present: page 1 rows 28h..C7h (the folder) onto the screen; the office is hidden.
void folder_present()
{
    set_draw_page(0);
    gfx_copy_rect_from_copy_page(0, 0x13F, 0x28, 0xC7);
    ds_u8(DS_office_hidden) = 1;
}

// 02d2:2c52 wait_key_idle: as wait_key, with the office animation: 0 on a key, or n after n - 1
// ticks (n = 0: until a key; n = 1: at once).
u16 wait_key_idle(u16 n)
{
    u16 i = 1;
    while (i != n) {
        bios_wait_ticks(1);
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        if (key != 0) return 0;
        if (n != 0) i++;
    }
    return n;
}

// 02d2:2c9e office_draw: GENA/GENB/GENC on page 1 with an empty speech area, shown, faded in.
void office_draw()
{
    set_draw_page(1);
    file_load_far(S_GENA, ds_far(DS_pictures_far));
    decode(DS_pictures_far);
    gfx_move_to(0, 0x27);
    ega_pal_init();
    draw_full(0x1637, 0x27);
    decode(DS_stern_art2_far);  // GENB.LZ
    gfx_move_to(0, 0x59);
    draw_full(0x1F9A, 0x59);
    decode(DS_world_b_far);  // GENC.LZ
    gfx_move_to(0, 0x8F);
    draw_full(0x1DCC, 0x8F);
    speech_clear();
    set_draw_page(0);
    gfx_copy_rect_from_copy_page(0, 0x13F, 0, 0xC7);
    engine_sound_on();
    pal_fade_in_vga();
    engine_sound_off();
}

// 02d2:2df4 office_restore: GENB/GENC and an empty speech area on page 1, shown (rows 28h..C7h),
// the office visible again and the face drawn.
void office_restore()
{
    set_draw_page(1);
    decode(DS_stern_art2_far);  // GENB.LZ
    gfx_move_to(0, 0x59);
    draw_full(0x1F9A, 0x59);
    decode(DS_world_b_far);  // GENC.LZ
    gfx_move_to(0, 0x8F);
    draw_full(0x1DCC, 0x8F);
    speech_clear();
    folder_present();
    ds_u8(DS_office_hidden) = 0;
    office_face_draw();
}

// 02d2:2bae bcd_to_bin: 4 BCD digits to binary (nibbles above 9 are not checked).
u16 bcd_to_bin(u16 x)
{
    u16 v = 0;
    for (u16 i = 0; i < 4; i++) {
        v = u16(v * 10);
        v = u16(v + ((x & 0xF000) >> 12));
        x = u16(x << 4);
    }
    return v;
}

// 02d2:2bea bin_to_bcd: binary to 4 BCD digits (unsigned divisions; the thousands digit mod 10).
u16 bin_to_bcd(u16 x)
{
    const u16 ones = x % 10, thousands = (x / 1000) % 10, hundreds = (x / 100) % 10, tens = (x / 10) % 10;
    return u16((tens << 4) + (hundreds << 8) + (thousands << 12) + ones);
}

// 02d2:2b4a bcd_inc: a BCD counter + 1, saturating at 9999.
u16 bcd_inc(u16 x)
{
    x = bcd_to_bin(x);
    if (x < 0x270F) x++;
    return bin_to_bcd(x);
}

// 02d2:2b72 bcd_add: the sum of two BCD numbers, saturating at 9999 (the binary sum wraps at 16
// bits first).
u16 bcd_add(u16 a, u16 b)
{
    a = bcd_to_bin(a);
    b = bcd_to_bin(b);
    a = u16(a + b);
    if (a > 0x270F) a = 0x270F;
    return bin_to_bcd(a);
}

} // namespace gb
