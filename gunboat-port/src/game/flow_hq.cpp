// The headquarters (game_flow.md §4-§5), segment 020d: the quiz, the pencil menus and the roster
// file.
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

constexpr u16 S_QUIZCOLR = 0x09C6, S_HQ1 = 0x09D3, S_HQ2 = 0x09DA, S_HQ3 = 0x09E1, S_HQARM = 0x09E8,
              S_DATAB_RB = 0x09F1, S_DATAB = 0x09F4, S_ROSTER_RB = 0x0A0B, S_ROSTER = 0x0A0E,
              S_ROSTER_WB = 0x0A1B, S_ROSTER_W = 0x0A1F;
constexpr u16 QUESTION_TEXTS = 0x6FF4, CLASS_TEXTS = 0x700A;  // DAT6-relative text pointers
constexpr u16 ANSWERS = 0x7C2C;                               // 5-byte answers from DS:7C2D
constexpr u16 TEXT_RIGHT = 0x6E5F, TEXT_WRONG = 0x6E81, TEXT_INSERT_DISK2 = 0x7812;
constexpr u16 TICK_MARK = 0x6E9F;  // 9 (dx, dy) byte pairs (DAT6)
constexpr u16 ROSTER_SIZE = 0x2BF;

// The quiz's typing cursor: an 8x8 cell at column n (in the text row 23), in a colour.
void quiz_cell(u16 n, u16 colour)
{
    gfx_set_colour(s16(colour));
    const u16 x = u16(n * 8 + 0x80);
    gfx_fill_rect(x, u16(x + 7), 0xB8, 0xBF);
}

} // namespace

// 020d:09cc menu_cursor_draw (game_flow.md §6): the band y0..y1 of page 1 back onto the screen,
// then the pencil (three colour masks of menu_cursor_init) at the pen.
void menu_cursor_draw(u16 y0, u16 y1)
{
    set_draw_page(0);
    gfx_copy_rect_from_copy_page(0x20, 0xEF, y0, y1);
    gfx_set_colour(s16(ds_u16(DS_pencil_colour)));
    gfx_draw_bitmap(DS_sprite_mask_a, 3, 0x15);
    gfx_set_colour(s16(ds_u16(DS_title_colour_b)));
    gfx_draw_bitmap(DS_pencil_mask_b, 3, 0x15);
    gfx_set_colour(s16(ds_u16(DS_title_colour_c)));
    gfx_draw_bitmap(DS_pencil_mask_c, 3, 0x15);
}

// 020d:09a0 menu_cursor_move: the pen to item `index` of the {x, y} word pairs at DS:items, and the
// pencil there.
void menu_cursor_move(u16 index, u16 items, u16 y0, u16 y1)
{
    const u16 si = u16(index * 4 + items);
    gfx_move_to(ds_s16(si), ds_s16(u16(si + 2)));
    menu_cursor_draw(y0, y1);
}

// 020d:0a56 menu_tick_mark: the pencil draws a 9-pixel tick on page 1 around (x, y), one pixel per
// BIOS tick, each step shown by menu_cursor_draw; then wait_key(9). In the front end the officer's
// face goes neutral first.
void menu_tick_mark(u16 x, u16 y, u16 y0, u16 y1)
{
    if (ds_u16(DS_phase) == 2) gfx_copy_rect(0, 0x1F, 0x20, 0x23, 0x100, 0x24, 1, 0);
    for (s16 i = 0; i < 9; i++) {
        gfx_set_colour(s16(ds_u16(DS_tick_mark_colour)));
        set_draw_page(1);
        const u8 dx = ds_u8(u16(TICK_MARK + 2 * i)), dy = ds_u8(u16(TICK_MARK + 1 + 2 * i));
        gfx_put_pixel(s16(u16(x + dx - 1)), s16(u16(y + dy - 1)));
        gfx_move_to(s16(u16(x + dx - 2)), s16(u16(y + dy - 1)));
        menu_cursor_draw(y0, y1);
        bios_wait_ticks(1);
    }
    wait_key(9);
}

