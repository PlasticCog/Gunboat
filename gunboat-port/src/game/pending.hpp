#pragma once
// Functions the title flow calls whose port is not in this tree yet (pending.cpp). Each goes away
// when its real port arrives: the sound code (a parallel branch), the cockpit messages (the mission
// port).
#include "types.hpp"

namespace gb {

// sound (12ed, 1ace): TODO(merge): replaced by src/sound when that branch is merged
void sfx_play_far(u16 id);  // 12ed:0018
void cms_silence();         // 1ace:005c
void engine_sound_on();     // 12ed:0000
void engine_sound_off();    // 12ed:0063
void sfx_timer_isr();       // 12ed:00a3
void speaker_reset();       // 1b37:0002
void music_tick();          // 1af5:0006
void speaker_music_tick();  // 1b37:00c0

// cockpit messages (simulation.md §9): not ported yet; reached only in a mission
void show_message_far(u16 id);  // 0919:1589

} // namespace gb
