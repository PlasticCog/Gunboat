// Bridge entries: the headquarters and the front end (game_flow.md §4-§6), segments 020d and 02d2.
#include "bridge.hpp"
#include "game/flow.hpp"
#include "platform/platform.hpp"

using namespace gb;

// 020d
BRIDGE(hq_quiz) { r.ax = hq_quiz(); }
BRIDGE(choice_menu) { r.ax = choice_menu(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]); }
BRIDGE(menu_cursor_move) { menu_cursor_move(a[0], a[1], a[2], a[3]); }
BRIDGE(menu_cursor_draw) { menu_cursor_draw(a[0], a[1]); }
BRIDGE(menu_tick_mark) { menu_tick_mark(a[0], a[1], a[2], a[3]); }
BRIDGE(roster_load) { roster_load(); }
BRIDGE(roster_save) { roster_save(); }

// 02d2: the office
BRIDGE(office_face_draw) { office_face_draw(); }
BRIDGE(office_idle) { office_idle(); }
BRIDGE(speech_clear) { speech_clear(); }
BRIDGE(folder_draw) { folder_draw(a[0], a[1]); }
BRIDGE(folder_present) { folder_present(); }
BRIDGE(wait_key_idle) { r.ax = wait_key_idle(a[0]); }
BRIDGE(office_draw) { office_draw(); }
BRIDGE(office_restore) { office_restore(); }
BRIDGE(bcd_inc) { r.ax = bcd_inc(a[0]); }
BRIDGE(bcd_add) { r.ax = bcd_add(a[0], a[1]); }
BRIDGE(bcd_to_bin) { r.ax = bcd_to_bin(a[0]); }
BRIDGE(bin_to_bcd) { r.ax = bin_to_bcd(a[0]); }

// 02d2: the front end
BRIDGE(front_end) { front_end(); }
BRIDGE(name_entry) { r.ax = name_entry(); }
BRIDGE(roster_edit) { roster_edit(); }
BRIDGE(roster_new_record) { roster_new_record(a[0]); }
BRIDGE(personnel_files) { personnel_files(a[0]); }
BRIDGE(personnel_file_show) { personnel_file_show(a[0]); }
BRIDGE(bcd_stats_print) { bcd_stats_print(a[0], a[1]); }
BRIDGE(byte_stats_print) { byte_stats_print(a[0], a[1]); }
BRIDGE(pbr_specs) { pbr_specs(a[0]); }
BRIDGE(spec_sheet_draw) { spec_sheet_draw(a[0]); }
BRIDGE(mission_select) { mission_select(); }
BRIDGE(mission_folder_draw) { mission_folder_draw(); }
BRIDGE(assignment_map) { assignment_map(a[0]); }
BRIDGE(map_draw) { map_draw(a[0]); }
BRIDGE(outfitting) { outfitting(); }
BRIDGE(outfitting_draw) { r.ax = outfitting_draw(); }
BRIDGE(debrief) { debrief(); }
BRIDGE(roster_update) { roster_update(); }

// helpers they use
BRIDGE(print_chars) { print_chars(a[0], a[1]); }
BRIDGE(ega_pal_entry) { ega_pal_entry(a[0], a[1]); }
BRIDGE(world_a_base) { r.ax = world_a_base(); }
BRIDGE(pit_random) { r.ax = pit_random(a[0]); }
BRIDGE(strcpy) { r.ax = crt_strcpy(a[0], a[1]); }
BRIDGE(strncmp) { r.ax = u16(crt_strncmp(a[0], a[1], a[2])); }
