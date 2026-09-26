// The cockpit panel (hud.md §2): the lamps (indicators) and switches of the boat record, their
// blinking, and the waterjet indicator of the pilot's station (hud.md §3).
//
// A lamp or switch byte: bits 0-1 the state (the picture), bit 2 locked, bits 3-6 visible from the
// stern, pilot, bow and midship stations, bit 7 blinking. Its pictures are copied from the panel
// art on page 1 to the current draw page.
#include "hud/hud.hpp"

#include "mem.hpp"
#include "platform/gfx.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// The mode byte (bits 6-7 of AL): 00h set the state, 40h redraw, 80h blink step, C0h clear.
constexpr u8 MODE_SET = 0x00, MODE_REDRAW = 0x40, MODE_BLINK = 0x80;

// The visibility test both draw routines share (0919:0089 / 01ca): the bit of the byte for the
// current station. Returns the AL the original leaves when it gives up (0, or the byte itself for
// a station above 4), or -1 if the byte is to be drawn.
int visible_or_al(u8 value)
{
    const u16 station = ds_u16(DS_station);
    u8 bit;
    if (station < 2) bit = 0x10;         // pilot (and 0)
    else if (station == 2) bit = 0x20;   // bow
    else if (station > 4) return value;  // the full-screen stations: nothing
    else if (station == 4) bit = 0x08;   // stern
    else bit = 0x40;                     // midship
    return (value & bit) ? -1 : 0;
}

} // namespace

// 0919:0000 panel_redraw_all (hud.md §2.1): indicator_draw in mode 40h (redraw) for lamps 0..1Dh.
// AH is the caller's for every call (indicator_draw stores it in scratch_b7e3). Returns AX with
// AL = 5Eh.
u16 panel_redraw_all(u16 ax)
{
    u8 al = 0x40;
    do {
        indicator_draw(u16((ax & 0xFF00) | al));  // PUSH AX / CALL / POP AX
        al = u8(al + 1);
    } while (al < 0x5E);
    return u16((ax & 0xFF00) | al);
}

// 0919:000e panel_blink (hud.md §2.1): every frame the blink timer counts down; at zero it is set to
// 1 (so from then on every frame) and every lamp gets mode 80h: a blinking lamp toggles its state.
u16 panel_blink(u16 ax)
{
    if (--ds_u8(DS_blink_timer) != 0) return ax;
    ds_u8(DS_blink_timer) = 1;
    u8 al = 0x80;
    do {
        indicator_draw(u16((ax & 0xFF00) | al));
        al = u8(al + 1);
    } while (al < 0x9E);
    return u16((ax & 0xFF00) | al);
}

// 0919:0027 indicator_draw (hud.md §2.1): AL = mode | lamp (0..1Dh), AH = the value for mode 00h.
// Updates the lamp byte by the mode, then, unless in chase view or not visible from this station,
// copies the lamp's state picture from page 1 to (column * 8, row * 8) on the draw page. The picture
// is the dark one (state 0) while the main switch is off, and for the gun-mount lamps 14h-1Dh
// while the mount's switch is off. Returns AX as the original leaves it.
u16 indicator_draw(u16 ax)
{
    u8 al = u8(ax);
    const u8 ah = u8(ax >> 8);
    if (al == 0xFF) return ax;
    ds_u8(DS_scratch_b7e2) = al;
    ds_u8(DS_scratch_b7e3) = ah;
    al &= 0x3F;
    if (al >= 0x1E) return u16(ah << 8 | al);
    const u16 id = al;
    u8 &lamp = ds_u8(u16(DS_boat_record + id));
    const u8 mode = ds_u8(DS_scratch_b7e2) & 0xC0;
    if (mode == MODE_SET) {
        if (lamp & 4) {
            al = 4;  // locked
        } else {
            ds_u8(DS_scratch_b7e3) &= 0x87;
            al = u8((lamp & 0x78) | ds_u8(DS_scratch_b7e3));
            lamp = al;
        }
    } else if (mode == MODE_REDRAW) {
        al = mode;
    } else if (mode == MODE_BLINK) {
        al = lamp;
        if (al < 0x80) return u16(ah << 8 | al);  // not blinking: nothing to draw
        al ^= 3;
        lamp = al;
    } else {  // C0h: clear the state and the blink bit
        al = lamp & 0x7C;
        lamp = al;
    }
    if (ds_u8(DS_chase_view) != 0) return u16(ah << 8 | al);
    const int v = visible_or_al(lamp);
    if (v >= 0) return u16(ah << 8 | v);

    // 00b7: the picture
    u16 si = u8(ds_u8(u16(DS_indicator_graphics + id)) << 1);  // SHL AL,1 (8 bits)
    const u16 row = u16(ds_u8(u16(DS_indicator_positions + 2 * id)) << 3);
    const u8 h = ds_u8(u16(DS_indicator_frame_sizes + 1 + si));
    const u16 dy_bottom = u16(row + h - 1);
    const u16 dx = u16(ds_u8(u16(DS_indicator_positions + 1 + 2 * id)) << 3);
    if (dx == 0) return 0;  // no lamp at column 0
    const u8 w = ds_u8(u16(DS_indicator_frame_sizes + si));
    si = u16(si << 2);
    si = u16(si + ((lamp & 3) << 1));
    bool dark = (ds_u8(DS_panel_switches) & 1) != 0;  // the main switch is off
    if (!dark && id >= 0x14) {
        if (id <= 0x17) dark = (ds_u8(u16(DS_panel_switches + 5)) & 1) != 0;       // bow mount
        else if (id <= 0x1A) dark = (ds_u8(u16(DS_panel_switches + 9)) & 1) != 0;  // midship mount
        else if (id <= 0x1D) dark = (ds_u8(u16(DS_panel_switches + 0x0E)) & 1) != 0;  // stern mount
    }
    if (dark) si &= 0xFFF8;
    const u16 y0 = ds_u8(u16(DS_indicator_frames + 1 + si));
    const u16 x0 = ds_u8(u16(DS_indicator_frames + si));
    gfx_copy_rect(x0, u16(w + x0 - 1), y0, u16(h + y0 - 1), dx, dy_bottom, 1, ds_u16(DS_draw_page));
    return 0;  // gfx_copy_rect's AX
}

