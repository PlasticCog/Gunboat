// The sound-effects driver (sound.md §2), segment 12ed: 13 effect programs played by the effects
// timer interrupt (236.695 Hz) on the PC speaker or, when sfx_device_tandy is 1, on the Tandy 3-voice
// chip. PORT: the Tandy chip is parked: its port writes (C0h) do nothing (tandy_out), but every memory
// effect of the Tandy branches is ported.
//
// The driver keeps up to four voices (DI = 0, 2, 4, 6 up to sfx_last_voice; the speaker has one) in
// word arrays indexed by DI, and works on one voice at a time through the work registers sfx_pc,
// sfx_remaining_work, sfx_legato_work and sfx_gap_work (sfx_channel_transfer).
#include "sound/sound.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 SFX_SEG = 0x12ED;
constexpr u16 SFX_ISR_OFF = 0x00A3;  // 12ed:00a3

bool tandy() { return ds_u8(DS_sfx_device_tandy) == 1; }

// Element DI (a byte offset) of a word array.
u16_m &voice(u16 array, u16 di) { return ds_u16(u16(array + di)); }

// SHR/SHL r16, CL as the 386 does them: the count is masked to 5 bits.
u16 shr16(u16 v, u8 cl)
{
    cl &= 0x1F;
    return cl >= 16 ? 0 : u16(v >> cl);
}
u16 shl16(u16 v, u8 cl)
{
    cl &= 0x1F;
    return cl >= 16 ? 0 : u16(v << cl);
}

// Voice DI off (inline in the original): the Tandy channel's attenuation to 0Fh, or the speaker gate.
void voice_off(u16 di)
{
    if (tandy())
        tandy_out(0xC0, u8(u8(u8(di) << 4) + 0x80) | 0x0F);
    else
        spk_gate(false);
}

// Everything off (inline in the original): the four Tandy attenuations, or the speaker gate.
void all_off()
{
    if (tandy()) {
        tandy_out(0xC0, 0x9F);
        tandy_out(0xC0, 0xBF);
        tandy_out(0xC0, 0xDF);
        tandy_out(0xC0, 0xFF);
    } else {
        spk_gate(false);
    }
}

// PUSHF / CALL FAR [sfx_old_vector]: the interrupt handler that was installed before. PORT: the game
// always finds the BIOS timer handler there (bios.cpp); any other vector is not modelled.
void call_old_vector()
{
    const FarPtr v = ds_far(DS_sfx_old_vector);
    if (v.seg == 0xF000 && v.off == 0xFEA5) {
        bios_tick();
        return;
    }
    host_fatal("sfx_timer_isr: chain to %04X:%04X is not modelled", v.seg, v.off);
}

// The Tandy volume of voice DI for this tick (12ed:059c..0640); false: nothing is written.
bool tandy_level(u16 di, u8 &al)
{
    if (voice(DS_sfx_voice_state, di) == 0) {
        al = 0x0F;
        return true;
    }
    if (voice(DS_sfx_note_started, di) == 1) {
        voice(DS_sfx_note_started, di) = 0;
        if (s16(voice(DS_sfx_tie, di)) >= 2) {
            if (voice(DS_sfx_tie, di) != 3) voice(DS_sfx_tie, di) = 0;
        } else {
            voice(DS_sfx_envelope_end, di) = 0;
            voice(DS_sfx_envelope_pos, di) = voice(DS_sfx_envelope, di);
        }
    } else {
        if (voice(DS_sfx_rest, di) == 1) {
            al = 0x0F;
            return true;
        }
        const u16 tie = voice(DS_sfx_tie, di);
        if (!(tie == 1 || tie == 3 || voice(DS_sfx_legato, di) == 1 ||
              s16(voice(DS_sfx_remaining, di)) > s16(voice(DS_sfx_gap, di)))) {
            al = 0x0F;
            return true;
        }
    }
    // 060b: the envelope
    if (voice(DS_sfx_envelope_end, di) == 0) {
        const u16 si = voice(DS_sfx_envelope_pos, di);
        voice(DS_sfx_envelope_pos, di)++;
        al = ds_u8(si);
        if (al == 0xFF) {
            voice(DS_sfx_envelope_end, di) = 1;
            return false;
        }
        return true;
    }
    if (s16(voice(DS_sfx_envelope_end, di)) > 1)
        al = 0x0F;
    else
        al = u8(voice(DS_sfx_volume, di));
    return true;
}

} // namespace

