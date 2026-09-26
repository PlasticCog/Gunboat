// The front end (game_flow.md §6), segment 02d2: the state machine between missions, the name entry,
// the roster, the personnel files, the spec sheets, the mission folders, the assignment map, the
// outfitting and the debrief. Texts are DAT5.DAT records at DS:6E54.. (print_records).
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

constexpr u16 S_DAT5 = 0x0A2C, S_GENCOLR = 0x0A35, S_GENB = 0x0A41, S_GENC = 0x0A49, S_FOLDER = 0x0A51,
              S_SPEC1 = 0x0A5B, S_SPEC2 = 0x0A64, S_SPEC3 = 0x0A6D, S_ADHEAD = 0x0A76, S_SPEC4 = 0x0A80,
              S_SPEC5 = 0x0A89, S_SPEC6 = 0x0A92, S_SMALL = 0x0A9B, S_INSIG = 0x0AA4, S_MP5A = 0x0AB6,
              S_MP5B = 0x0ABE;
constexpr u16 S_MPA = 0x0AC6, S_MPB = 0x0ACE;  // "MP2A.LZ" + 16 * region (MP2/MP3/MP4)
constexpr u16 S_DATN = 0x0B26;                 // "DAT1.DAT" + 9 * region
constexpr u16 S_TJL = 0x0B0B;                  // "TJL" and 14 spaces
constexpr u16 DAT5 = 0x6E54;

// DAT5 texts and tables (DGROUP offsets)
constexpr u16 T_MOMENT = 0x741A, T_ENTER_RESULT = 0xAEF0, T_OFFERS = 0x7668, T_IMPATIENT = 0x777B,
              T_FINAL_ORDERS = 0x7858, T_NAME_ENTRY = 0x73B4, T_HI_TOM = 0x73FF, T_WELCOME = 0x76C1,
              T_WELCOME_BACK = 0x743A, T_WHICH_REGION = 0x745D, T_REGIONS_3 = 0x7497,
              T_REGIONS_2 = 0x7480, T_ROSTER = 0x7A82, T_PERSONNEL = 0x7942, T_SPEC_KEYS = 0x9F54,
              T_OUTFIT_RETURN = 0xAEC5, T_VACATION = 0x9F3D, T_MISSION_KEYS = 0x9EFE,
              T_INVULNERABLE = 0xB31E, T_NIGHT_A = 0xADBF, T_DAY_A = 0xADD0, T_MAP_KEYS = 0x7BC2,
              T_OUTFIT_KEYS = 0x9ED3, T_DAY_I = 0xADB0, T_NIGHT_I = 0xAD9F, T_OUTFIT = 0xADDF,
              T_SHOT_MY_MEN = 0xAFE0, T_KILLED = 0xAF2E, T_PROMOTED = 0xB012, T_MEDAL = 0xB135,
              T_SCORES = 0xB1A4;
constexpr u16 CROSSED_BOX = 0x9F34;  // the character '\' (a crossed box in the font)
constexpr u16 RANK_TITLES = 0x7B1C;  // 10 characters per rank
constexpr u16 REGION_ITEMS = 0x7ED3, ROW_DELTAS = 0x7EC1, ROSTER_ITEMS = 0x7BED, SLOT_ITEMS = 0x7BF9,
              COLUMN_DELTAS = 0x7ECA;
constexpr u16 FILE_STATS = 0xB4A8, FILE_STAT_CELLS = 0x7EDF, MISSION_STATS = 0xB4C4,
              MISSION_STAT_CELLS = 0xB27F;
constexpr u16 SPEC_TEXTS = 0xB47E;                  // text page pointers by spec page (EXE)
constexpr u16 BRIEFINGS = 0xB2DB;                   // DAT5-relative, 8 per region
constexpr u16 MISSION_MEDAL = 0xAD7F;               // medal icon by 8 * region + mission
constexpr u16 OBJECTIVES = 0xB3C0;                  // (x, y) by 8 * region + mission
constexpr u16 OUTFIT_TOP = 0xAD97, OUTFIT_BOTTOM = 0xAD9B;
constexpr u16 RESULT_TEXTS = 0xB472, RANK_WORDS = 0xB456;  // absolute pointers (EXE)
constexpr u16 PROMOTION_RANK = 0xB2C3, MISSION_MEDAL_BIT = 0xB293;
constexpr u16 ROSTER_RECORDS = 0xB550;  // 13 records of 50 bytes after the count (DS_roster)
constexpr u16 REC_SIZE = 50;

u16 record(u16 slot) { return u16(ROSTER_RECORDS + REC_SIZE * slot); }
u8 record_rank(u16 slot) { return ds_u8(u16(record(slot) + 20)); }

// The rank title and the name of a roster record at the text cursor.
void print_commander(u16 slot)
{
    print_chars(u16(RANK_TITLES + 10 * record_rank(slot)), 0x0A);
    print_chars(record(slot), 0x11);
}

// The key line on row 24 (white on black), then the rest of the same record group in black on grey:
// the idiom of most front-end screens.
u16 print_keys_and_text(u16 text)
{
    text_set_colours(0x0F, 0);
    u16 off = print_records(text, 0);
    text_set_colours(0, 7);
    return print_records(text, off);
}

void officer_neutral() { gfx_copy_rect(0, 0x1F, 0x20, 0x23, 0x100, 0x24, 1, 0); }

// ---- the front end's states 4, 11 and 12, inline in front_end

