// Key dispatch and the key handlers (simulation.md §3.2, §3.3): the key code of the mission loop goes
// to a handler of the code-segment table key_handlers (01h..7Fh) or of the DGROUP table
// fkey_handlers (F1..F10 at the 3D stations); everything else polls the held controls.
#include "game/sim.hpp"

#include "game/flow.hpp"
#include "game/pending.hpp"
#include "host.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// The handlers the two tables hold, by their offset in segment 0919 (the tables are data: a
// dispatch goes through the address the table holds, as CALL AX does). Returns SI (F1 and F9 change
// it; mission_run passes it on to game_frame).
u16 call_handler(u16 ax, u16 si)
{
    switch (ax) {
    case 0x03C7: key_default(); break;
    case 0x043C: key_f10_fire_at_will(); break;
    case 0x0465: si = key_f9_identify(si); break;
    case 0x0503: key_minus_control_rate(); break;
    case 0x0520: si = key_f1_panel(si); break;
    case 0x056B: key_f3_panel(); break;
    case 0x05AE: key_f2_panel(); break;
    case 0x0649: key_f5_branch_left(); break;
    case 0x0658: key_f6_branch_right(); break;
    case 0x0667: key_f4_reverse_course(); break;  // pending.cpp: needs route_point / route_advance
    case 0x06A9: key_f8_faster(); break;
    case 0x06EE: key_f7_slower(); break;
    case 0x074A: key_m_map(); break;
    case 0x0756: key_period_assignment(); break;
    case 0x0762: key_z_pilot_left(); break;
    case 0x0769: key_x_pilot_ahead(); break;
    case 0x0770: key_c_pilot_right(); break;
    case 0x07C0: key_v_bow(); break;
    case 0x07F6: key_n_midship(); break;
    case 0x0833: key_b_stern(); break;
    case 0x0871: key_slash_damage_report(); break;
    case 0x087D: key_plus_time_compression(); break;
    case 0x08BE: key_tab_return_to_base(); break;
    case 0x090D: key_comma_chase_view(); break;
    case 0x0930: key_d_detail(); break;
    // PORT: the tables hold only these handlers; any other address would be a jump into code that is
    // not a key handler.
    default: host_fatal("key_dispatch: 0919:%04X is not a key handler", ax);
    }
    return si;
}

// Station and the panel's switch bytes.
u8 station_low() { return u8(ds_u16(DS_station)); }
u8 &switch_byte(u16 n) { return ds_u8(u16(DS_panel_switches + n)); }

// The switch picture value and lamp value for a switch byte: 3 when the switch is on (bit 0 clear),
// 0 when off.
u8 on_value(u16 n) { return (switch_byte(n) & 1) ? 0 : 3; }

// AL replaced, AH kept (MOV AL, ...).
u16 set_al(u16 ax, u8 al) { return u16((ax & 0xFF00) | al); }

} // namespace

// 0919:038e key_dispatch (simulation.md §3.2): key_code 01h..7Fh through key_handlers (the entry
// for code c at CS:028E + 2c); 81h..8Ah through fkey_handlers while the station is a 3D one (word
// below 5); 0, 80h and everything else: controls_poll. Returns SI as the handler leaves it.
u16 key_dispatch(u16 si)
{
    const u8 al = ds_u8(DS_key_code);
    u16 bx = u16(al << 1);
    if (bx == 0 || al == 0x80) {
        controls_poll();
        return si;
    }
    if (al < 0x80) return call_handler(seg_u16(CSSEG_key_handlers, u16(CS_key_handlers - 2 + bx)), si);
    if (al > 0x8A || ds_u16(DS_station) >= 5) {
        controls_poll();
        return si;
    }
    bx = u16(u8(al - 0x81) << 1);
    return call_handler(ds_u16(u16(DS_fkey_handlers + bx)), si);
}

// 0919:03c7 key_default (simulation.md §3.2): the handler of the keys without an action.
void key_default() { controls_poll(); }

