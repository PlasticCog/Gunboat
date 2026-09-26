// Placeholders for functions not ported yet (pending.hpp).
#include "game/pending.hpp"

#include "host.hpp"

namespace gb {

// TODO(merge): the sound branch replaces these. Until then the effects timer is never installed
// (DS:DA46 stays 0), and in that state sfx_play does nothing, so doing nothing is exact; the
// timer and speaker effects of engine_sound_on/off and cms_silence are missing.
void sfx_play_far(u16) {}
void cms_silence() {}
void engine_sound_on() {}
void engine_sound_off() {}
void sfx_timer_isr() {}
void speaker_reset() {}
void music_tick() {}
void speaker_music_tick() {}

// Not ported yet: stop with a clear message instead of doing something else.
void show_message_far(u16 id) { host_fatal("not ported yet: show_message (0919:1589), message %02Xh", id); }

} // namespace gb