// 12ed:0000 engine_sound_on (sound.md §2.2): installs the effects timer unless sfx_timer_on is set.
void engine_sound_on()
{
    if (ds_u8(DS_sfx_timer_on) != 0) return;
    ds_u8(DS_sfx_timer_on) = 1;
    sfx_install();
}

// 12ed:0018 sfx_play_far (sound.md §2.2): the C entry of sfx_play.
void sfx_play_far(u16 id) { sfx_play(id); }

// 12ed:0025 sfx_play (sound.md §2.2): voice 0 plays program sfx_programs[id]; an id above 12 is
// used as the program address itself (quirk). Nothing while the effects timer is off.
void sfx_play(u16 ax)
{
    if (ds_u8(DS_sfx_timer_on) == 0) return;
    u16 bx = ax;
    if (bx <= 12) bx = ds_u16(u16(DS_sfx_programs + 2 * bx));
    // cli
    ds_u16(DS_sfx_program) = bx;
    if (tandy()) {  // the other Tandy voices play the program at DS:6E55
        ds_u16(u16(DS_sfx_program + 2)) = 0x6E55;
        ds_u16(u16(DS_sfx_program + 4)) = 0x6E55;
        ds_u16(u16(DS_sfx_program + 6)) = 0x6E55;
    }
    ds_u16(DS_sfx_state) = 1;
    // sti
}

// 12ed:0063 engine_sound_off (sound.md §2.2): stops the effect and removes the effects timer.
void engine_sound_off()
{
    if (ds_u8(DS_sfx_timer_on) != 1) return;
    ds_u8(DS_sfx_timer_on) = 0;
    ds_u16(DS_sfx_state) = 0;
    all_off();
    sfx_remove();
}

// 12ed:00a3 sfx_timer_isr (platform.md §1, sound.md §2.3), with its body at 12ed:00b1: tick_counter
// advances on 4 of every 13 interrupts (72.83 Hz), the old handler runs on every 13th (18.21 Hz),
// then one effects step. PORT: the register saves, STI and the EOI (out 20h,20h) have no effect here.
void sfx_timer_isr()
{
    if ((ds_u16(DS_sfx_timer_divider) & 3) == 0) ds_u16(DS_tick_counter)++;
    ds_u16(DS_sfx_timer_divider)++;
    if (s16(ds_u16(DS_sfx_timer_divider)) >= 13) {
        ds_u16(DS_sfx_timer_divider) = 0;
        call_old_vector();
    }
    sfx_timer_tick();
}