// State 4 (02d2:03a6): the region brief on a folder, then the conditions and the number of missions.
void region_brief()
{
    folder_draw('S', u8(ds_u8(DS_region) + '1'));
    text_set_colours(0x0F, 0);
    print_records(T_ENTER_RESULT, 0);
    text_set_colours(4, 7);
    const u16 text = u16(ds_u16(u16(DS_region_brief_texts + 2 * ds_u16(DS_region))) + world_a_base());
    u16 off = print_records(text, 0);
    text_set_colours(0, 7);
    print_records(text, off);
    folder_present();
    wait_key_idle(0);
    office_restore();
    set_draw_page(0);
    ds_u16(DS_sea_state) = random() & 3;
    const u16 rank = ds_u8(DS_rank);
    ds_u8(DS_missions_digit) = ds_u8(u16(DS_missions_digit_by_rank + rank));
    ds_u16(DS_missions_offered) = ds_u8(u16(DS_missions_by_rank + rank));
    if ((ds_u16(DS_region) == 0 && ds_u8(DS_rank) >= 5) || (ds_u16(DS_region) == 1 && ds_u8(DS_rank) >= 9)) {
        ds_u8(DS_missions_digit) = '8';
        ds_u16(DS_missions_offered) = 8;
    }
    text_set_colours(0, 7);
    print_records(u16(ds_u16(u16(DS_region_orders_texts + 2 * ds_u16(DS_region))) + world_a_base()), 0);
    print_records(ds_u16(u16(DS_sea_state_texts + 2 * ds_u16(DS_sea_state))), 0);
    print_keys_and_text(T_OFFERS);
    ds_u16(DS_mission_number) = u16(ds_u16(DS_missions_offered) - 1);
    gfx_copy_rect_to_copy_page(0, 0x13F, 0x90, 0xC7);
    engine_sound_off();
    const u16 region = ds_u16(DS_region);
    file_load_near(u16(S_DATN + 9 * region), DS_mission_record);
    file_load_far(u16(S_MPA + 16 * region), ds_far(DS_map_a_far));
    ds_u16(DS_map_a_runs) = ds_u16(u16(DS_map_sheet_sizes + 4 * region));
    file_load_far(u16(S_MPB + 16 * region), ds_far(DS_map_sheet_b_far));
    ds_u16(DS_map_b_runs) = ds_u16(u16(DS_map_sheet_sizes + 2 + 4 * region));
    engine_sound_on();
    wait_key_idle(0);
    ds_u16(DS_front_end_state) = 5;
}

// State 11 (02d2:0616): the final orders (after "It's about time you decided!" when the officer's
// patience, the mood, ran out). The front end ends; the mission follows.
void final_orders()
{
    office_restore();
    set_draw_page(0);
    if (ds_u16(DS_office_mood) == 0) {
        print_keys_and_text(T_IMPATIENT);
        wait_key_idle(0);
        speech_clear();
    }
    print_keys_and_text(T_FINAL_ORDERS);
    wait_key_idle(0);
    ds_u16(DS_front_end_state) = 0x0C;
    ds_u16(DS_front_end_running) = 0;
}

// State 12 (02d2:06e0): after the mission, the debrief and the roster file.
void after_mission()
{
    debrief();
    roster_update();
    engine_sound_off();
    bios_wait_ticks(4);
    roster_save();
    ds_u16(DS_front_end_state) = 0;
    bios_wait_ticks(4);
    engine_sound_on();
}

} // namespace

// 02d2:0008 front_end (game_flow.md §6): the office's pictures and the folders' pictures loaded,
// then the states in DS:0084 until state 11 ends the front end (the mission follows; main calls
// front_end again after it, in state 12).
void front_end()
{
    ds_u16(DS_map_a_far) = u16(ds_u16(DS_bow_art1_far) + 0x125C);  // offset only, no carry
    ds_u16(u16(DS_map_a_far + 2)) = ds_u16(u16(DS_bow_art1_far + 2));
    ds_u16(DS_small_far) = u16(ds_u16(DS_tile_bin_offset) + 0x2710);
    ds_u16(u16(DS_small_far + 2)) = ds_u16(DS_tile_bin_segment);
    engine_sound_on();
    pal_fade_out_vga();
    engine_sound_off();
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 9 || mode == 0x0D) {
        // PORT: ega_pal_register(8, DS:0934) (147c:000f), EGA and Tandy only, is not ported.
    }
    file_load_near(S_DAT5, DAT5);
    ds_u16(DS_office_face_hold) = 0;
    ds_u16(DS_front_end_running) = 1;
    ds_u8(DS_spec_page) = 0;
    ds_u8(DS_outfit_visited) = 0;
    ds_u8(DS_mission_chosen) = 0;
    ds_u8(DS_replace_mode) = 0;
    ds_u16(DS_dat5_7ef9) = 0;
    file_load_near(S_GENCOLR, DS_palette_3d);
    file_load_far(S_GENB, ds_far(DS_stern_art2_far));
    file_load_far(S_GENC, ds_far(DS_world_b_far));
    office_draw();
    ds_u8(DS_office_hidden) = 0;
    text_set_colours(0, 7);
    print_records(T_MOMENT, 0);
    file_load_far(S_FOLDER, ds_far(DS_bd2_far));
    file_load_far(S_SPEC1, ds_far(DS_clip_far));
    file_load_far(S_SPEC2, ds_far(DS_tile_bin_offset));
    file_load_far(S_SPEC3, ds_far(DS_bow_art2_far));
    set_draw_page(0);
    file_load_far(S_ADHEAD, ds_far(DS_bow_art1_far));
    decode(DS_bow_art1_far);
    gfx_move_to(0xF0, 0x43);
    picture_draw(PIC, 0x08C2, 0x48);  // the officer's head while the rest loads
    file_load_far(S_SPEC4, ds_far(DS_pictures_far));
    file_load_far(S_SPEC5, ds_far(DS_clip_1838_far));
    file_load_far(S_SPEC6, ds_far(DS_midship_art2_far));
    file_load_far(S_SMALL, ds_far(DS_small_far));
    set_draw_page(0);
    gfx_copy_rect_from_copy_page(0xF0, 0x137, 2, 0x43);
    set_draw_page(1);
    gfx_move_to(0, 0x27);
    file_load_far(S_INSIG, ds_far(DS_bow_art1_far));
    decode(DS_bow_art1_far);
    if (ds_u16(DS_video_mode) == 4) {
        ega_pal_entry(1, 0x2AA);
        ega_pal_entry(3, 0x255);
        ega_pal_entry(4, 0x200);
        ega_pal_entry(5, 0x255);
        ega_pal_entry(6, 0x2AA);
        ega_pal_entry(7, 0x2FF);
        ega_pal_entry(9, 0x2AA);
        ega_pal_entry(0x0D, 0x255);
        ega_pal_entry(0x0E, 0x255);
        ega_pal_entry(0x1D, 0x200);
    }
    picture_draw(PIC, 0x0E27, 0x100);  // the sprite sheet (insignia, medals, faces) on page 1
    ega_pal_apply();
    file_load_far(S_MP5A, ds_far(DS_bow_art1_far));
    ds_u16(DS_mp5a_runs) = ds_u16(DS_mp5_run_counts);
    file_load_far(S_MP5B, ds_far(DS_bd4_far));
    ds_u16(DS_mp5b_runs) = ds_u16(u16(DS_mp5_run_counts + 2));
    ds_u16(DS_office_blink_count) = 0;
    kbd_flush_key();
    engine_sound_on();
    while (ds_u16(DS_front_end_running) == 1) {
        switch (ds_u16(DS_front_end_state)) {  // jump table 02d2:0718; above 12: nothing
        case 0:
            ds_u16(DS_office_mood) = 1;
            office_face_draw();
            name_entry();
            break;
        case 1: roster_edit(); break;
        case 2: personnel_files(0); break;
        case 3: personnel_files(1); break;
        case 4: region_brief(); break;
        case 5: mission_select(); break;
        case 6: assignment_map(5); break;
        case 7: pbr_specs(5); break;
        case 8: assignment_map(9); break;
        case 9: outfitting(); break;
        case 10: pbr_specs(9); break;
        case 11: final_orders(); break;
        case 12: after_mission(); break;
        default: break;
        }
        officer_neutral();
    }
    engine_sound_off();
}

