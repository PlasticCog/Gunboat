// Mission clock and time of day (simulation.md §9.2).
#include "game/sim.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

// 0919:1dd6 time_of_day (simulation.md §9.2): day flag CH and twilight stage CL from the clock
// (hours BCD, minutes binary): before 6:00 night (0, 0); 6:00-6:03 (0, 1); 6:04-6:06 (0, 2); from
// 6:07 to 19:54 day (1, 0); 19:55-19:57 (0, 2); 19:58-19:59 (0, 1); after 19h night. On a change:
// the terrain is rebuilt, the shade masks and the four scene colours are set for the video mode
// (VGA 18h/10h; others 8/4; CGA mode 4: 1/1 and the table 10h bytes further on).
void time_of_day()
{
    u8 ch = 0, cl = 0;
    const u8 bh = ds_u8(DS_clock_hours), bl = ds_u8(DS_clock_minutes);
    if (bh < 6) {
    } else if (bh == 6) {
        cl = 1;
        if (bl >= 4) {
            cl = 2;
            if (bl >= 7) {
                ch = 1;
                cl = 0;
            }
        }
    } else if (bh <= 0x19) {
        ch = 1;
        if (bh == 0x19 && bl > 0x36) {
            ch = 0;
            cl = 2;
            if (bl > 0x39) cl--;
        }
    }
    if (ds_u8(DS_daylight) == ch && ds_u8(DS_twilight_stage) == cl) return;
    ds_u8(DS_daylight) = ch;
    ds_u8(DS_twilight_stage) = cl;
    ds_u16(DS_terrain_rebuild) = 0xFFFF;
    u8 bx = u8(1 - ds_u8(DS_daylight) + ds_u8(DS_twilight_stage));
    u16 off = u16(bx << 2);
    ds_u8(DS_shade_mask_a) = 0x18;
    ds_u8(DS_shade_mask_b) = 0x10;
    if (u8(ds_u16(DS_video_mode)) != 0x13) {
        ds_u8(DS_shade_mask_a) = 8;
        ds_u8(DS_shade_mask_b) = 4;
        if (u8(ds_u16(DS_video_mode)) == 4) {
            off = u16((off & 0xFF00) | u8(off + 0x10));  // add bl,10h
            ds_u8(DS_shade_mask_a) = 1;
            ds_u8(DS_shade_mask_b) = 1;
        }
    }
    for (u16 i = 0; i < 4; i++) ds_u8(u16(DS_scene_colours + i)) = ds_u8(u16(DS_scene_colour_table + off + i));
}

} // namespace gb