// 12ed:00eb sfx_timer_tick (sound.md §2.3): one step of every voice.
void sfx_timer_tick()
{
    if (ds_u16(DS_sound_muted) != 0) {
        all_off();
        return;
    }
    if (ds_u16(DS_sfx_secondary_state) != 0) {  // the secondary sequence (never started in the game)
        if (ds_u16(DS_sfx_secondary_state) == 1) {
            ds_u16(DS_sfx_saved_voice_state) = ds_u16(DS_sfx_voice_state);
            ds_u16(DS_sfx_saved_rest) = ds_u16(DS_sfx_rest);
            ds_u16(DS_sfx_saved_tie) = ds_u16(DS_sfx_tie);
            ds_u16(DS_sfx_saved_note_started) = ds_u16(DS_sfx_note_started);
            ds_u16(DS_sfx_saved_volume) = ds_u16(DS_sfx_volume);
            ds_u16(DS_sfx_saved_envelope) = ds_u16(DS_sfx_envelope);
        }
        ds_u16(DS_sfx_voice_state) = ds_u16(DS_sfx_secondary_state);
        ds_u16(DS_sfx_tempo_work) = ds_u16(DS_sfx_secondary_tempo);
        ds_u16(DS_sfx_tempo_shift_work) = ds_u16(DS_sfx_secondary_tempo_shift);
        sfx_secondary_step(0);
        ds_u16(DS_sfx_secondary_tempo) = ds_u16(DS_sfx_tempo_work);
        ds_u16(DS_sfx_secondary_tempo_shift) = ds_u16(DS_sfx_tempo_shift_work);
        ds_u16(DS_sfx_secondary_state) = ds_u16(DS_sfx_voice_state);
        if (ds_u16(DS_sfx_secondary_state) == 0) {
            ds_u16(DS_sfx_voice_state) = ds_u16(DS_sfx_saved_voice_state);
            ds_u16(DS_sfx_rest) = ds_u16(DS_sfx_saved_rest);
            ds_u16(DS_sfx_tie) = ds_u16(DS_sfx_saved_tie);
            ds_u16(DS_sfx_note_started) = ds_u16(DS_sfx_saved_note_started);
            ds_u16(DS_sfx_volume) = ds_u16(DS_sfx_saved_volume);
            ds_u16(DS_sfx_envelope) = ds_u16(DS_sfx_saved_envelope);
        }
        return;
    }
    if (ds_u16(DS_sfx_state) == 0) return;
    if (ds_u16(DS_sfx_state) == 1) {  // sfx_play started an effect: every voice starts
        ds_u16(DS_sfx_state) = 2;
        u16 di = 0;
        do {
            voice(DS_sfx_voice_state, di) = 1;
            di = u16(di + 2);
        } while (s16(di) <= s16(ds_u16(DS_sfx_last_voice)));
    }
    ds_u16(DS_sfx_tempo_work) = ds_u16(DS_sfx_tempo);
    ds_u16(DS_sfx_tempo_shift_work) = ds_u16(DS_sfx_tempo_shift);
    u16 di = 0;
    do {
        const u16 state = voice(DS_sfx_voice_state, di);
        if (state != 0) {
            if (state == 1 || voice(DS_sfx_remaining, di) == 0)
                sfx_channel_transfer(di);
            else if (ds_u16(DS_sfx_legato_work) == 0 && voice(DS_sfx_remaining, di) == ds_u16(DS_sfx_gap_work))
                voice_off(di);  // the release gap (with the last voice's legato and gap: quirk)
        }
        voice(DS_sfx_remaining, di)--;
        di = u16(di + 2);
    } while (s16(di) <= s16(ds_u16(DS_sfx_last_voice)));
    ds_u16(DS_sfx_tempo) = ds_u16(DS_sfx_tempo_work);
    ds_u16(DS_sfx_tempo_shift) = ds_u16(DS_sfx_tempo_shift_work);
    u16 si = 0;
    di = 0;
    do {
        si |= voice(DS_sfx_voice_state, di);
        if (si != 0) {
            sfx_tandy_envelope();
            return;
        }
        di = u16(di + 2);
    } while (s16(di) <= s16(ds_u16(DS_sfx_last_voice)));
    all_off();  // every voice has ended
    ds_u16(DS_sfx_state) = 0;
}

// 12ed:0284 sfx_secondary_step: the secondary sequence's registers through the work registers.
void sfx_secondary_step(u16 di)
{
    ds_u16(DS_sfx_pc) = ds_u16(DS_sfx_secondary_program);
    ds_u16(DS_sfx_remaining_work) = ds_u16(DS_sfx_secondary_remaining);
    ds_u16(DS_sfx_legato_work) = ds_u16(DS_sfx_secondary_legato);
    ds_u16(DS_sfx_gap_work) = ds_u16(DS_sfx_secondary_gap);
    sfx_channel_step(di);
    ds_u16(DS_sfx_secondary_program) = ds_u16(DS_sfx_pc);
    ds_u16(DS_sfx_secondary_remaining) = ds_u16(DS_sfx_remaining_work);
    ds_u16(DS_sfx_secondary_legato) = ds_u16(DS_sfx_legato_work);
    ds_u16(DS_sfx_secondary_gap) = ds_u16(DS_sfx_gap_work);
}

// 12ed:02b8 sfx_channel_transfer: voice DI's registers through the work registers.
void sfx_channel_transfer(u16 di)
{
    ds_u16(DS_sfx_pc) = voice(DS_sfx_program, di);
    ds_u16(DS_sfx_remaining_work) = voice(DS_sfx_remaining, di);
    ds_u16(DS_sfx_legato_work) = voice(DS_sfx_legato, di);
    ds_u16(DS_sfx_gap_work) = voice(DS_sfx_gap, di);
    sfx_channel_step(di);
    voice(DS_sfx_program, di) = ds_u16(DS_sfx_pc);
    voice(DS_sfx_remaining, di) = ds_u16(DS_sfx_remaining_work);
    voice(DS_sfx_legato, di) = ds_u16(DS_sfx_legato_work);
    voice(DS_sfx_gap, di) = ds_u16(DS_sfx_gap_work);
}

// 12ed:02f4 sfx_channel_step
void sfx_channel_step(u16 di)
{
    if (voice(DS_sfx_voice_state, di) != 2) sfx_channel_start(di);
    sfx_channel_run(di);
}

