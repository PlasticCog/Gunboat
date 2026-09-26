// Bridge entries: sound (sound.md), and the speaker hardware state and timer log for soundmodel.py.
#include "bridge.hpp"
#include "host_stub.hpp"
#include "sound/sound.hpp"

#if defined(_WIN32)
#define GB_SOUND_EXPORT extern "C" __declspec(dllexport)
#else
#define GB_SOUND_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace gb;

namespace {
FarPtr far_arg(const u16 *a, int i) { return {a[i], a[i + 1]}; }
// ES:SI of a near script op of 1b37 (the ops return the moved pointer).
FarPtr es_si(const Regs &r) { return {r.si, r.es}; }
void set_es_si(Regs &r, FarPtr p)
{
    r.si = p.off;
    r.es = p.seg;
}
} // namespace

// ---- effects, 12ed
BRIDGE(engine_sound_on) { engine_sound_on(); }
BRIDGE(sfx_play_far) { sfx_play_far(a[0]); }
BRIDGE(sfx_play) { sfx_play(r.ax); }
BRIDGE(engine_sound_off) { engine_sound_off(); }
BRIDGE(sfx_timer_isr) { sfx_timer_isr(); }
BRIDGE(sfx_timer_tick) { sfx_timer_tick(); }
BRIDGE(sfx_secondary_step) { sfx_secondary_step(r.di); }
BRIDGE(sfx_channel_transfer) { sfx_channel_transfer(r.di); }
BRIDGE(sfx_channel_step) { sfx_channel_step(r.di); }
BRIDGE(sfx_channel_start) { sfx_channel_start(r.di); }
BRIDGE(sfx_channel_run) { sfx_channel_run(r.di); }
BRIDGE(sfx_parse_command) { sfx_parse_command(r.di); }
BRIDGE(sfx_note_on) { sfx_note_on(r.ax, r.di); }
BRIDGE(sfx_note_duration) { sfx_note_duration(r.di); }
BRIDGE(sfx_tandy_envelope) { sfx_tandy_envelope(); }
BRIDGE(sfx_control) { sfx_control(r.ax, r.di); }
BRIDGE(sfx_cmd_legato) { sfx_cmd_legato(u8(r.ax)); }
BRIDGE(sfx_cmd_tempo_shift) { sfx_cmd_tempo_shift(r.ax); }
BRIDGE(sfx_cmd_tempo) { sfx_cmd_tempo(u8(r.ax)); }
BRIDGE(sfx_cmd_envelope) { sfx_cmd_envelope(u8(r.ax), r.di); }
BRIDGE(sfx_cmd_volume) { sfx_cmd_volume(u8(r.ax), r.di); }
BRIDGE(sfx_cmd_raw_length) { sfx_cmd_raw_length(u8(r.ax)); }
BRIDGE(sfx_raw_pitch) { sfx_raw_pitch(r.ax, r.di); }
BRIDGE(sfx_speaker_init) { sfx_speaker_init(); }
BRIDGE(sfx_silence) { sfx_silence(); }
BRIDGE(sfx_install) { sfx_install(); }
BRIDGE(sfx_remove) { sfx_remove(); }

// ---- music, 1ace and 1af5
BRIDGE(music_play) { music_play(far_arg(a, 0), a[2]); }
BRIDGE(music_silence) { music_silence(); }
BRIDGE(music_resume) { music_resume(); }
BRIDGE(sound_detect) { sound_detect(a[0], far_arg(a, 1), far_arg(a, 3)); }
BRIDGE(music_tick) { music_tick(); }
BRIDGE(mpu_command) { r.ax = mpu_command(a[0]); }
BRIDGE(mpu_reset) { r.ax = mpu_reset(); }
BRIDGE(adlib_load_bin) { r.ax = adlib_load_bin(far_arg(a, 0)); }
BRIDGE(adlib_driver_present) { r.ax = adlib_driver_present(); }
BRIDGE(speaker_note_on) { speaker_note_on(a[0], a[1], a[2]); }
BRIDGE(speaker_note_off) { speaker_note_off(a[0]); }
BRIDGE(speaker_program) { speaker_program(a[0], a[1]); }
BRIDGE(mus_open) { r.ax = mus_open(a[0]); }
BRIDGE(mus_close) { mus_close(a[0]); }
BRIDGE(mus_read) { r.ax = mus_read(a[0], far_arg(a, 1), a[3]); }

// ---- speaker music, 1b37 (the ops: ES:SI in and out)
BRIDGE(speaker_music_reset) { speaker_music_reset(); }
BRIDGE(speaker_voice_start) { speaker_voice_start(a[0], a[1], a[2]); }
BRIDGE(speaker_voice_play) { speaker_voice_play(a[0]); }
BRIDGE(speaker_music_tick) { speaker_music_tick(); }
BRIDGE(spk_op_end) { spk_op_end(); }
#define GB_SPK_OP(name, call)                                                                            \
    BRIDGE(name)                                                                                         \
    {                                                                                                    \
        FarPtr p = es_si(r);                                                                             \
        call;                                                                                            \
        set_es_si(r, p);                                                                                 \
    }
GB_SPK_OP(spk_op_set, spk_op_set(u8(r.ax), r.di, p))
GB_SPK_OP(spk_op_wait, spk_op_wait(r.di, p))
GB_SPK_OP(spk_op_return, spk_op_return(r.di, p))
GB_SPK_OP(spk_op_add, spk_op_add(u8(r.ax), r.di, p))
GB_SPK_OP(spk_op_branch, spk_op_branch(u8(r.ax), r.di, p))
GB_SPK_OP(spk_op_compare, spk_op_compare(u8(r.ax), r.di, p))
GB_SPK_OP(spk_op_poke, spk_op_poke(p))
#undef GB_SPK_OP

// ---- Creative detection, 1b5f
BRIDGE(cms_detect) { r.ax = cms_detect(); }
BRIDGE(cms_opl_wait) { cms_opl_wait(u8(r.ax)); }
BRIDGE(cms_opl_write) { cms_opl_write(r.ax); }
BRIDGE(cms_opl_delay) { cms_opl_delay(); }
BRIDGE(cms_dsp_read)
{
    u8 al = u8(r.ax);
    cms_dsp_read(al);
    r.ax = u16((r.ax & 0xFF00) | al);
}
BRIDGE(cms_dsp_write) { cms_dsp_write(u8(r.ax)); }

// ---- the speaker hardware and the PIT channel 0 log (soundmodel.py)
GB_SOUND_EXPORT void gb_speaker_hw_set(u16 divisor, u8 gate_bits) { spk_hw_set(divisor, gate_bits); }
GB_SOUND_EXPORT void gb_speaker_hw_get(u16 *divisor, u8 *gate_bits) { spk_hw_state(divisor, gate_bits); }
GB_SOUND_EXPORT void gb_timer_clear() { host_stub_timer_clear(); }
GB_SOUND_EXPORT int gb_timer_log(u16 *out, int max)
{
    const auto &log = host_stub_timer_log();
    for (int i = 0; i < int(log.size()) && i < max; i++) out[i] = log[size_t(i)];
    return int(log.size());
}
