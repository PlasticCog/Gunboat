// The engine noise (sound.md §2.1): game_frame rewrites effect 6 from the throttles each frame.
#include "sound/sound.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Bytes of effect 6 (engine_sfx_program, DS:DB9F): the tempo argument, the loop count (1 = stop
// after this pass) and the pitch bytes of its six notes.
constexpr u16 TEMPO = DS_engine_sfx_program + 1;  // DBA0
constexpr u16 LOOPS = DS_engine_sfx_program + 3;  // DBA2
constexpr u16 NOTE_A5 = DS_engine_sfx_program + 6, NOTE_A7 = DS_engine_sfx_program + 8,
              NOTE_A9 = DS_engine_sfx_program + 10, NOTE_AB = DS_engine_sfx_program + 12,
              NOTE_AD = DS_engine_sfx_program + 14, NOTE_AF = DS_engine_sfx_program + 16;

} // namespace

// 0919:3cc9 engine_sound_update (sound.md §2.1, simulation.md §1.2): with the E toggle (DS:0080) set
// or both throttles near zero ((B816 + B817) >> 2 == 0, 8-bit sum) the engine effect is told to
// stop (loop count 1). Otherwise n = that value >> 1: tempo 26h - n; a base pitch k = n + 1 (or
// n / 2 + 5 from n = 8); the six notes take the pitches k, k+2, k+3, k+4, k+6, k+8 in the order
// A7 AD A9 A5 AF AB while they stay <= 0Ch, and from the first one above that the other sequence
// k+4, k+6, k+7, k+8, k+10, k+12. Effect 6 is (re)started when no effect is playing.
void engine_sound_update()
{
    if (ds_u16(DS_e_toggle) != 0) {
        ds_u8(LOOPS) = 1;
        return;
    }
    u8 al = u8(u8(ds_u8(DS_throttle) + ds_u8(DS_throttle + 1)) >> 2);
    if (al == 0) {
        ds_u8(LOOPS) = 1;
        return;
    }
    al = u8(al >> 1);
    u8 ah = al;
    ds_u8(TEMPO) = u8(-al + 0x26);
    ds_u8(LOOPS) = 0;
    if (ah >= 8) ah = u8((ah >> 1) + 4);
    ah++;
    u16 ax = u16((ah + 4) << 8 | ah);  // mov al,ah / add ah,4
    // Each note: the AL sequence while AL <= 0Ch, then from the first larger one the AH sequence.
    const u16 notes[6] = {NOTE_A7, NOTE_AD, NOTE_A9, NOTE_A5, NOTE_AF, NOTE_AB};
    const u16 al_step[6] = {0x202, 0x101, 0x101, 0x202, 0x202, 0};  // added after each AL note
    const u8 ah_step[6] = {2, 1, 1, 2, 2, 0};                       // added after each AH note
    int i = 0;
    for (; i < 6; i++) {
        if (u8(ax) > 0x0C) break;
        ds_u8(notes[i]) = u8(ax);
        ax = u16(ax + al_step[i]);
    }
    for (; i < 6; i++) {
        ds_u8(notes[i]) = u8(ax >> 8);
        ax = u16(ax + (ah_step[i] << 8));
    }
    if (ds_u16(DS_sfx_state) == 0) sfx_play(6);
}

} // namespace gb