// 020d:0824 choice_menu (game_flow.md §6): a pencil menu over the {x, y} items at DS:items; the
// keys 91h..99h move by the signed deltas at DS:deltas (95h does nothing). Returns the index on
// Enter (after the tick mark), FEh on key_fe, FDh on key_fd, FFh on the time-out, which never
// comes: the idle count is always 0 when it is tested, so only timeout + 1 <= 0 returns FFh (at
// once). key_delay (2 after a move) blocks moves and the two keys, not Enter, and can be left set.
u16 choice_menu(u16 timeout, u16 items, u16 deltas, u16 y0, u16 y1, u16 key_fe, u16 count, u16 key_fd)
{
    set_draw_page(0);
    gfx_copy_rect_to_copy_page(0, 0x13F, 0x28, 0xC7);
    u16 cur = 0;
    ds_u16(DS_key_delay) = 2;
    if (items == 0x7ED3) cur = u16(count - 1);  // the region menu starts on the last region
    menu_cursor_move(cur, items, y0, y1);
    u16 idle = 0;
    for (;;) {
        if (!(s16(u16(timeout + 1)) > s16(idle))) return 0xFF;
        bios_wait_ticks(1);
        if (ds_u16(DS_key_delay) != 0) ds_u16(DS_key_delay)--;
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        if (key != 0) {
            idle = 0;
            if (key == 0x0D) {
                const u16 si = u16(items + cur * 4);
                menu_tick_mark(ds_u16(si), ds_u16(u16(si + 2)), y0, y1);
                gfx_copy_rect_from_copy_page(0x20, 0xEF, y0, y1);
                return cur;
            }
        }
        if (key == 0) continue;
        if (ds_u16(DS_key_delay) != 0) continue;
        const u16 old = cur;
        if (key == key_fe) return 0xFE;
        if (key == key_fd) return 0xFD;
        const s16 k = s16(key);
        if (k < 0x91 || (k > 0x94 && k < 0x96) || k > 0x99) {
            idle = 0;
            continue;
        }
        const u16 cand = u16(s8(ds_u8(u16(deltas + key - 0x91))) + cur);
        if (s16(cand) >= 0 && s16(cand) < s16(count)) cur = cand;
        if (old != cur) {
            menu_cursor_move(cur, items, y0, y1);
            ds_u16(DS_key_delay) = 2;
        }
        idle = 0;
    }
}

// 020d:0b28 roster_load (game_flow.md §5): GBROSTER.DAT into the roster if it is exactly 703 bytes
// and its check byte (XOR of bytes 0..701, ^ 5Bh) is right; otherwise the default roster stays.
void roster_load()
{
    ds_u16(DS_flow_file) = crt_fopen(S_ROSTER, S_ROSTER_RB);
    if (ds_u16(DS_flow_file) == 0) return;
    if (crt_fread(PIC, 1, ROSTER_SIZE, ds_u16(DS_flow_file)) == ROSTER_SIZE &&
        crt_fread(PIC, 1, 1, ds_u16(DS_flow_file)) == 0) {
        u16 check = 0;
        for (s16 i = 0; i < 0x2BE; i++) check ^= ds_u8(u16(PIC + i));
        check ^= 0x5B;  // XOR of the low byte
        if (check == ds_u8(u16(PIC + 0x2BE)))
            for (s16 i = 0; i < s16(ROSTER_SIZE); i++) ds_u8(u16(DS_roster + i)) = ds_u8(u16(PIC + i));
    }
    crt_fclose(ds_u16(DS_flow_file));
}

// 020d:0bdc roster_save (game_flow.md §5): the roster with its check byte to GBROSTER.DAT; a short
// write is fatal error 3. (The original does not check the fopen.)
void roster_save()
{
    ds_u16(DS_flow_file) = crt_fopen(S_ROSTER_W, S_ROSTER_WB);
    u16 check = 0;
    for (s16 i = 0; i < 0x2BE; i++) check ^= ds_u8(u16(DS_roster + i));
    check ^= 0x5B;
    ds_u8(DS_roster_check) = u8(check);
    if (s16(crt_fwrite(DS_roster, 1, ROSTER_SIZE, ds_u16(DS_flow_file))) < s16(ROSTER_SIZE)) fatal_exit(3);
    crt_fclose(ds_u16(DS_flow_file));
}