// 02d2:0bd8 name_entry (game_flow.md §6.1): "Identify yourself, sailor"; up to 17 letters, digits
// and spaces. A name in the roster loads that commander (rank, medals, statistics) and, from rank 5,
// asks for the region; a new name goes to the roster screen; F1 to the personnel files. Returns 0
// after Enter; after F1 AX is whatever it was (PORT: 0; front_end ignores it).
u16 name_entry()
{
    office_restore();
    set_draw_page(0);
    u16 blink = print_keys_and_text(T_NAME_ENTRY);
    gfx_copy_rect_to_copy_page(0, 0x13F, 0x90, 0xC7);
    u16 len = 0;
    for (s16 i = 0; i < 0x11; i++) ds_u8(u16(DS_name_input + i)) = ' ';
    while (ds_u16(DS_front_end_state) == 0) {
        gfx_set_colour(s16((blink & 3) + 8));
        u16 x = u16(len * 8 + 0x20);
        gfx_fill_rect(x, u16(x + 7), 0x98, 0x9F);
        blink++;
        bios_wait_ticks(1);
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        if (key != 0) {
            if (key == 0x0D && len != 0) {
                gfx_set_colour(7);
                x = u16(len * 8 + 0x20);
                gfx_fill_rect(x, u16(x + 7), 0x98, 0x9F);
                s16 slot = 0;
                for (; slot < 0x0D; slot++) {
                    ds_u8(DS_rank) = 1;  // the match flag
                    for (s16 i = 0; i < 0x11; i++)
                        if (ds_u8(u16(record(u16(slot)) + i)) != ds_u8(u16(DS_name_input + i))) ds_u8(DS_rank) = 0;
                    if (ds_u8(DS_rank) == 1) {
                        ds_u8(DS_rank) = record_rank(u16(slot));
                        for (s16 i = 0; i < 0x1C; i++)
                            ds_u8(u16(DS_medals + i)) = ds_u8(u16(record(u16(slot)) + 21 + i));
                        break;
                    }
                }
                ds_u16(DS_office_mood) = 2;
                office_face_draw();
                speech_clear();
                text_set_colours(0, 7);
                ds_u8(DS_practice_targets) = 0;
                if (crt_strncmp(DS_name_input, S_TJL, 0x11) == 0) {  // the designer's initials
                    print_records(T_HI_TOM, 0);
                    ds_u8(DS_practice_targets) = 1;
                }
                ds_u16(DS_region) = 0;
                if (ds_u8(DS_rank) == 0) {  // a new commander
                    for (s16 i = 0; i < 0x1C; i++) ds_u8(u16(DS_medals + i)) = 0;
                    ds_u8(DS_rank) = 1;
                    print_keys_and_text(T_WELCOME);
                    wait_key_idle(0);
                    ds_u16(DS_front_end_state) = 1;
                    return 0;
                }
                ds_u16(DS_commander_slot) = u16(slot);
                print_records(T_WELCOME_BACK, 0);
                text_goto_cell(0x13, 7);
                print_commander(ds_u16(DS_commander_slot));
                if (ds_u8(DS_rank) >= 5) {
                    print_records(T_WHICH_REGION, 0);
                    if (ds_u8(DS_rank) >= 9) {
                        print_records(T_REGIONS_3, 0);
                        ds_u16(DS_region) = choice_menu(0, REGION_ITEMS, ROW_DELTAS, 0x94, 0xAF, 0, 3, 0);
                    } else {
                        print_records(T_REGIONS_2, 0);
                        ds_u16(DS_region) = choice_menu(0, REGION_ITEMS, ROW_DELTAS, 0x94, 0xAF, 0, 2, 0);
                    }
                } else {
                    wait_key_idle(0x3C);
                }
                ds_u16(DS_front_end_state) = 4;
                return 0;
            }
            // a character: MSC isalnum (the _ctype table) or a space (not first), in upper case
            if ((ds_u8(u16(DS_crt_ctype + 1 + key)) & 7) != 0 || key == 0x20) {
                if (s16(len) < 0x11 && !(key == 0x20 && len == 0)) {
                    if (s16(key) > 0x60) key = u16(key - 0x20);
                    ds_u8(u16(DS_name_input + len)) = u8(key);
                    gfx_set_colour(7);
                    x = u16(len * 8 + 0x20);
                    gfx_fill_rect(x, u16(x + 7), 0x98, 0x9F);
                    text_goto_cell(0x13, s16(len + 4));
                    text_set_colours(0, 7);
                    const u8 c = u8(key);
                    text_draw_char(&c);
                    len++;
                }
            }
            if (key == 8 && s16(len) > 0) {  // Backspace (the glyph stays drawn)
                gfx_set_colour(7);
                x = u16(len * 8 + 0x20);
                gfx_fill_rect(x, u16(x + 7), 0x98, 0x9F);
                len--;
                ds_u8(u16(DS_name_input + len)) = ' ';
            }
        }
        if (key == 0x81) ds_u16(DS_front_end_state) = 2;  // F1
    }
    return 0;
}