// 12ed:0302 sfx_channel_start: a voice's defaults (gap 6, raw length 1).
void sfx_channel_start(u16 di)
{
    ds_u8(DS_sfx_bad_command_count) = 0;
    ds_u8(DS_sfx_range_count) = 0;
    ds_u16(DS_sfx_legato_work) = 0;
    ds_u16(DS_sfx_gap_work) = 6;
    ds_u16(DS_sfx_tempo_shift_work) = 0;
    ds_u16(DS_sfx_raw_length) = 1;
    voice(DS_sfx_rest, di) = 0;
    voice(DS_sfx_tie, di) = 0;
    ds_u16(DS_sfx_remaining_work) = 0;
    voice(DS_sfx_voice_state, di) = 2;
}

// 12ed:033d sfx_channel_run (sound.md §2.3): the speaker cuts the note when the remaining ticks reach
// the gap (or 4, below the gap), unless legato; at 0 the next commands run until one takes time or
// the voice ends. Tandy voices always run the command loop (their gap is the envelope's).
void sfx_channel_run(u16 di)
{
    if (!tandy()) {
        const u16 ax = ds_u16(DS_sfx_remaining_work);
        if (ds_u16(DS_sfx_legato_work) != 1) {
            const s16 gap = s16(ds_u16(DS_sfx_gap_work));
            if (s16(ax) == gap || (s16(ax) < gap && ax == 4)) {
                voice_off(di);
                return;
            }
        }
        if (ax != 0) return;
    }
    do {
        sfx_parse_command(di);
    } while (voice(DS_sfx_voice_state, di) != 0 && ds_u16(DS_sfx_remaining_work) == 0);
}

// 12ed:03c4 sfx_parse_command (sound.md §2.4): the 2-byte command at sfx_pc; low nibble of the
// first byte: 0 rest, 1..12 note, 13 control, 14 raw pitch, 15 end.
void sfx_parse_command(u16 di)
{
    ds_u16(DS_sfx_remaining_work) = 0;
    const u8 b = ds_u8(ds_u16(DS_sfx_pc));
    const u8 ah = u8(b >> 4);
    const u8 al = b & 0x0F;
    const u16 ax = u16(ah << 8 | al);
    if (al == 0) {
        voice_off(di);
        voice(DS_sfx_rest, di) = 1;
        sfx_note_duration(di);
    } else if (s8(al) <= 12) {
        sfx_note_on(ax, di);
        voice(DS_sfx_rest, di) = 0;
        sfx_note_duration(di);
    } else if (al == 13) {
        sfx_control(ax, di);
    } else if (al == 14) {
        sfx_raw_pitch(ax, di);
    } else if (al == 15) {
        voice(DS_sfx_voice_state, di) = 0;
        voice_off(di);
    } else {
        ds_u8(DS_sfx_bad_command_count)++;  // unreachable
    }
    ds_u16(DS_sfx_pc) = u16(ds_u16(DS_sfx_pc) + 2);
}

// 12ed:0472 sfx_note_on (sound.md §2.4): note AL (1..12) shifted down AH octaves:
// divisor = sfx_divisor_table[AL - 1] >> AH, then the gate on. Tandy: octave AH - 1 (0 counted in
// sfx_range_count), divisors above 3FFh halved once; voice 6 is the noise channel.
void sfx_note_on(u16 ax, u16 di)
{
    u8 al = u8(ax), ah = u8(ax >> 8);
    if (tandy()) {
        if (di == 6) {  // noise: control E0h + note - 1, envelope AH - 1
            al = u8(al + 0xE0);
            al--;
            tandy_out(0xC0, al);
            if (ah != 0) {
                ah--;
                sfx_cmd_envelope(ah, di);
            }
            return;
        }
        if (s8(ah) >= 1)
            ah--;
        else
            ds_u8(DS_sfx_range_count)++;
    }
    const u8 cl = ah;
    al--;
    al = u8(al << 1);
    u16 d = shr16(ds_u16(u16(ds_u16(DS_sfx_divisor_table) + al)), cl);
    if (tandy() && s16(d) > 0x3FF) {
        d = u16(d >> 1);
        ds_u8(DS_sfx_range_count)++;
    }
    if (!tandy()) {
        spk_pit2_divisor(d);
        spk_gate(true);
    } else {
        tandy_out(0xC0, u8(u8(u8(u8(di) << 4) + 0x80) | (d & 0x0F)));
        tandy_out(0xC0, u8(d >> 4));
    }
}