// 020d:0008 hq_quiz (game_flow.md §4): the headquarters' question (a ship class and a figure); the
// player types up to 5 digits or '.' and Enter. In this GB.EXE the answer check is disabled (an
// unconditional jump at 020d:0414 where the compiled check has a conditional one), so every answer
// passes: "Right. Welcome to Headquarters.", the roster loaded, and 0 returned. The port keeps that
// code as it is (PORT: nothing to skip).
u16 hq_quiz()
{
    engine_sound_on();
    pal_fade_out_vga();
    engine_sound_off();
    set_draw_page(1);
    ds_u16(DS_tick_mark_colour) = 0;
    file_load_near(S_QUIZCOLR, DS_palette_3d);
    file_load_far(S_HQ1, ds_far(DS_tile_bin_offset));
    decode(DS_tile_bin_offset);
    ega_pal_init();
    gfx_move_to(0, 0x34);
    draw_full(0x22AD, 0x34);
    file_load_far(S_HQ2, ds_far(DS_tile_bin_offset));
    decode(DS_tile_bin_offset);
    gfx_move_to(0, 0x69);
    draw_full(0x1EC5, 0x69);
    file_load_far(S_HQ3, ds_far(DS_tile_bin_offset));
    decode(DS_tile_bin_offset);
    gfx_move_to(0, 0x9D);
    draw_full(0x19DB, 0x9D);
    gfx_set_colour(7);
    gfx_fill_rect(0, 0x13F, 0x9E, 0xC7);
    text_set_colours(0, 7);
    set_draw_page(0);
    gfx_copy_rect_from_copy_page(0, 0x13F, 0, 0xC7);
    engine_sound_on();
    pal_fade_in_vga();
    engine_sound_off();

    // a question with an answer for the ship class
    u16 pick = 7, question = 0, cls = 0, answer = 0;
    while ((pick & 7) == 7) {
        pick = random();
        pick = pit_random(pick);
        question = pick & 0x0F;
        if (s16(question) >= 0x0B) question = u16(question - 0x0B);
        cls = pick & 0xF0;
        if (s16(cls) >= 0xC0) cls = u16(cls - 0x40);
        answer = u16((s16(cls) / 16) * 0x37 + question * 5);
        pick = ds_u8(u16(ANSWERS + 1 + answer)) == 0xFC ? 7 : 8;
    }
    u16 text = u16(ds_u16(u16(QUESTION_TEXTS + 2 * question)) + world_a_base());
    u16 counter = print_records(text, 0);
    text = u16(ds_u16(u16(CLASS_TEXTS + 2 * (s16(cls) / 16))) + world_a_base());
    counter = print_records(text, 0);
    gfx_copy_rect_to_copy_page(0, 0x13F, 0x9E, 0xC7);
    file_load_far(S_HQARM, ds_far(DS_tile_bin_offset));
    u16 typed = 0;
    ds_u16(DS_quiz_digit) = 1;
    for (s16 i = 0; i < 0x14; i++) ds_u8(u16(DS_name_input + i)) = ' ';
    engine_sound_on();

    // the answer, typed
    while (ds_u16(DS_quiz_digit) == 1) {
        quiz_cell(typed, u16((counter & 3) + 6));  // the blinking cursor
        counter++;
        bios_wait_ticks(1);
        random();
        set_draw_page(0);
        gfx_copy_rect_from_copy_page(0x98, 0xAF, 0x33, 0x34);
        u16 key = 0;
        input_read_key(&key);
        if (key == 0) continue;
        if (key == 0x0D && typed != 0) {
            quiz_cell(typed, 7);
            counter = 0;  // the wrong-answer count
            ds_u16(DS_quiz_digit) = 1;
            do {
                const u16 e = ds_u8(u16(ANSWERS + answer + ds_u16(DS_quiz_digit)));
                // the expected character, 0FFh - the nibbles swapped; compared with the typed one at
                // 020d:0412, then an unconditional jump: the "wrong" branch (count + 1, digit + 0Eh)
                // is never taken in this GB.EXE
                (void)e;
                ds_u16(DS_quiz_digit)++;
            } while (s16(ds_u16(DS_quiz_digit)) < 6);
        }
        if (key == '.' || (s16(key) > 0x2F && s16(key) < 0x3A)) {
            if (s16(typed) < 5) {
                ds_u8(u16(DS_name_input + typed)) = u8(key);
                quiz_cell(typed, 6);
                text_goto_cell(0x17, s16(typed + 0x10));
                text_set_colours(0, 7);
                const u8 c = u8(key);
                text_draw_char(&c);
                typed++;
            }
        }
        if (key == 8 && s16(typed) > 0) {  // Backspace (the glyph stays drawn)
            quiz_cell(typed, 7);
            typed--;
            ds_u8(u16(DS_name_input + typed)) = ' ';
        }
    }

    // the result
    gfx_set_colour(7);
    gfx_fill_rect(0, 0x13F, 0x9E, 0xC7);
    engine_sound_off();
    u16 result;
    if (counter != 0) {  // wrong (not reached in this GB.EXE)
        print_records(TEXT_WRONG, 0);
        set_draw_page(1);
        decode(DS_tile_bin_offset);
        gfx_move_to(0x70, 0x79);
        picture_draw(PIC, 0x0310, 0x20);
        set_draw_page(0);
        gfx_copy_rect_from_copy_page(0x70, 0x8F, 0x45, 0x79);
        result = 1;
    } else {
        print_records(TEXT_RIGHT, 0);
        result = 0;
    }
    u16 ok = 0;
    do {  // the disk-2 check: the port has its files, so it passes at once
        ds_u16(DS_flow_file) = crt_fopen(S_DATAB, S_DATAB_RB);
        if (ds_u16(DS_flow_file) == 0) {
            print_records(TEXT_INSERT_DISK2, 0);
            ok = 0;
        } else {
            crt_fclose(ds_u16(DS_flow_file));
            ok = 1;
        }
        wait_key(ok);
    } while (ok == 0);
    roster_load();
    wait_key(0x32);
    return result;
}

} // namespace gb