// 02d2:106c roster_edit (game_flow.md §6): the PBR COMMANDERS folder; ADD appends the new commander
// (or overwrites the last of 13), REPLACE picks a slot (an empty one appends), REDO goes back to
// the name entry; F1 to the personnel files. REPLACE mode (DS:B50B) stays set until the next
// front_end, so after F1 the list comes back at once.
void roster_edit()
{
    folder_draw('D', '6');
    print_keys_and_text(T_ROSTER);
    for (s16 i = 0; i < s16(ds_u16(DS_roster)); i++) {
        text_goto_cell(s16(i + 0x0B), 8);
        print_commander(u16(i));
    }
    if (ds_u8(DS_replace_mode) != 0) {
        text_goto_cell(8, 0x10);
        text_draw_char(&ds_u8(CROSSED_BOX));
    }
    folder_present();
    auto append = [] {  // 02d2:11ba
        ds_u16(DS_commander_slot) = ds_u16(DS_roster);
        ds_u16(DS_roster)++;
    };
    auto new_record = [] {  // 02d2:11cb
        roster_new_record(ds_u16(DS_commander_slot));
        ds_u16(DS_front_end_state) = 4;
    };
    while (ds_u16(DS_front_end_state) == 1) {
        u16 choice = ds_u8(DS_replace_mode);
        if (choice == 0) {
            choice = choice_menu(0, ROSTER_ITEMS, ROW_DELTAS, 0x2E, 0x47, 0x81, 3, 0);
            if (choice == 0xFE) ds_u16(DS_front_end_state) = 3;
        }
        if (choice == 0) {  // ADD
            if (s16(ds_u16(DS_roster)) < 0x0D) append();
            else ds_u16(DS_commander_slot) = u16(ds_u16(DS_roster) - 1);
            new_record();
        } else if (choice == 1) {  // REPLACE
            ds_u8(DS_replace_mode) = 1;
            choice = choice_menu(0, SLOT_ITEMS, COLUMN_DELTAS, 0x3E, 0xBF, 0x81, 0x0D, 0);
            if (choice == 0xFE) {
                ds_u16(DS_front_end_state) = 3;
                continue;
            }
            ds_u16(DS_commander_slot) = choice;
            if (s16(choice) >= s16(ds_u16(DS_roster))) append();
            new_record();
        } else if (choice == 2) {  // REDO
            ds_u16(DS_front_end_state) = 0;
        }
    }
}

// 02d2:123e roster_new_record: the typed name and the rank into roster record `slot`, shown in the
// list, the record's medals and statistics cleared. The original calls gfx_set_colour(0, 7) where
// text_set_colours(0, 7) was meant; only the graphics colour becomes 0 (kept).
void roster_new_record(u16 slot)
{
    for (s16 i = 0; i < 0x11; i++) ds_u8(u16(record(slot) + i)) = ds_u8(u16(DS_name_input + i));
    ds_u8(u16(record(slot) + 20)) = ds_u8(DS_rank);
    text_goto_cell(s16(slot + 0x0B), 8);
    gfx_set_colour(0);
    print_commander(slot);
    for (s16 i = 0x15; i < 0x32; i++) ds_u8(u16(record(slot) + i)) = 0;
    wait_key_idle(0x1E);
}

// 02d2:1306 personnel_files: the file of each commander in turn (arrows), F1 back to ret_state.
void personnel_files(u16 ret_state)
{
    u16 slot = ds_u16(DS_dat5_7ef9);
    personnel_file_show(slot);
    while (ds_u16(DS_front_end_state) == 2 || ds_u16(DS_front_end_state) == 3) {
        bios_wait_ticks(1);
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        switch (key) {  // jump table 02d2:1392 for 81h..99h
        case 0x81:
            ds_u16(DS_dat5_7ef9) = slot;
            ds_u16(DS_front_end_state) = ret_state;
            break;
        case 0x91: case 0x92: case 0x93: case 0x96:
            if (slot == u16(ds_u16(DS_roster) - 1)) slot = 0;
            else slot++;
            personnel_file_show(slot);
            break;
        case 0x94: case 0x97: case 0x98: case 0x99:
            if (slot == 0) slot = u16(ds_u16(DS_roster) - 1);
            else slot--;
            personnel_file_show(slot);
            break;
        default: break;
        }
    }
}

// 02d2:13cc personnel_file_show: the PERSONNEL FILE folder of a commander: rank and name, the
// statistics, the rank insignia and the medals. It copies the record's medals and statistics into
// the working copy (DS:B50E..B529) and its rank into DS:B507 (kept: a commander added afterwards
// inherits them).
void personnel_file_show(u16 slot)
{
    folder_draw('E', u8(slot + '1'));
    print_keys_and_text(T_PERSONNEL);
    ds_u8(u16(DS_folder_code + 3)) = u8(slot + '1');
    text_goto_cell(7, 0x22);
    text_draw_char(&ds_u8(u16(DS_folder_code + 3)));
    text_goto_cell(0x0D, 7);
    print_commander(slot);
    ds_u8(DS_file_rank) = record_rank(slot);
    for (s16 i = 0; i < 0x1C; i++) ds_u8(u16(DS_medals + i)) = ds_u8(u16(record(slot) + 21 + i));
    bcd_stats_print(FILE_STATS, FILE_STAT_CELLS);
    gfx_copy_rect(0xD0, 0xFF, 0, 0x0F, 0x88, 0x4F, 1, 1);
    const u16 insignia = u16((ds_u8(DS_file_rank) << 4) - 0x10);
    gfx_copy_rect(insignia, u16(insignia + 0x0F), 0, 0x0F, 0x48, 0x5F, 1, 1);
    u16 medals = u16((ds_u8(u16(DS_medals + 1)) << 8) + ds_u8(DS_medals));
    u16 x = 0x58, icon = 0;
    while (medals != 0) {
        if ((u8(medals) & 1) != 0) gfx_copy_rect(icon, u16(icon + 0x0F), 0x10, 0x1F, x, 0x5F, 1, 1);
        icon = u16(icon + 0x10);
        x = u16(x + 0x10);
        medals = u16(s16(medals) >> 1);  // SAR: bit 15 would never clear
    }
    folder_present();
}

// 02d2:1598 bcd_stats_print: the BCD words DS:[table[i]] (a 0-terminated pointer list) at the text
// cells (col, row) of `cells`, 4 digits with leading zeros blanked.
void bcd_stats_print(u16 table, u16 cells)
{
    for (s16 i = 0;; i++) {
        const u16 p = ds_u16(u16(table + 2 * i));
        if (p == 0) break;
        text_goto_cell(ds_u8(u16(cells + 2 * i + 1)), ds_u8(u16(cells + 2 * i)));
        u16 v = ds_u16(p);
        for (s16 d = 3; d >= 0; d--) {
            ds_u8(u16(DS_folder_code + d)) = u8((u8(v) & 0x0F) + '0');
            v = u16(s16(v) >> 4);
        }
        for (s16 d = 0; d < 3; d++) {
            if (ds_u8(u16(DS_folder_code + d)) != '0') break;
            ds_u8(u16(DS_folder_code + d)) = ' ';
        }
        print_chars(DS_folder_code, 4);
    }
}

