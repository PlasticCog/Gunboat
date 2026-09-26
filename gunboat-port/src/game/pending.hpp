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

// not ported yet (fatal if reached): the cockpit messages (a mission only), the HQ, the front end,
// the mission
void show_message_far(u16 id);  // 0919:1589
u16 hq_quiz();                  // 020d:0008
void front_end();               // 02d2:0008
void mission_run();             // 05bd:000a
void joystick_calibrate(u16 stick);  // 146e:000e (with a joystick enabled)

} // namespace gb