// 0919:03cb panel_switch_toggle (simulation.md §3.3, hud.md §2): entry BX of panel_switch_table
// (switch number, lamp number). The switch is flipped (switch_draw, mode 00h, value = its byte xor
// 1); then its lamp shows 3 while the switch is on, 0 when off (none for lamp FFh). A switch number
// above 80h (none in the table) drives two lamps, n and n + 1, with opposite values. Returns AX as
// the original leaves it (the key handlers go on with it).
u16 panel_switch_toggle(u16 bx)
{
    bx = u16(bx + CS_panel_switch_table);
    const u8 entry = seg_u8(CSSEG_panel_switch_table, bx);
    u16 si = u8(entry & 0x7F);
    const u8 cl = u8(switch_byte(si) ^ 1);
    switch_draw(u16(cl << 8 | u8(si)));
    bx++;
    if (entry <= 0x80) {
        si = entry;
        const u8 lamp = seg_u8(CSSEG_panel_switch_table, bx);
        if (lamp == 0xFF) return 0x00FF;  // SUB AH,AH before: AX = 00FFh
        return indicator_draw(u16(on_value(si) << 8 | lamp));
    }
    si = u8(entry & 0x7F);
    const u8 lamp = seg_u8(CSSEG_panel_switch_table, bx);
    u8 c = on_value(si);
    indicator_draw(u16(c << 8 | lamp));
    c ^= 3;
    return indicator_draw(u16(c << 8 | u8(lamp + 1)));
}

// 0919:043c key_f10_fire_at_will (simulation.md §3.3): at a 3D station (station low byte below 5)
// the crew's fire at will toggles: "Cease fire." (9) or "Open fire!" (6).
void key_f10_fire_at_will()
{
    if (station_low() >= 5) return;
    if (ds_u16(DS_fire_at_will) != 0) {
        show_message(9);
        ds_u16(DS_fire_at_will) = 0;
    } else {
        show_message(6);
        ds_u16(DS_fire_at_will) = 1;
    }
}

// 0919:0465 key_f9_identify (simulation.md §6.3): the visible list from its last entry down to
// identify_first (signed compare). The entry's bearing (offset by +28h / -28h for the pilot looking
// to a side) - 44h, shifted left 3 as an 11-bit signed value with the fine bearing in its low bits,
// halved (SAR): in view below 40h (unsigned). Kind 3Ch: a candidate; above 39h: skipped; 39h: stop
// with the candidate so far; kind 0, the boat and the temporary objects (offset below 48h) and
// distance class 0: skipped; below 28h: chosen at once; 28h..38h: a candidate if there is none yet.
// The result goes to identified_object and message 4 prints it. Returns SI (the object offset of the
// last entry in view that was examined).
u16 key_f9_identify(u16 si)
{
    ds_u16(DS_identify_candidate) = 0;
    u16 bx = ds_u16(DS_visible_count);
    u16 ax;
    for (;;) {
        bx--;
        if (!(s16(bx) >= s16(ds_u16(DS_identify_first)))) {
            ax = ds_u16(DS_identify_candidate);
            break;
        }
        u8 al = ds_u8(u16(DS_visible_bearing + bx));
        if (ds_u16(DS_station) == 1 && ds_u16(DS_look_direction) != 1)
            al = u8(al + (ds_u16(DS_look_direction) > 1 ? 0x28 : 0xD8));
        al = u8(al - 0x44);
        u16 v = u16(al << 3);
        if (v & 0x0400) v |= 0xF800;  // OR AH,F8h: the sign of the 11-bit value
        v = u16((v & 0xFF00) | u8(u8(v) | ds_u8(u16(DS_visible_fine_bearing + bx))));
        v = u16(s16(v) >> 1);
        if (v >= 0x40) continue;
        si = u16(bx << 1);
        const u8 distance = ds_u8(u16(DS_visible_distance + si));
        si = ds_u16(u16(DS_visible_object + si));
        const u8 kind = ds_u8(u16(DS_object_word + si));
        if (kind != 0x3C) {
            if (kind > 0x39) continue;
            if (kind == 0x39) {
                ax = ds_u16(DS_identify_candidate);
                break;
            }
            if (kind == 0 || si < 0x48 || distance == 0) continue;
            if (kind < 0x28) {
                ax = ds_u16(u16(DS_visible_object + u16(bx << 1)));
                break;
            }
        }
        if (ds_u16(DS_identify_candidate) == 0) {
            bx = u16(bx << 1);
            ds_u16(DS_identify_candidate) = ds_u16(u16(DS_visible_object + bx));
            bx = u16(bx >> 1);  // SHL/SHR: bit 15 of BX is lost
        }
    }
    ds_u16(DS_identified_object) = ax;
    show_message(4);
    return si;
}

