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

// 0919:1528 message_sequencer (simulation.md §9.1, world group): the message timer counts down; at 0,
// state 2 ends the mission, state 1 shows the queued reply (or the identification when nothing is
// queued and an object was identified, or clears the line).
void message_sequencer()
{
    if (ds_u8(DS_message_timer) != 0) {
        ds_u8(DS_message_timer)--;
        return;
    }
    const u8 state = ds_u8(DS_message_state);
    if (state == 0) return;
    if (state >= 2) {
        end_mission();
        return;
    }
    u8 al = ds_u8(DS_message_reply);
    if (al == 0 && ds_u16(DS_identified_object) != 0) al = 5;
    show_message(al);
}

// 0919:16e3 heading_readout (simulation.md §9.1, hud.md §4): AL = heading, AH = fraction (the raw
// heading fraction byte). Prints the compass letters compass_points[((AL + 10h) >> 4) & 0Eh] (two
// characters), ':', then the heading in degrees: the 14-bit value (AL >> 2) : (AL bits 0-1 and AH)
// scaled by 65536 / 5B00h and >> 7, printed as three digits (print_3digits with the carry = bit 8).
void heading_readout(u16 ax)
{
    u8 al = u8(ax), ah = u8(ax >> 8);
    print_chars(u16(DS_compass_points + (u8(u8(al + 0x10) >> 4) & 0x0E)), 2);
    print_colon();
    const bool c1 = al & 1;  // SHR AL,1 / OR AH,8
    al >>= 1;
    if (c1) ah |= 8;
    ah = u8(ah << 4);
    const bool c2 = al & 1;  // SHR AL,1 / RCR AH,1
    al >>= 1;
    ah = u8((c2 ? 0x80 : 0) | (ah >> 1));
    const u16 value = u16(al << 8 | ah);  // AL and AH exchanged through BL
    u16 q = div32_16(u32(value) << 16, 0x5B00);
    q = u16(q >> 7);
    print_3digits(u8(q), (q >> 8) != 0);
}

// 0919:17d4 message_line_draw (simulation.md §9.1, hud.md §4): once per frame, in text colour 4 (2
// in CGA mode 4). The readout's cell by station: chase view (11h, row 4Eh); bow (11h, row 85h, or
// 22h, row 64h with the second bow weapon); midship and stern (20h, row 84h); other stations above
// 4: nothing (and the colour stays 4). The pilot (station 0, 1) looking left or ahead first gets the
// clock at row B0h (column 0Fh / 2) when the minutes changed, then his readout at row B0h, column 22h
// / 15h / 8 for looking left / ahead / right. The readout is the heading (the hull's at the pilot's
// station, else the view heading) with the view fraction, redrawn when it changed. Colour 0Fh after.
void message_line_draw()
{
    text_set_colours(u8(ds_u16(DS_video_mode)) == 4 ? 2 : 4, 0);
    u16 bx = 0x11, cx = 0x4E;
    if (ds_u8(DS_chase_view) == 0) {
        const u8 st = u8(ds_u16(DS_station));
        bx = 0x11;
        cx = 0x85;
        if (ds_u8(DS_bow_weapon) != 0) {
            bx = 0x22;
            cx = 0x64;
        }
        if (st < 2) {
            const u8 look = u8(ds_u16(DS_look_direction));
            if (look <= 1) {
                text_goto(0xB0, look == 1 ? 2 : 0x0F);
                if (ds_u8(DS_clock_minutes) != ds_u8(DS_readout_minutes_cache)) {
                    ds_u8(DS_readout_minutes_cache) = ds_u8(DS_clock_minutes);
                    print_bcd_2digits(ds_u8(DS_clock_hours));
                    print_colon();
                    ds_u8(DS_scratch_b7e3) = 1;
                    print_digits_tens(ds_u8(DS_clock_minutes), 1);
                }
            }
            cx = 0xB0;
            bx = look == 1 ? 0x15 : look > 1 ? 8 : 0x22;
        } else if (st != 2) {
            bx = 0x20;
            cx = 0x84;
            if (st != 3 && st != 4) return;
        }
    }
    text_goto(s16(cx), s16(bx));
    u8 al = ds_u8(DS_view_heading);
    if (ds_u16(DS_station) == 1) al = ds_u8(DS_heading);
    const u16 ax = u16(ds_u8(DS_view_heading_fraction) << 8 | al);
    if (ax != ds_u16(DS_readout_heading_cache)) {
        ds_u16(DS_readout_heading_cache) = ax;
        heading_readout(ax);
    }
    text_set_colours(0x0F, 0);
}

} // namespace gb
