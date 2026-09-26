// The title music (game_flow.md §3.1, sound.md §4).
#include "game/flow.hpp"

#include <cstring>

#include "mem.hpp"
#include "platform/platform.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {
constexpr u16 S_VALKPC = 0x09A4, S_VALK12 = 0x09AF, S_VALK3V = 0x09BA;
constexpr u16 TIMBRE_A = 0x0822, TIMBRE_B = 0x0856, TIMBRE_C = 0x088A;  // AdLib instruments (DGROUP)

void ds_strcpy(u16 dst, u16 src)  // 15ee:0786 strcpy
{
    u16 i = 0;
    do ds_u8(u16(dst + i)) = ds_u8(u16(src + i));
    while (ds_u8(u16(src + i++)) != 0);
}
} // namespace

// 00f2:1044 music_start (game_flow.md §3.1): the sound device is detected, the music file chosen by
// it (the speaker plays VALKPC.MUS), the AdLib instruments set, the menu timer installed (also when
// the sound is off), and the file loaded into the far buffer bow_art2_far and started, looping.
void music_start()
{
    sound_detect(0x0F, far_normalize(ds_far(DS_world_b_far)), far_normalize(ds_far(DS_tile_bin_offset)));
    if (ds_u16(DS_sound_muted) == 0) {
        ds_strcpy(DS_name_buffer, S_VALKPC);
        const u16 device = ds_u16(DS_sound_device);
        if (device == 8 || device == 4) ds_strcpy(DS_name_buffer, S_VALK12);
        if (device == 1 || device == 2) ds_strcpy(DS_name_buffer, S_VALK3V);
        if (device == 4) {  // AdLib: the instruments of voices 4..8 (INT 65h; parked, sound.md)
            adlib_call(4, TIMBRE_C, DGROUP);
            for (s16 v = 5; v < 8; v++) adlib_call(u16(v), TIMBRE_B, DGROUP);
            adlib_call(8, TIMBRE_A, DGROUP);
        }
    }
    timer_install();
    ds_u8(DS_music_timer_on) = 1;
    if (ds_u16(DS_sound_muted) != 0) return;
    const u16 fh = mus_open(DS_name_buffer);
    if (s16(fh) <= 0) return;
    StackLocal count(2);  // the byte count at the start of the file (a local of the original)
    mus_read(fh, count.far(), 2);
    mus_read(fh, ds_far(DS_bow_art2_far), ds_u16(count.off()));
    mus_close(fh);
    music_play(ds_far(DS_bow_art2_far), 1);
}

// 00f2:119c music_stop: while the menu timer runs: the music silenced, the timer restored, and
// the effects timer back (engine_sound_on).
void music_stop()
{
    if (ds_u8(DS_music_timer_on) == 1) {
        music_silence();
        timer_restore();
        ds_u8(DS_music_timer_on) = 0;
        engine_sound_on();
    }
}

} // namespace gb