// 02d2:1626 byte_stats_print: as bcd_stats_print for byte counters in decimal (3 digits), on page 1
// (or the view page in the mission). No caller in GB.EXE.
void byte_stats_print(u16 table, u16 cells)
{
    const u16 page = ds_u16(DS_phase) == 3 ? ds_u16(DS_view_page) : 1;
    set_draw_page(page);
    text_set_colours(0, 7);
    for (s16 i = 0;; i++) {
        const u16 p = ds_u16(u16(table + 2 * i));
        if (p == 0) break;
        const s16 v = ds_u8(p);
        ds_u8(u16(DS_folder_code + 2)) = u8(v % 10 + '0');
        ds_u8(u16(DS_folder_code + 1)) = u8(v % 100 / 10 + '0');
        ds_u8(DS_folder_code) = u8(v / 100 + '0');
        for (s16 d = 0; d < 2; d++) {
            if (ds_u8(u16(DS_folder_code + d)) != '0') break;
            ds_u8(u16(DS_folder_code + d)) = ' ';
        }
        text_goto_cell(ds_u8(u16(cells + 2 * i + 1)), ds_u8(u16(cells + 2 * i)));
        print_chars(DS_folder_code, 3);
    }
    set_draw_page(0);
}

// 02d2:1712 pbr_specs: the PBR EQUIPMENT sheets, pages 0..16 without 13 and 15 (arrows); F2 to the
// map, F3 back. Keys wait while key_delay (left by choice_menu) is set: it is never counted down
// here (kept: the screen can lock).
void pbr_specs(u16 ret_state)
{
    spec_sheet_draw(ret_state);
    while (ds_u16(DS_front_end_state) == 7 || ds_u16(DS_front_end_state) == 0x0A) {
        bios_wait_ticks(1);
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        if (key == 0 || ds_u16(DS_key_delay) != 0) continue;
        u8 &page = ds_u8(DS_spec_page);
        switch (key) {  // jump table 02d2:17EE for 82h..99h
        case 0x82: ds_u16(DS_front_end_state) = ret_state == 5 ? 6 : 8; break;
        case 0x83: ds_u16(DS_front_end_state) = ret_state; break;
        case 0x91: case 0x92: case 0x93: case 0x96:
            page = page == 0x10 ? 0 : u8(page + 1);
            if (page == 0x0F) page = 0x10;
            if (page == 0x0D) page = 0x0E;
            spec_sheet_draw(ret_state);
            break;
        case 0x94: case 0x97: case 0x98: case 0x99:
            page = page == 0 ? 0x10 : u8(page - 1);
            if (page == 0x0F) page = 0x0E;
            if (page == 0x0D) page = 0x0C;
            spec_sheet_draw(ret_state);
            break;
        default: break;
        }
    }
}

// 02d2:1826 spec_sheet_draw: one spec page on a folder: a text page, or a picture (SPEC1..6) with
// its EGA/Tandy colours.
void spec_sheet_draw(u16 ret_state)
{
    const u8 page = ds_u8(DS_spec_page);
    folder_draw('C', u8((page >> 1) + '1'));
    text_set_colours(0x0F, 0);
    u16 off = 0;
    if (ret_state == 5) off = print_records(T_SPEC_KEYS, 0);
    else off = print_records(T_OUTFIT_RETURN, off);
    text_set_colours(0, 7);
    print_records(T_SPEC_KEYS, off);  // the header; works because both key lines are 43 bytes long
    const u16 text = ds_u16(u16(SPEC_TEXTS + 2 * ds_u8(DS_spec_page)));
    if (text != 0) {
        print_records(text, 0);
        folder_present();
        return;
    }
    gfx_move_to(0x20, 0xBE);
    const u16 mode = ds_u16(DS_video_mode);
    u8 *col = &ds_u8(DS_spec_colours);
    if (mode == 4) {
        ega_pal_entry(0, 0x200);
        ega_pal_entry(7, 0x255);
        ega_pal_entry(8, 0x2AA);
        ega_pal_entry(0x0F, 0x2FF);
        ega_pal_entry(0x10, 0x277);
        ega_pal_entry(0x11, 0x266);
        ega_pal_entry(0x12, 0x2AA);
        ega_pal_entry(0x13, 0x200);
    } else if (mode == 9 || mode == 0x0D) {
        col[0] = 0x82;
        col[1] = 0x81;
        col[2] = 8;
        col[3] = 0;
    }
    switch (ds_u8(DS_spec_page)) {
    case 0:
        decode(DS_clip_far);  // SPEC1
        ds_u16(DS_spec_runs) = 0x1B8B;
        col[0] = 7;
        col[1] = 8;
        col[2] = 0x81;
        col[3] = 0x80;
        ega_pal_entry(0x13, 0x222);
        break;
    case 2:
        decode(DS_tile_bin_offset);  // SPEC2
        ds_u16(DS_spec_runs) = 0x2ECA;
        col[3] = 0x80;
        ega_pal_entry(0x13, 0x222);
        break;
    case 4:
        decode(DS_bow_art2_far);  // SPEC3
        ds_u16(DS_spec_runs) = 0x2AEA;
        col[2] = 0x81;
        col[3] = 0x80;
        ega_pal_entry(0x12, 0x266);
        ega_pal_entry(0x13, 0x222);
        break;
    case 6:
        decode(DS_pictures_far);  // SPEC4
        ds_u16(DS_spec_runs) = 0x2CEA;
        col[1] = 7;
        ega_pal_entry(0x11, 0x255);
        break;
    case 8:
        decode(DS_clip_1838_far);  // SPEC5
        ds_u16(DS_spec_runs) = 0x168B;
        col[0] = 7;
        col[1] = 8;
        col[2] = 0x80;
        ega_pal_entry(0x10, 0x255);
        ega_pal_entry(0x11, 0x2AA);
        break;
    case 0x0B:
        decode(DS_midship_art2_far);  // SPEC6
        ds_u16(DS_spec_runs) = 0x2D01;
        break;
    default: ds_u16(DS_spec_runs) = 0; break;
    }
    if (mode == 0x0D || mode == 9) {
        for (s16 i = 0; i < 4; i++) {
            const u8 c = col[i];
            if (c < 0x10) ega_pal_entry(u16(i + 0x10), u16(0x200 + u8(0x11 * c)));
            else ega_pal_entry(u16(i + 0x10), c == 0x80 ? 0x208 : c == 0x81 ? 0x278 : 0x27F);
        }
    }
    if (ds_u16(DS_spec_runs) != 0) picture_draw(PIC, s16(ds_u16(DS_spec_runs)), 0x100);
    ega_pal_apply();
    folder_present();
}