// 0919:0503 key_minus_control_rate (simulation.md §3.3): at a 3D station, panel switch 0Ch (the
// control rate) set to the next value: (rate & 3) 0 -> 1, 1 -> 2, 2 and 3 -> 0.
void key_minus_control_rate()
{
    if (ds_u16(DS_station) >= 5) return;
    u8 al = ds_u8(DS_control_rate) & 3;
    if (al >= 2) al = 0xFF;
    switch_draw(u16(u8(al + 1) << 8 | 0x0C));
}

// 0919:0520 key_f1_panel (simulation.md §3.3): bow (2): panel entry 12h; midship (3): 10h; stern
// (station low byte above 3): 1Ch; pilot (0, 1): entry 0Ch, the main switch, and when that leaves it
// off, engine_switch for each engine whose switch is on. Then the whole panel is redrawn, with the AX
// the last call left (its AH goes to scratch_b7e3). Returns SI (the engine last switched).
u16 key_f1_panel(u16 si)
{
    const u8 st = station_low();
    u16 ax;
    if (st == 2) {
        ax = panel_switch_toggle(0x12);
    } else if (st > 2) {
        ax = panel_switch_toggle(st == 3 ? 0x10 : 0x1C);
    } else {
        ax = panel_switch_toggle(0x0C);
        ax = set_al(ax, u8(switch_byte(0) >> 1));
        if (switch_byte(0) & 1) {
            ax = set_al(ax, u8(ds_u8(DS_engine_switches) >> 1));
            if (!(ds_u8(DS_engine_switches) & 1)) ax = engine_switch(si = 0);
            ax = set_al(ax, u8(ds_u8(DS_engine_switches + 1) >> 1));
            if (!(ds_u8(DS_engine_switches + 1) & 1)) ax = engine_switch(si = 1);
        }
    }
    panel_redraw_all(ax);
    return si;
}

// 0919:056b key_f3_panel (simulation.md §3.3): bow panel entry 2, midship 6, stern 18h; then, when
// the AL left by that (or the station's low byte at other stations) is 1, entry 4 (the pilot's F3,
// and a quirk: a toggle that returns AL = 1 at a gun station also toggles entry 4).
void key_f3_panel()
{
    u16 ax = ds_u16(DS_station);
    const u8 st = u8(ax);
    if (st == 2) ax = panel_switch_toggle(2);
    else if (st == 3) ax = panel_switch_toggle(6);
    else if (st == 4) ax = panel_switch_toggle(0x18);
    if (u8(ax) == 1) panel_switch_toggle(4);
}

// 0919:05ae key_f2_panel (simulation.md §3.3): midship panel entry 0Eh; stern 1Ah; bow 16h then 14h;
// pilot (0, 1), with the main switch on: engine_switch for each engine that has fuel and is not
// destroyed (condition 2).
void key_f2_panel()
{
    const u8 st = station_low();
    if (st == 3) {
        panel_switch_toggle(0x0E);
        return;
    }
    if (st == 4) {
        panel_switch_toggle(0x1A);
        return;
    }
    if (st < 2) {
        if (switch_byte(0) & 1) return;
        if (ds_u16(DS_fuel) != 0 && (ds_u8(DS_engine_condition) & 3) != 2) engine_switch(0);
        if (ds_u16(DS_fuel + 2) != 0 && (ds_u8(DS_engine_condition + 1) & 3) != 2) engine_switch(1);
        return;
    }
    if (st != 2) return;
    panel_switch_toggle(0x16);
    panel_switch_toggle(0x14);
}

