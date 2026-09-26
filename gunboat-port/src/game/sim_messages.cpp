// Messages and readout digits (simulation.md §9.1): the crew's message line and the helpers that
// print the clock and heading numbers of message_line_draw (hud).
#include "game/sim.hpp"

#include "game/flow.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Characters from a code-segment string (message_table and component_names entries), each stored in
// scratch_b7dc and drawn from there, up to a byte >= 80h. Returns the offset of that byte.
u16 print_code_text(u16 bx)
{
    for (;;) {
        const u8 al = seg_u8(CSSEG_message_table, bx);
        if (al & 0x80) return bx;
        ds_u8(DS_scratch_b7dc) = al;
        text_draw_char(&ds_u8(DS_scratch_b7dc));
        bx++;
    }
}

// The line's reply timing (0919:161b): shown for 0Ch world passes, message state 1.
void show_for_a_while()
{
    ds_u8(DS_message_timer) = 0x0C;
    ds_u8(DS_message_state) = 1;
}

} // namespace

// 0919:1558 print_colon (simulation.md §9.1): readout_colon (':') at the text cursor.
void print_colon() { text_draw_char(&ds_u8(DS_readout_colon)); }

// 0919:1565 show_message_page0 (simulation.md §9.1): show_message on page 0 (the visible page) from
// the drawing part of the frame, then drawing continues on page 1.
void show_message_page0(u8 al)
{
    ds_u16(DS_draw_page) = 0;
    gfx_set_draw_page(0);
    show_message(al);
    ds_u16(DS_draw_page) = 1;
    gfx_set_draw_page(1);
}

// 0919:1589 show_message_far (simulation.md §9.1): the far entry (input_read_key's pause messages).
void show_message_far(u16 message) { show_message(u8(message)); }

// 0919:1594 show_message (simulation.md §9.1): message AL of message_table on the message line.
// Nothing while the mission is ending (message_state 2). The line is cleared (message_blank at
// column 6, row message_row), the readout caches are reset, and message 0 only sets the state to 0.
// Otherwise the text is printed and its attribute byte decides what follows: a reply queued in
// message_reply (1, 3: "Aye-aye, sir!"; 2: "-he's dead, sir." if the bow gunner is dead, else the
// identification 5), a component name (4, then "No damage possible." in practice or missions 0-1),
// the identified object's name (5), or the end of the mission (6 with a score word, 7 and above).
void show_message(u8 al)
{
    if (ds_u8(DS_message_state) == 2) return;
    ds_u8(DS_message_reply) = 0;
    text_goto(s16(ds_u8(DS_message_row)), 6);
    print_text(DS_message_blank, 0);
    ds_u16(DS_readout_cache_spare) = 0xFFFF;
    ds_u16(DS_readout_heading_cache) = 0xFFFF;
    text_goto(s16(ds_u8(DS_message_row)), 6);
    if (al == 0) {
        ds_u8(DS_message_state) = 0;
        return;
    }
    u16 bx = seg_u16(CSSEG_message_table, u16(CS_message_table + 2 * al));
    const u8 attribute = seg_u8(CSSEG_message_table, bx);
    print_code_text(u16(bx + 1));
    switch (attribute) {
    case 0:
        break;
    case 1:
    case 3:
        ds_u8(DS_message_reply) = 2;
        break;
    case 2:
        if ((ds_u8(DS_gunners_mate_condition) & 3) == 2) {
            ds_u16(DS_identified_object) = 0;
            ds_u8(DS_message_reply) = 0x12;
        } else {
            ds_u8(DS_message_reply) = 5;
        }
        break;
    case 4:
        bx = seg_u16(CSSEG_component_names, u16(CS_component_names + 2 * ds_u8(DS_hit_thresholds)));
        print_code_text(bx);
        if (u8(ds_u16(DS_practice_mode)) != 0 || (u8(ds_u16(DS_mission_number)) & 7) <= 1)
            ds_u8(DS_message_reply) = 0x26;
        break;
    case 5: {
        u16 kind = ds_u16(DS_identified_object);
        if (kind != 0) {
            kind = ds_u8(u16(DS_object_word + kind));
            if (kind == 0x3C) kind = 0x39;
            ds_u16(DS_identified_object) = 0;
        }
        print_text(u16(ds_u16(u16(DS_object_name_offset + 2 * kind)) + DS_world_a_data), 0);
        break;
    }
    default:
        if (attribute < 7) ds_u16(DS_score_words + 2) = bcd_inc(ds_u16(DS_score_words + 2));
        ds_u8(DS_message_state) = 2;
        ds_u8(DS_message_timer) = 0x1E;
        return;
    }
    show_for_a_while();
}

// 0919:174e print_bcd_2digits (simulation.md §9.1): a BCD byte converted to binary (low nibble + 10 *
// high nibble), printed as two digits (digit_text + 1).
void print_bcd_2digits(u8 al)
{
    u8 ah = al & 0xF0;
    al &= 0x0F;
    ah = u8(ah >> 1);
    al = u8(al + ah);
    ah = u8(ah >> 2);
    al = u8(al + ah);
    ds_u8(DS_scratch_b7e3) = 1;
    print_digits_hundreds(al, false);
}

// 0919:1769 print_3digits (simulation.md §9.1): CF*256 + AL printed as three digits.
void print_3digits(u8 al, bool cf)
{
    ds_u8(DS_scratch_b7e3) = 0;
    print_digits_hundreds(al, cf);
}

// 0919:176e print_digits_hundreds (simulation.md §9.1): the hundreds digit of CF*256 + AL into
// digit_text[0] (a blank for none), then the tens. With CF the value starts at 200 + (AL + 38h); if
// that addition carries, the hundreds digit becomes 3 and the carry is lost (quirk: 456..511 print
// as 300..355; the heading readout never passes 359).
void print_digits_hundreds(u8 al, bool cf)
{
    u8 cl = 0, ah = '0';
    if (cf) {
        cl++;
        ah = '2';
        const bool carry = al + 0x38 > 0xFF;
        al = u8(al + 0x38);
        if (carry) ah++;
    }
    while (al >= 100) {
        al = u8(al - 100);
        cl++;
        ah++;
    }
    if (cl == 0) ah = ' ';
    ds_u8(DS_digit_text) = ah;
    print_digits_tens(al, cl);
}

// 0919:1799 print_digits_tens (simulation.md §9.1): the tens digit of AL into digit_text[1] (a blank
// when zero and nothing was counted before, CL = 0), the units into digit_text[2]; then prints
// 3 - B7E3 characters from digit_text + B7E3.
void print_digits_tens(u8 al, u8 cl)
{
    u8 ah = '0';
    while (al >= 10) {
        al = u8(al - 10);
        cl++;
        ah++;
    }
    if (cl == 0) ah = ' ';
    ds_u8(DS_digit_text + 1) = ah;
    ds_u8(DS_digit_text + 2) = u8('0' + al);
    const u8 skip = ds_u8(DS_scratch_b7e3);
    print_chars(u16(DS_digit_text + skip), u8(3 - skip));
}

} // namespace gb