// 12ed:0523 sfx_note_duration (sound.md §2.4): sfx_remaining_work from the second byte d:
// (tempo + shift) >> (d & 7), plus half of it if d & 8 (dotted); d & 10h ties. Quirk: the dotted
// case replaces AH (the byte d) with the high byte of the half before the tie test.
void sfx_note_duration(u16 di)
{
    voice(DS_sfx_note_started, di) = 1;
    const u8 d = ds_u8(u16(ds_u16(DS_sfx_pc) + 1));
    u8 ah = d;
    u16 bx = u16(ds_u16(DS_sfx_tempo_work) + ds_u16(DS_sfx_tempo_shift_work));
    const u8 al = d & 7;
    if (al != 0) bx = shr16(bx, al);
    if (ah & 8) {
        const u16 half = u16(bx >> 1);
        ah = u8(half >> 8);
        bx = u16(bx + half);
    }
    u16_m &tie = voice(DS_sfx_tie, di);
    if (ah & 0x10) {
        if (tie != 3) {
            if (tie == 1)
                tie = 3;
            else
                tie++;
        }
    } else if (tie != 0) {
        tie++;
    }
    ds_u16(DS_sfx_remaining_work) = bx;
}

// 12ed:0586 sfx_tandy_envelope (sound.md §2.5): the attenuation of every Tandy voice from its
// envelope. Only memory effects are ported (PORT: the chip is parked).
void sfx_tandy_envelope()
{
    if (!tandy()) return;
    u8 bl = 0x90;
    u16 di = 0;
    do {
        u8 al = 0;
        if (tandy_level(di, al)) tandy_out(0xC0, u8(al + bl));
        bl = u8(bl + 0x20);
        di = u16(di + 2);
    } while (s16(di) <= s16(ds_u16(DS_sfx_last_voice)));
}

// 12ed:0655 sfx_control (sound.md §2.4): command xD, sub-command AH, argument = the second byte.
void sfx_control(u16 ax, u16 di)
{
    const u16 bx = ds_u16(DS_sfx_pc);
    const u8 al = ds_u8(u16(bx + 1));
    const u8 ah = u8(ax >> 8);
    if (ah == 0) {
        sfx_cmd_legato(al);
    } else if (s8(ah) < 3) {
        sfx_cmd_tempo_shift(u16(ah << 8 | al));
    } else if (ah == 3) {
        sfx_cmd_raw_length(al);
    } else if (ah == 4) {
        sfx_cmd_tempo(al);
    } else if (tandy() && ah == 5) {
        sfx_cmd_envelope(al, di);
    } else if (tandy() && ah == 6) {
        sfx_cmd_volume(al, di);
    } else if (ah == 7) {  // loop count
        voice(DS_sfx_loop_count, di) = al;
    } else if (ah == 8) {  // loop: back by the argument while the count lasts (0: forever)
        u16_m &count = voice(DS_sfx_loop_count, di);
        if (count != 0 && --count == 0) return;
        ds_u16(DS_sfx_pc) = u16(bx - al);
    }
}

// 12ed:06d1 sfx_cmd_legato (D0): 0 = legato; otherwise the gap's low byte (quirk: byte store).
void sfx_cmd_legato(u8 al)
{
    if (al != 0) {
        ds_u16(DS_sfx_legato_work) = 0;
        ds_u8(DS_sfx_gap_work) = al;
    } else {
        ds_u16(DS_sfx_legato_work) = 1;
    }
}

// 12ed:06ec sfx_cmd_tempo_shift (D1 / D2): shift = +- round(tempo * AL / 100) (remainder > 49 up).
void sfx_cmd_tempo_shift(u16 ax)
{
    u16 cx = u8(ax);
    if (cx == 0) {
        ds_u16(DS_sfx_tempo_shift_work) = 0;
        return;
    }
    u16 rem = 0;
    u16 q = div32_16(u32(ds_u16(DS_sfx_tempo_work)) * cx, 100, &rem);
    if (s16(rem) > 0x31) q++;
    cx = q;
    if (u8(ax >> 8) != 1) cx = u16(-cx);
    ds_u16(DS_sfx_tempo_shift_work) = cx;
}

// 12ed:0726 sfx_cmd_tempo (D4): tempo = AL * 4.
void sfx_cmd_tempo(u8 al) { ds_u16(DS_sfx_tempo_work) = u16(al << 2); }

// 12ed:0732 sfx_cmd_envelope (D5, Tandy): envelope AL of the table sfx_tandy_envelopes.
void sfx_cmd_envelope(u8 al, u16 di)
{
    voice(DS_sfx_envelope, di) = ds_u16(u16(DS_sfx_tandy_envelopes + u8(al << 1)));
}