// 0919:016a panel_switches_redraw (hud.md §2.1): switch_draw in mode 40h for switches 0..10h. AH is
// the caller's for every call. Returns AX with AL = 51h.
u16 panel_switches_redraw(u16 ax)
{
    u8 al = 0x40;
    do {
        switch_draw(u16((ax & 0xFF00) | al));
        al = u8(al + 1);
    } while (al < 0x51);
    return u16((ax & 0xFF00) | al);
}

// 0919:0178 switch_draw (hud.md §2.1): indicator_draw for the panel switches (AL = mode | switch
// 0..10h, AH = value), with their own position, graphic and picture tables and no dark pictures.
// Modes 80h and C0h both clear the state and the blink bit (switches do not blink). Returns AX.
u16 switch_draw(u16 ax)
{
    u8 al = u8(ax);
    const u8 ah = u8(ax >> 8);
    if (al == 0xFF) return ax;
    ds_u8(DS_scratch_b7e2) = al;
    ds_u8(DS_scratch_b7e3) = ah;
    al &= 0x3F;
    if (al >= 0x11) return u16(ah << 8 | al);
    const u16 id = al;  // SUB AH,AH: from here AH = 0
    u8 &sw = ds_u8(u16(DS_panel_switches + id));
    const u8 mode = ds_u8(DS_scratch_b7e2) & 0xC0;
    if (mode == MODE_SET) {
        if (!(sw & 4)) {
            ds_u8(DS_scratch_b7e3) &= 0x87;
            sw = u8((sw & 0x78) | ds_u8(DS_scratch_b7e3));
        }
    } else if (mode != MODE_REDRAW) {
        sw = sw & 0x7C;
    }
    al = sw;
    if (ds_u8(DS_chase_view) != 0) return al;
    const int v = visible_or_al(al);
    if (v >= 0) return u16(v);

    // 01f4: the picture
    u16 si = u8(ds_u8(u16(DS_switch_graphics + id)) << 1);
    const u16 row = u16(ds_u8(u16(DS_switch_positions + 2 * id)) << 3);
    const u8 h = ds_u8(u16(DS_switch_frame_sizes + 1 + si));
    const u16 dy_bottom = u16(row + h - 1);
    const u16 dx = u16(ds_u8(u16(DS_switch_positions + 1 + 2 * id)) << 3);
    if (dx == 0) return 0;
    const u8 w = ds_u8(u16(DS_switch_frame_sizes + si));
    si = u16(si << 2);
    si = u16(si + ((sw & 3) << 1));
    const u16 y0 = ds_u8(u16(DS_switch_frames + 1 + si));
    const u16 x0 = ds_u8(u16(DS_switch_frames + si));
    gfx_copy_rect(x0, u16(w + x0 - 1), y0, u16(h + y0 - 1), dx, dy_bottom, 1, ds_u16(DS_draw_page));
    return 0;
}

// 0919:0f77 jet_indicator_draw (hud.md §3; simulation.md §4.1): at the pilot's station, the two
// pictures of the waterjet direction for its phase (jet_indicator_phase), from page 1 rows 83h..9Ch
// to the bottom row C7h at the look direction's columns; the first only where its column is not 0.
// The arguments are pushed as words whose high byte is DH, the high byte of draw_page. Returns AX.
u16 jet_indicator_draw(u16 ax)
{
    if (ds_u16(DS_station) != 1) return ax;
    const u16 look = ds_u16(DS_look_direction);
    const u8 cl = ds_u8(u16(DS_jet_picture_a_dx + look));
    const u8 ch = ds_u8(u16(DS_jet_picture_b_dx + look));
    ds_u8(DS_scratch_b7e2) = ch;
    const u16 phase = ds_u8(DS_jet_indicator_phase);
    const u8 a = ds_u8(u16(DS_jet_picture_a_x + phase));
    ds_u8(DS_scratch_b7e3) = ds_u8(u16(DS_jet_picture_b_x + phase));
    const u16 dst = ds_u16(DS_draw_page);
    const u16 dh = dst & 0xFF00;
    const u16 src = u16(dh | 1), dy_bottom = u16(dh | 0xC7);
    if (cl != 0)
        gfx_copy_rect(u16(dh | a), u16(dh | u8(a + 0x1F)), u16(dh | 0x83), u16(dh | 0x9C), u16(dh | cl), dy_bottom, src,
                      dst);
    // SUB DH,DH: the second picture's own arguments are plain bytes
    const u16 x0 = ds_u8(DS_scratch_b7e3);
    gfx_copy_rect(x0, u16(x0 + 0x37), 0x83, 0x9C, ds_u8(DS_scratch_b7e2), dy_bottom, src, dst);
    return 0;
}

} // namespace gb