// 0919:0608 engine_switch (simulation.md §4.2): engine SI's switch (panel entry 8 + 2 SI) flips.
// Only when its lamp's low 3 bits are 0 or 3 (a lamp of 3 is first set to 2) does the engine react:
// in a countdown (state & FCh nonzero) the countdown reverses (state & FCh xor FFh); otherwise the
// countdown starts, 7Fh when the switch is now on (it counts down to 1, running), 80h when off (it
// counts up to 0, stopped). Returns AX as the original leaves it.
u16 engine_switch(u16 si)
{
    u16 ax = panel_switch_toggle(u16(u16(si << 1) + 8));
    u8 al = ds_u8(u16(DS_engine_indicator + si)) & 7;
    ax = set_al(ax, al);
    if (al != 0) {
        if (al != 3) return ax;
        ax = indicator_draw(u16(0x0200 | u8(si + 1)));
    }
    u8 &state = ds_u8(u16(DS_engine_state + si));
    al = state & 0xFC;
    if (al != 0) {
        al ^= 0xFF;
        state = al;
        return set_al(ax, al);
    }
    al = (ds_u8(u16(DS_engine_switches + si)) & 1) ? 0x80 : 0x7F;
    state = al;
    return set_al(ax, al);
}

// 0919:0649 key_f5_branch_left (simulation.md §3.3): "Pilot, branch left." (20h), branch_command 1.
void key_f5_branch_left()
{
    ds_u8(DS_branch_command) = 1;
    show_message(0x20);
    pilot_command_reply();
}

// 0919:0658 key_f6_branch_right (simulation.md §3.3): "Pilot, branch right." (23h), branch_command 2.
void key_f6_branch_right()
{
    ds_u8(DS_branch_command) = 2;
    show_message(0x23);
    pilot_command_reply();
}

// 0919:06a9 key_f8_faster (simulation.md §3.3): "Pilot, faster." (1Ch): the crew throttle target
// + 15, at most the port throttle maximum (then the answer "-maximum speed, sir." 32h replaces
// "Aye-aye" (2), and "-we're not moving" 24h becomes "Aye-aye" first) and at most the starboard
// maximum (quirk: that limit does not give the maximum-speed answer).
void key_f8_faster()
{
    show_message(0x1C);
    u8 ah = 0;
    u8 al = u8(ds_u8(DS_crew_throttle_target) + 0x0F);
    if (al > ds_u8(DS_throttle_max)) {
        al = ds_u8(DS_throttle_max);
        ah++;
    }
    if (al > ds_u8(DS_throttle_max + 1)) al = ds_u8(DS_throttle_max + 1);
    ds_u8(DS_crew_throttle_target) = al;
    pilot_command_reply();
    if (ds_u8(DS_message_reply) == 0x24) ds_u8(DS_message_reply) = 2;
    if (ds_u8(DS_message_reply) == 2 && ah != 0) ds_u8(DS_message_reply) = 0x32;
}

// 0919:06ee key_f7_slower (simulation.md §3.3): the crew throttle target - 15, at least 8 (also for a
// negative byte); "Pilot, slower." (30h); at idle the answer is "-we're not moving, sir." (24h).
void key_f7_slower()
{
    u8 al = u8(ds_u8(DS_crew_throttle_target) - 0x0F);
    if (s8(al) < 0 || al < 8) al = 8;
    ds_u8(DS_crew_throttle_target) = al;
    show_message(0x30);
    pilot_command_reply();
    if (ds_u8(DS_crew_throttle_target) == 8) ds_u8(DS_message_reply) = 0x24;
}

// 0919:0713 pilot_command_reply (simulation.md §3.3): the crew's answer, queued unless the mission is
// ending: 21h at the pilot station (station low byte 1, not in chase view), 12h with the captain
// dead, 24h with the boat stopped, else 2.
void pilot_command_reply()
{
    u8 ah = 0x21;
    if (!(ds_u8(DS_chase_view) == 0 && station_low() == 1)) {
        ah = 0x12;
        if ((ds_u8(DS_captain_condition) & 3) != 2) {
            ah = 0x24;
            if (ds_u8(DS_speed) != 0) ah = 2;
        }
    }
    if (ds_u8(DS_message_state) == 2) return;
    ds_u8(DS_message_state) = 1;
    ds_u8(DS_message_reply) = ah;
}

