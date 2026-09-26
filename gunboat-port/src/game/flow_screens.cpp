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

// 00f2:0e44 print_chars: exactly n characters of DS:base at the text cursor (no terminator test).
void print_chars(u16 base, u16 n)
{
    for (s16 i = 0; i < s16(n); i++) {
        const u8 c = ds_u8(u16(base + i));
        text_draw_char(&c);
    }
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

// 00f2:0f24 ega_pal_entry (video.md §3): a colour pattern of the library through ega_pal_set
// (14ae:0005; the value's low byte only in Tandy mode 9). In mode 13h ega_pal_set does nothing.
void ega_pal_entry(u16 index, u16 value)
{
    ega_pal_set(index, ds_u16(DS_video_mode) == 9 ? u8(value) : value);
}

// 00f2:0f46 ega_pal_apply (video.md §3): the colour patterns of the palette file into the library
// (ega_pal_entry): CGA (4) patterns 0..1Fh from cga_patterns; Tandy (9) patterns 10h..1Fh from
// ega_patterns, each colour & 0Fh doubled into both nibbles (* 11h); EGA (0Dh) patterns 10h..1Fh
// from ega_patterns; nothing in other modes.
void ega_pal_apply()
{
    switch (ds_u16(DS_video_mode)) {
    case 4:
        for (s16 i = 0; i < 0x20; i++) ega_pal_entry(u16(i), ds_u16(u16(DS_cga_patterns + 2 * i)));
        break;
    case 9:
        for (s16 i = 0; i < 0x10; i++)
            ega_pal_entry(u16(i + 0x10), u16((ds_u16(u16(DS_ega_patterns + 2 * i)) & 0x0F) * 0x11));
        break;
    case 0x0D:
        for (s16 i = 0; i < 0x10; i++) ega_pal_entry(u16(i + 0x10), ds_u16(u16(DS_ega_patterns + 2 * i)));
        break;
    default:
        break;
    }
}

// 00f2:0fea ega_pal_init (video.md §3): the palette registers of the palette file: Tandy (9) first
// converts ega_palette in place from the EGA's rgbRGB to its IRGB (bit 4, the 200-line intensity,
// becomes bit 3; bits 0-2 stay; the high byte is cleared), so a second call on the same table
// loses the intensity (kept); Tandy and EGA (0Dh) load the 16 registers (gfx_set_ega_palette);
// then the colour patterns (ega_pal_apply).
void ega_pal_init()
{
    const u16 mode = ds_u16(DS_video_mode);
    if (mode == 9) {
        s16 i = 0;
        do {
            const u16 si = u16(DS_ega_palette + 2 * i);
            ds_u16(si) = u16((ds_u8(si) & 0x10) >> 1 | (ds_u16(si) & 7));
        } while (++i < 0x10);
    }
    if (mode == 9 || mode == 0x0D) gfx_set_ega_palette(DS_ega_palette);
    ega_pal_apply();
}

} // namespace gb