// 02d2:1b6e mission_select: the mission folders (arrows), Enter accepts (the next iteration goes to
// the outfitting); Rest & Relaxation (mission 0) ends the game. F2 map, F4 specs.
void mission_select()
{
    mission_folder_draw();
    while (ds_u16(DS_front_end_state) == 5) {
        bios_wait_ticks(1);
        office_idle();
        if (ds_u8(DS_mission_chosen) != 0) {
            ds_u16(DS_front_end_state) = 9;
            continue;
        }
        u16 key = 0;
        input_read_key(&key);
        if (key != 0 && key == 0x0D) {
            menu_tick_mark(0xD2, 0x43, 0x2B, 0x46);
            gfx_copy_rect_from_copy_page(0x30, 0xEF, 0x2B, 0x46);
            if (ds_u16(DS_mission_number) == 0) {
                set_draw_page(0);
                gfx_set_colour(0);
                gfx_fill_rect(0, 0x13F, 0xC0, 0xC7);
                print_records(T_VACATION, 0);
                wait_key_idle(0x64);
                quit_to_dos();
            }
            ds_u8(DS_mission_chosen) = 1;
            ds_u16(DS_mission_type) = u16((ds_u16(DS_region) << 3) + ds_u16(DS_mission_number));
        }
        if (key == 0 || ds_u16(DS_key_delay) != 0) continue;
        u16 &mission = ds_u16(DS_mission_number);
        switch (key) {  // jump table 02d2:1CBE for 82h..99h
        case 0x82: ds_u16(DS_front_end_state) = 6; break;
        case 0x84: ds_u16(DS_front_end_state) = 7; break;
        case 0x91: case 0x92: case 0x93: case 0x96:
            if (u16(ds_u16(DS_missions_offered) - 1) == mission) mission = 0;
            else mission++;
            mission_folder_draw();
            break;
        case 0x94: case 0x97: case 0x98: case 0x99:
            if (mission == 0) mission = u16(ds_u16(DS_missions_offered) - 1);
            else mission--;
            mission_folder_draw();
            break;
        default: break;
        }
    }
}

// 02d2:1d04 mission_folder_draw: the mission's folder: the briefing, DAY/NIGHT (the byte after the
// briefing text, DS:B7FC), the practice note, the medal it can earn, and the pencil at "# Accept".
void mission_folder_draw()
{
    folder_draw('A', u8(ds_u16(DS_mission_number) + '1'));
    print_keys_and_text(T_MISSION_KEYS);
    const u16 mission = ds_u16(DS_mission_number);
    u16 table;
    switch (ds_u16(DS_region)) {
    case 1: table = u16(BRIEFINGS + 0x10); break;
    case 2: table = u16(BRIEFINGS + 0x20); break;
    default: table = BRIEFINGS; break;
    }
    const u16 text = u16(ds_u16(u16(table + 2 * mission)) + world_a_base());
    const u16 off = print_records(text, 0);
    ds_u8(DS_daylight) = ds_u8(u16(text + off));
    if (ds_u16(DS_mission_number) == 1) print_records(T_INVULNERABLE, 0);
    if (ds_u16(DS_mission_number) != 0) {
        text_set_colours(4, 7);
        if (ds_u8(DS_daylight) == 0) print_records(T_NIGHT_A, 0);
        else print_records(T_DAY_A, 0);
    }
    text_set_colours(0, 7);
    if (ds_u8(DS_mission_chosen) != 0) {
        text_goto_cell(8, 0x1A);
        text_draw_char(&ds_u8(CROSSED_BOX));
    }
    const u16 medal = ds_u8(u16(MISSION_MEDAL + (ds_u16(DS_region) << 3) + ds_u16(DS_mission_number)));
    if (medal != 0) {
        const u16 x = u16(medal << 4);
        gfx_copy_rect(u16(x - 0x10), u16(x - 1), 0x10, 0x1F, 0x100, 0xBB, 1, 1);
    }
    folder_present();
    if (ds_u8(DS_mission_chosen) == 0) {
        gfx_move_to(0xD2, 0x43);
        menu_cursor_draw(0x43, 0x43);
    }
}

// 02d2:1ec2 assignment_map: the sector map of the chosen mission with the objective blinking and the
// boat's start point; arrows change the mission (until one is accepted), F3 back, F4 specs. Keys
// wait while key_delay is set (kept, as in pbr_specs).
void assignment_map(u16 ret_state)
{
    ds_u16(DS_map_drawn) = 0;
    map_draw(ret_state);
    while (ds_u16(DS_front_end_state) == 6 || ds_u16(DS_front_end_state) == 8) {
        bios_wait_ticks(1);
        if (ds_u16(DS_mission_number) != 0) {
            gfx_set_colour(s16(ds_u16(DS_map_blink)++ & 3));
            const u16 si = u16(((ds_u16(DS_region) << 3) + ds_u16(DS_mission_number)) * 2);
            gfx_move_to(ds_u8(u16(OBJECTIVES + si)), s16(ds_u8(u16(OBJECTIVES + 1 + si)) + 2));
            gfx_draw_bitmap(DS_pencil_mask_tip, 1, 8);
            const u8 row = u8(u8(ds_u16(DS_object_y) >> 7) << 1) ^ 0xFF;
            gfx_put_pixel(s16((ds_u16(DS_object_x) >> 6) + 0x18), u8(row + 0xCF));
            gfx_move_to(s16((ds_u16(DS_object_x) >> 6) + 0x15), u8(row + 0xD2));
            gfx_set_colour(s16((ds_u16(DS_map_blink) + 2) & 3));
            gfx_draw_bitmap(DS_pencil_mask_point, 1, 7);
        }
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        if (key == 0 || ds_u16(DS_key_delay) != 0) continue;
        u16 &mission = ds_u16(DS_mission_number);
        switch (key) {  // jump table 02d2:2062 for 83h..99h
        case 0x83: ds_u16(DS_front_end_state) = ret_state; break;
        case 0x84: ds_u16(DS_front_end_state) = ret_state == 5 ? 7 : 0x0A; break;
        case 0x91: case 0x92: case 0x93: case 0x96:
            if (ds_u8(DS_mission_chosen) != 0) break;
            if (u16(ds_u16(DS_missions_offered) - 1) == mission) mission = 0;
            else mission++;
            map_draw(ret_state);
            break;
        case 0x94: case 0x97: case 0x98: case 0x99:
            if (ds_u8(DS_mission_chosen) != 0) break;
            if (mission == 0) mission = u16(ds_u16(DS_missions_offered) - 1);
            else mission--;
            map_draw(ret_state);
            break;
        default: break;
        }
    }
}

