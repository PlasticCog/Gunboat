// The game font and the text cursor (platform.md §7), segment 121b.
#include "platform/platform.hpp"

#include "mem.hpp"
#include "platform/gfx.hpp"
#include "symbols.hpp"

namespace gb {

// 121b:0380 far_normalize: DX:AX = (seg + off / 16):0000.
FarPtr far_normalize(FarPtr p) { return {0, u16(p.seg + (p.off >> 4))}; }

// 121b:0397 text_set_colours: the colours are kept in the high bytes.
void text_set_colours(s16 fg, s16 bg)
{
    ds_u16(DS_text_bg) = u16(u8(bg) << 8);
    ds_u16(DS_text_fg) = u16(u8(fg) << 8);
}

// 121b:03b0 text_goto_cell: row in character cells (8 pixels), column.
void text_goto_cell(s16 row, s16 col)
{
    ds_u8(DS_text_y) = u8(row << 3);
    ds_u8(DS_text_col) = u8(col);
}

// 121b:03c7 text_goto: row in pixels, column in cells.
void text_goto(s16 y, s16 col)
{
    ds_u8(DS_text_y) = u8(y);
    ds_u8(DS_text_col) = u8(col);
}

// 121b:03d8 text_draw_char (platform.md §7): one character c[0] in the fixed 8x8 cell at the cursor;
// characters below 20h are not drawn. The glyph (bottom row first) is copied to text_glyph and drawn
// upward from the cell's bottom row in the foreground colour, then, unless text_transparent, its
// inverse in the background colour. The column advances.
void text_draw_char(const u8 *c)
{
    const u8 ch = c[0];
    if (ch >= 0x20) {
        const u16 glyph = u16((ch << 3) + 0x6E - 0x100);
        for (u16 i = 0; i < 8; i++) ds_u8(u16(DS_text_glyph + i)) = seg_u8(CSSEG_font_8x8, u16(glyph + i));
        if (u8(ds_u16(DS_video_mode)) == 0x0D) {
            // PORT: the EGA planar path (121b:049c) is not ported.
        } else {
            gfx_move_to(s16(ds_u8(DS_text_col) << 3), s16(ds_u8(DS_text_y) + 7));
            gfx_set_colour(s16(ds_u16(DS_text_fg) >> 8));
            gfx_draw_bitmap(DS_text_glyph, 1, 8);
            gfx_set_colour(s16(ds_u16(DS_text_bg) >> 8));
            if (ds_u8(DS_text_transparent) == 0) {
                for (u16 i = 0; i < 8; i += 2) ds_u16(u16(DS_text_glyph + i)) ^= 0xFFFF;
                gfx_draw_bitmap(DS_text_glyph, 1, 8);
            }
        }
    }
    ds_u8(DS_text_col)++;
}

} // namespace gb
