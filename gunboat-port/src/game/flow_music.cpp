// The title music (game_flow.md §3.1, sound.md §4).
#include "game/flow.hpp"

#include "game/pending.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

// 00f2:119c music_stop: while the menu timer runs: the CMS voices off, the timer restored, and the
// effects timer back (engine_sound_on).
void music_stop()
{
    if (ds_u8(DS_music_timer_on) == 1) {
        cms_silence();
        timer_restore();
        ds_u8(DS_music_timer_on) = 0;
        engine_sound_on();
    }
}

} // namespace gb