// 02d2:209a map_draw: the map folder: Mare Island (MP5A/B) for mission 0, else the region's map
// (MPnA/B); between two real missions page 1 is only shown again (the folder code keeps the first
// mission's digit: kept).
void map_draw(u16 ret_state)
{
    if (!(ds_u16(DS_mission_number) != 0 && ds_u16(DS_map_drawn) != 0)) {
        folder_draw('B', u8(ds_u16(DS_mission_number) + '1'));
        gfx_set_colour(7);
        gfx_fill_rect(0x20, 0x27, 0x35, 0x53);
        text_set_colours(0x0F, 0);
        u16 off = 0;
        if (ret_state == 5) print_records(T_MAP_KEYS, 0);
        else print_records(T_OUTFIT_RETURN, off);
        const u16 mode = ds_u16(DS_video_mode);
        if (mode == 4) {
            ega_pal_entry(0, 0x200);
            ega_pal_entry(2, 0x255);
            ega_pal_entry(3, 0x255);
            ega_pal_entry(9, 0x255);
            ega_pal_entry(0x0A, 0x2FF);
            ega_pal_entry(0x0E, 0x200);
            ega_pal_entry(0x0F, 0x2FF);
            ega_pal_entry(0x1A, 0x2AA);
        } else if (mode == 9 || mode == 0x0D) {
            ega_pal_entry(0x1A, 0x2AA);
        }
        if (ds_u16(DS_mission_number) == 0) {
            decode(DS_bow_art1_far);  // MP5A
            gfx_move_to(0x28, 0x77);
            picture_draw(PIC, s16(ds_u16(DS_mp5a_runs)), 0xF0);
            decode(DS_bd4_far);  // MP5B
            gfx_move_to(0x28, 0xBF);
            picture_draw(PIC, s16(ds_u16(DS_mp5b_runs)), 0xF0);
        } else if (ds_u16(DS_map_drawn) == 0) {
            decode(DS_map_a_far);
            gfx_move_to(0x28, 0x77);
            picture_draw(PIC, s16(ds_u16(DS_map_a_runs)), 0xF0);
            decode(DS_map_sheet_b_far);
            gfx_move_to(0x28, 0xBF);
            picture_draw(PIC, s16(ds_u16(DS_map_b_runs)), 0xF0);
        }
        ega_pal_apply();
    }
    folder_present();
    ds_u16(DS_map_drawn) = ds_u16(DS_mission_number);
}

// 02d2:22aa outfitting (game_flow.md §6.4): the PBR OUTFIT folder: for the bow, the engines, the
// stern and the midship in turn, a line is chosen with up/down and Enter (DS:B804..B807); after the
// fourth, the final orders. F2 map, F4 specs. A move blocks keys for 2 iterations.
void outfitting()
{
    if (ds_u8(DS_outfit_visited) == 0) {
        ds_u8(DS_outfit_visited) = 1;
        ds_u8(DS_stern_weapon) = 0;
        ds_u8(DS_bow_weapon) = 0;
        ds_u8(DS_outfit_item) = 0;
        ds_u8(DS_midship_weapon) = 1;
        ds_u8(DS_engines_upgraded) = 1;
    }
    u16 y = outfitting_draw();
    u16 delay = 2;
    while (ds_u16(DS_front_end_state) == 9) {
        bios_wait_ticks(1);
        if (delay != 0) delay--;
        office_idle();
        u16 key = 0;
        input_read_key(&key);
        if (key != 0 && delay == 0) {
            const u16 item = ds_u8(DS_outfit_item);
            switch (key) {
            case 0x82: ds_u16(DS_front_end_state) = 8; break;
            case 0x84: ds_u16(DS_front_end_state) = 0x0A; break;
            case 0x92: case 0x96:
                if (ds_u8(u16(OUTFIT_TOP + item)) == y) break;
                gfx_copy_rect_from_copy_page(0x20, 0xEF, u16(y - 0x18), u16(y + 2));
                y = u16(y - 8);
                gfx_move_to(0x6A, s16(y));
                menu_cursor_draw(0x43, 0x43);
                delay = 2;
                break;
            case 0x94: case 0x98:
                if (ds_u8(u16(OUTFIT_BOTTOM + item)) == y) break;
                gfx_copy_rect_from_copy_page(0x20, 0xEF, u16(y - 0x18), u16(y + 2));
                y = u16(y + 8);
                gfx_move_to(0x6A, s16(y));
                menu_cursor_draw(0x43, 0x43);
                delay = 2;
                break;
            default: break;
            }
        }
        if (key == 0 || delay != 0 || key != 0x0D) continue;
        const u16 item = ds_u8(DS_outfit_item);
        ds_u8(u16(DS_bow_weapon + item)) = u8(u16(y - ds_u8(u16(OUTFIT_TOP + item))) >> 3);
        menu_tick_mark(0x6A, y, u16(y - 0x18), u16(y + 3));
        gfx_copy_rect_from_copy_page(0x20, 0xEF, u16(y - 0x18), u16(y + 3));
        if (ds_u8(DS_outfit_item) == 3) {
            ds_u16(DS_front_end_state) = 0x0B;
            continue;
        }
        ds_u8(DS_outfit_item)++;
        const u8 next = ds_u8(DS_outfit_item);
        y = ds_u8(u16(OUTFIT_TOP + next));
        if (next == 1) y = u16(y + 8);
        if (ds_u8(DS_outfit_item) == 3) y = u16(y + 8);
        gfx_move_to(0x6A, s16(y));
        menu_cursor_draw(0x43, 0x43);
    }
}