// 0919:074a key_m_map (simulation.md §3.3): station 5, the tactical map; chase view off.
void key_m_map()
{
    ds_u16(DS_station) = 5;
    ds_u8(DS_chase_view) = 0;
}

// 0919:0756 key_period_assignment (simulation.md §3.3): station 8, the assignment.
void key_period_assignment()
{
    ds_u16(DS_station) = 8;
    ds_u8(DS_chase_view) = 0;
}

// 0919:0762 key_z_pilot_left (simulation.md §3.3)
void key_z_pilot_left() { key_pilot_station(0); }

// 0919:0769 key_x_pilot_ahead (simulation.md §3.3)
void key_x_pilot_ahead() { key_pilot_station(1); }

// 0919:0770 key_c_pilot_right (simulation.md §3.3)
void key_c_pilot_right() { key_pilot_station(2); }

// 0919:0777 key_pilot_station (simulation.md §3.3): the pilot's station looking BX. Refused in
// shooting practice (practice_mode low byte 1 or 2, not in the demo): "Practice shooting, OK?" (19h)
// at a 3D station; with the captain dead: station_dead_reply.
void key_pilot_station(u16 bx)
{
    const u8 practice = u8(ds_u16(DS_practice_mode));
    if ((practice == 1 || practice == 2) && ds_u16(DS_demo_mode) == 0) {
        if (ds_u16(DS_station) < 5) show_message(0x19);
        return;
    }
    if ((ds_u8(DS_captain_condition) & 3) == 2) {
        station_dead_reply();
        return;
    }
    ds_u16(DS_look_direction) = bx;
    ds_u16(DS_station) = 1;
    ds_u8(DS_chase_view) = 0;
}

// 0919:07b3 station_dead_reply (simulation.md §3.3): "-he's dead, sir." (12h) at a 3D station
// (station low byte below 5).
void station_dead_reply()
{
    if (station_low() >= 5) return;
    show_message(0x12);
}

// 0919:07c0 key_v_bow (simulation.md §3.3): the bow gun. Refused when practice_mode >= 2 (at a 3D
// station: 19h for practice 2, else 1Bh "Practice piloting, OK?"), or with the gunner's mate dead.
void key_v_bow()
{
    if (ds_u16(DS_practice_mode) >= 2) {
        if (ds_u16(DS_station) >= 5) return;
        show_message(ds_u16(DS_practice_mode) == 2 ? 0x19 : 0x1B);
        return;
    }
    if ((ds_u8(DS_gunners_mate_condition) & 3) == 2) {
        station_dead_reply();
        return;
    }
    ds_u16(DS_station) = 2;
    ds_u8(DS_chase_view) = 0;
}

// 0919:07f6 key_n_midship (simulation.md §3.3): the midship gun. Refused in any practice (not in the
// demo): 1Bh for practice 3, else 19h; or with the seaman dead.
void key_n_midship()
{
    if (ds_u16(DS_demo_mode) == 0 && ds_u16(DS_practice_mode) != 0) {
        if (ds_u16(DS_station) >= 5) return;
        show_message(ds_u16(DS_practice_mode) == 3 ? 0x1B : 0x19);
        return;
    }
    if ((ds_u8(DS_seaman_condition) & 3) == 2) {
        station_dead_reply();
        return;
    }
    ds_u16(DS_station) = 3;
    ds_u8(DS_chase_view) = 0;
}

// 0919:0833 key_b_stern (simulation.md §3.3): the stern gun. Refused when practice_mode is odd (not in
// the demo): 1Bh for practice 3, else 19h; or with the engineman dead.
void key_b_stern()
{
    if (ds_u16(DS_demo_mode) == 0 && (ds_u16(DS_practice_mode) & 1)) {
        if (ds_u16(DS_station) >= 5) return;
        show_message(ds_u16(DS_practice_mode) == 3 ? 0x1B : 0x19);
        return;
    }
    if ((ds_u8(DS_engineman_condition) & 3) == 2) {
        station_dead_reply();
        return;
    }
    ds_u16(DS_station) = 4;
    ds_u8(DS_chase_view) = 0;
}