// 12ed:0747 sfx_cmd_volume (D6, Tandy)
void sfx_cmd_volume(u8 al, u16 di) { voice(DS_sfx_volume, di) = al; }

// 12ed:074e sfx_cmd_raw_length (D3): the low byte of the raw pitch length.
void sfx_cmd_raw_length(u8 al) { ds_u8(DS_sfx_raw_length) = al; }

// 12ed:0752 sfx_raw_pitch (sound.md §2.4): command xE with argument p: p = 0 silences; otherwise
// divisor = (sfx_divisor_table[0] - (p << sfx_pitch_shift)) >> AH for sfx_raw_length ticks.
void sfx_raw_pitch(u16 ax, u16 di)
{
    u16 dx = ds_u8(u16(ds_u16(DS_sfx_pc) + 1));
    if (dx == 0) {
        voice_off(di);
        return;
    }
    dx = shl16(dx, ds_u8(DS_sfx_pitch_shift));
    dx = u16(-dx);
    const u8 cl = u8(ax >> 8);
    const u16 d = shr16(u16(ds_u16(ds_u16(DS_sfx_divisor_table)) + dx), cl);
    if (!tandy()) {
        spk_pit2_divisor(d);
        spk_gate(true);
    } else {
        tandy_out(0xC0, u8(u8(u8(u8(di) << 4) + 0x80) | (d & 0x0F)));
        tandy_out(0xC0, u8(d >> 4));
    }
    ds_u16(DS_sfx_remaining_work) = ds_u16(DS_sfx_raw_length);
}

// 12ed:0800 sfx_speaker_init (sound.md §2.2): the device's tables and vector; PIT channel 2 in
// square-wave mode (speaker) or the chip silenced and enabled (Tandy).
void sfx_speaker_init()
{
    ds_u16(DS_sfx_state) = 0;
    ds_u16(DS_sfx_secondary_state) = 0;
    for (u16 v = 0; v < 8; v += 2) ds_u16(u16(DS_sfx_voice_state + v)) = 0;
    if (tandy()) {
        ds_u16(DS_sfx_divisor_table) = DS_sfx_tandy_divisors;
        ds_u8(DS_sfx_pitch_shift) = 1;
        ds_u16(DS_sfx_voice_count) = 4;
        ds_u16(DS_sfx_last_voice) = 6;
        ds_u8(DS_sfx_timer_vector) = 0x1C;
    } else {
        ds_u16(DS_sfx_divisor_table) = DS_sfx_speaker_divisors;
        ds_u8(DS_sfx_pitch_shift) = 7;
        ds_u16(DS_sfx_voice_count) = 1;
        ds_u16(DS_sfx_last_voice) = 0;
        ds_u8(DS_sfx_timer_vector) = 8;
    }
    if (!tandy()) {
        spk_pit2_mode();
        return;
    }
    all_off();
    tandy_enable();
}

// 12ed:089d sfx_silence
void sfx_silence() { all_off(); }

// 12ed:08c0 sfx_install (sound.md §2.2): saves the vector sfx_timer_vector (DOS 35h) in
// sfx_old_vector, points it at sfx_timer_isr (DOS 25h) and programs PIT channel 0 to 13B1h.
// PORT: the PIT rate and the handler go to the host (integration: the host's INT 8 dispatch may
// replace the handler argument).
void sfx_install()
{
    sfx_speaker_init();
    const u16 v = u16(ds_u8(DS_sfx_timer_vector) * 4);
    ds_u16(DS_sfx_old_vector) = mem_u16(0, v);
    ds_u16(u16(DS_sfx_old_vector + 2)) = mem_u16(0, u16(v + 2));
    mem_u16(0, v) = SFX_ISR_OFF;
    mem_u16(0, u16(v + 2)) = seg_of(SFX_SEG);
    host_set_timer(PIT_DIV_MISSION, sfx_timer_isr);
}

// 12ed:08f3 sfx_remove (sound.md §2.2): silence, the old vector back, PIT channel 0 to 18.2 Hz.
// PORT: the host runs the BIOS timer handler again (the vector the game restores).
void sfx_remove()
{
    sfx_silence();
    const u16 v = u16(ds_u8(DS_sfx_timer_vector) * 4);
    mem_u16(0, v) = ds_u16(DS_sfx_old_vector);
    mem_u16(0, u16(v + 2)) = ds_u16(u16(DS_sfx_old_vector + 2));
    host_set_timer(PIT_DIV_BIOS, bios_tick);
}

} // namespace gb