// 02d2:24ac outfitting_draw: the PBR OUTFIT folder with the boat (SMALL.LZ), DAY/NIGHT, the choices
// made so far marked, and the pencil at the current item's default line. Returns the pencil's y.
u16 outfitting_draw()
{
    folder_draw('I', u8(ds_u8(DS_outfit_item) + '1'));
    decode(DS_small_far);
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 4) {
        ega_pal_entry(1, 0x255);
        ega_pal_entry(4, 0x2FF);
        ega_pal_entry(7, 0x2FF);
        ega_pal_entry(8, 0x255);
        ega_pal_entry(9, 0x2AA);
        ega_pal_entry(0x0F, 0x2AA);
        ega_pal_entry(0x10, 0x2AA);
        ega_pal_entry(0x11, 0x2FF);
        ega_pal_entry(0x12, 0x255);
        ega_pal_entry(0x13, 0x2AA);
        ega_pal_entry(0x1A, 0x255);
        ega_pal_entry(0x1C, 0x2FF);
    } else if (mode == 9 || mode == 0x0D) {
        ega_pal_entry(0x10, 0x2FF);
        ega_pal_entry(0x11, 0x266);
        ega_pal_entry(0x12, 0x288);
        ega_pal_entry(0x13, 0x277);
        ega_pal_entry(0x1A, 0x288);
        ega_pal_entry(0x1C, 0x266);
    }
    gfx_move_to(0x38, 0xBA);
    picture_draw(PIC, 0x0798, 0x30);
    ega_pal_apply();
    text_set_colours(0x0F, 0);
    print_records(T_OUTFIT_KEYS, 0);
    text_set_colours(4, 7);
    if (ds_u8(DS_daylight) != 0) print_records(T_DAY_I, 0);
    else print_records(T_NIGHT_I, 0);
    text_set_colours(0, 7);
    print_records(T_OUTFIT, 0);
    u16 row = 0x0B;
    for (u16 i = 0; i < ds_u8(DS_outfit_item); i++) {
        text_goto_cell(s16(ds_u8(u16(DS_bow_weapon + i)) + row), 0x0D);
        text_draw_char(&ds_u8(CROSSED_BOX));
        row = u16(row + 3);
    }
    folder_present();
    u16 y = ds_u8(u16(OUTFIT_TOP + ds_u8(DS_outfit_item)));
    if (ds_u8(DS_outfit_item) == 1) y = u16(y + 8);
    if (ds_u8(DS_outfit_item) == 3) y = u16(y + 8);
    gfx_move_to(0x6A, s16(y));
    menu_cursor_draw(0x43, 0x43);
    return y;
}

// 02d2:273e debrief (game_flow.md §6.5): the mission's result, the officer's mood by it, and a
// promotion (by the mission type) and a medal (one per mission type) when the objective was
// reached (progress 3..5). Shooting the own men (result 0) is a court-martial: rank 1, no medals.
void debrief()
{
    office_restore();
    set_draw_page(0);
    u16 off = print_keys_and_text(T_ENTER_RESULT);  // "Your mission was"
    if (ds_u8(DS_objective_progress) > 5) ds_u8(DS_objective_progress) = 5;
    u16 &hours = ds_u16(u16(DS_score_words + 4));
    if (hours == 0) hours = bcd_add(hours, 1);
    if (ds_u8(DS_friendly_hits) > 5) {
        ds_u8(DS_mission_result) = 0;
        ds_u8(DS_objective_progress) = 0;
    }
    off = 0;
    if (ds_u8(DS_objective_progress) != 0) ds_u16(DS_office_mood) = u16(ds_u8(DS_objective_progress) - 1);
    else ds_u16(DS_office_mood) = 0;
    office_face_draw();
    switch (ds_u8(DS_mission_result)) {
    case 0:
        print_records(T_SHOT_MY_MEN, off);
        ds_u8(u16(DS_medals + 1)) = 0;
        ds_u8(DS_medals) = 0;
        ds_u8(DS_rank) = 1;
        break;
    case 1: print_records(T_KILLED, off); break;
    default: {
        print_records(ds_u16(u16(RESULT_TEXTS + 2 * ds_u8(DS_objective_progress))), off);
        if (ds_u8(DS_objective_progress) <= 2) break;
        ds_u16(DS_missions_completed) = bcd_add(ds_u16(DS_missions_completed), 1);
        const u16 type = ds_u16(DS_mission_type);
        if (ds_u8(u16(PROMOTION_RANK + type)) > ds_u8(DS_rank)) {
            ds_u8(DS_rank) = ds_u8(u16(PROMOTION_RANK + type));
            wait_key_idle(0);
            speech_clear();
            gfx_copy_rect(0xA0, 0xBF, 0x10, 0x27, 0xE8, 0xB3, 1, 0);
            const u16 x = u16((ds_u8(DS_rank) << 4) - 0x10);
            gfx_copy_rect(x, u16(x + 0x0F), 0, 0x0F, 0xF0, 0xAF, 1, 0);
            print_keys_and_text(T_PROMOTED);
            print_records(ds_u16(u16(RANK_WORDS + 2 * ds_u8(DS_rank))), 0);
        }
        u16 medals = u16((ds_u8(u16(DS_medals + 1)) << 8) + ds_u8(DS_medals));
        const u16 bit = ds_u16(u16(MISSION_MEDAL_BIT + 2 * ds_u16(DS_mission_type)));
        if (bit == 0 || (medals & bit) != 0) break;
        medals |= bit;
        ds_u8(DS_medals) = u8(medals);
        ds_u8(u16(DS_medals + 1)) = u8(s16(medals) / 256);
        wait_key_idle(0);
        speech_clear();
        gfx_copy_rect(0xA0, 0xBF, 0x10, 0x27, 0xE8, 0xB3, 1, 0);
        const u16 x = u16((ds_u8(u16(MISSION_MEDAL + (ds_u16(DS_region) << 3) + ds_u16(DS_mission_number))) << 4) - 0x10);
        gfx_copy_rect(x, u16(x + 0x0F), 0x10, 0x1F, 0xF0, 0xAF, 1, 0);
        print_keys_and_text(T_MEDAL);
        break;
    }
    }
    wait_key_idle(0);
}

// 02d2:2a6c roster_update: the mission's scores shown, added to the career statistics (BCD), and
// the working copy (rank, medals, missions, statistics) written back into the commander's record.
void roster_update()
{
    speech_clear();
    set_draw_page(0);
    print_keys_and_text(T_SCORES);
    bcd_stats_print(MISSION_STATS, MISSION_STAT_CELLS);
    gfx_copy_rect_to_copy_page(0x28, 0x11F, 0x90, 0xC7);
    for (s16 i = 0; i < 0x0C; i++) {
        u16 &stat = ds_u16(u16(DS_career_stats + 2 * i));
        stat = bcd_add(stat, ds_u16(u16(DS_score_words + 2 * i)));
    }
    for (s16 i = 0x14; i < 0x32; i++)
        ds_u8(u16(record(ds_u16(DS_commander_slot)) + i)) = ds_u8(u16(DS_commander + i));
    wait_key_idle(0);
}

} // namespace gb
