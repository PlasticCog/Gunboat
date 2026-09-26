// Screens of the game flow (game_flow.md §3; the shared routines are Test Drive III's, TD3
// game_flow.md §4.6): text records, the dissolve, the credits, the VGA palette wrappers.
#include "game/flow.hpp"

#include "mem.hpp"
#include "platform/gfx.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

// 00f2:0dbc print_records: text records {col, row, text} from DS:base + off; a byte >= 80h ends a
// text: 80h = another record follows, AAh = the end. Returns the offset after the last record.
u16 print_records(u16 base, u16 off)
{
    u8 c = 0;
    do {
        const u16 si = u16(off + base);
        text_goto_cell(ds_u8(u16(si + 1)), ds_u8(si));
        for (;;) {
            c = ds_u8(u16(base + off + 2));
            if (c >= 0x80) break;
            text_draw_char(&c);
            off++;
        }
        off = u16(off + 3);
    } while (c != 0xAA);
    return off;
}

// 00f2:0e10 print_text: one text from DS:base + off at the cursor, up to a byte >= 80h. Returns the
// offset after that byte.
u16 print_text(u16 base, u16 off)
{
    for (;;) {
        u8 c = ds_u8(u16(off + base));
        if (c >= 0x80) break;
        text_draw_char(&c);
        off++;
    }
    return u16(off + 1);
}

// 00f2:0ece screen_present: page 1 onto the screen with the dissolve.
void screen_present()
{
    ds_u16(DS_draw_page) = 0;
    gfx_set_draw_page(0);
    dissolve_page1_to_0();
}

// 00f2:0d3e credits_text (game_flow.md §3): the credit lines one group at a time on text row 24,
// each shown for 45 BIOS ticks (times title_delay); any key ends them. Returns 0.
u16 credits_text(u16 records)
{
    u16 off = 0;
    ds_u16(DS_draw_page) = 0;
    gfx_set_draw_page(0);
    for (;;) {
        off = print_records(records, off);
        const u16 si = u16(off + records);
        if (u16(ds_u8(u16(si + 1)) + ds_u8(si)) == 0) {
            wait_key(u16(0x64 * ds_u16(DS_title_delay)));
            return 0;
        }
        bios_wait_ticks(s16(u16(0x2D * ds_u16(DS_title_delay))));
        u16 key = random();
        input_read_key(&key);
        if (key != 0) return 0;
    }
}

// 00f2:0eec..0f16 the palette routines, VGA only
void pal_apply_vga()
{
    if (ds_u16(DS_video_mode) == 0x13) pal_apply();
}
void pal_black_vga()
{
    if (ds_u16(DS_video_mode) == 0x13) pal_black();
}
void pal_fade_in_vga()
{
    if (ds_u16(DS_video_mode) == 0x13) pal_fade_in();
}
void pal_fade_out_vga()
{
    if (ds_u16(DS_video_mode) == 0x13) pal_fade_out();
}

// 00f2:0f24 ega_pal_entry: an EGA palette register (ega_pal_set 14ae:0005). PORT: the EGA, Tandy and
// CGA palettes are not ported; the callers use it only in those modes.
void ega_pal_entry(u16, u16) {}

// 00f2:0f46 ega_pal_apply: the 16 (EGA, Tandy) or 32 (CGA) palette registers from the tables
// DS:0944 / DS:0964; nothing in other modes. PORT: those modes are not ported.
void ega_pal_apply()
{
    const u16 mode = ds_u16(DS_video_mode);
    if (mode != 4 && mode != 9 && mode != 0x0D) return;
}

// 00f2:0fea ega_pal_init: Tandy and EGA palette set-up (PORT: not ported), then ega_pal_apply.
void ega_pal_init() { ega_pal_apply(); }

} // namespace gb