// 0919:0871 key_slash_damage_report (simulation.md §3.3): station 7.
void key_slash_damage_report()
{
    ds_u16(DS_station) = 7;
    ds_u8(DS_chase_view) = 0;
}

// 0919:087d key_plus_time_compression (simulation.md §3.3): at a 3D station, time compression 0 -> 1
// -> 2 -> 0 (messages 2Ch..2Eh). Going up sets bit 20h of the byte at world_a_data + [world_a_data];
// going back to 0 leaves it (enemy_update toggles it). Then panel switch 0Bh is set from the time
// compression switch byte ((sw & 3) 0 -> 1, 1 -> 2, else 0).
void key_plus_time_compression()
{
    if (ds_u16(DS_station) >= 5) return;
    const u16 bx = u16(ds_u16(DS_world_a_data) + DS_world_a_data);
    u8 ah = ds_u8(bx) | 0x20;
    u8 al = u8(ds_u8(DS_time_compression) + 1);
    if (al > 2) {
        ah = ds_u8(bx);
        al = 0;
    }
    ds_u8(DS_time_compression) = al;
    ds_u8(bx) = ah;
    show_message(u8(al + 0x2C));
    al = ds_u8(DS_time_compression_switch) & 3;
    if (al >= 2) al = 0xFF;
    switch_draw(u16(u8(al + 1) << 8 | 0x0B));
}

// 0919:08be key_tab_return_to_base (simulation.md §3.3): at a 3D station, "Return to base." (7), shown
// for 6 world passes before the mission ends. With water_level >= 10h or both engines unusable
// (engine condition & fuel tank condition & 3 == 2 on each side) the mission is aborted crippled:
// mission_result 2 and score word 1 + 1 (BCD).
void key_tab_return_to_base()
{
    if (ds_u16(DS_station) >= 5) return;
    show_message(7);
    ds_u8(DS_message_timer) = 6;
    const bool crippled =
        ds_u8(DS_water_level) >= 0x10 ||
        ((ds_u8(DS_engine_condition) & ds_u8(DS_fuel_tank_condition) & 3) == 2 &&
         (ds_u8(DS_engine_condition + 1) & ds_u8(DS_fuel_tank_condition + 1) & 3) == 2);
    if (!crippled) return;
    ds_u8(DS_mission_result) = 2;
    ds_u16(DS_score_words + 2) = bcd_inc(ds_u16(DS_score_words + 2));
}

// 0919:090d key_comma_chase_view (simulation.md §3.3): the chase boat view (not when already on):
// station 2, its heading from the view heading, message 25h, then the view on and the scene rebuilt.
void key_comma_chase_view()
{
    if (ds_u8(DS_chase_view) != 0) return;
    ds_u16(DS_station) = 2;
    ds_u8(DS_chase_heading) = ds_u8(DS_view_heading);
    show_message(0x25);
    ds_u8(DS_chase_view) = 1;
    ds_u8(DS_scene_rebuild) = 1;
}

// 0919:0930 key_d_detail (simulation.md §3.3): at a 3D station, the detail level toggles; DS:D6C0
// (the object distance limit, render3d §5) FFh for high, 16h for low; the scene is rebuilt; message
// 36h / 37h.
void key_d_detail()
{
    if (ds_u16(DS_station) >= 5) return;
    u8 bh = 0xFF;
    ds_u8(DS_detail_low) ^= 1;
    if (ds_u8(DS_detail_low) != 0) bh = 0x16;
    ds_u8(0xD6C0) = bh;  // DS:D6C0, render3d's object distance limit
    ds_u8(DS_scene_rebuild) = 1;
    show_message(u8(ds_u8(DS_detail_low) + 0x36));
}

} // namespace gb
