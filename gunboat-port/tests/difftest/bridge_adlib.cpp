// Bridge entries: the Ad Lib sound driver ADLIB.COM (sound/adlib_driver.cpp, adlib_driver.md), GB.EXE's
// AdLib back end (1af5), the menu timer (for the music scenarios), and the OPL write log and the
// driver's loader for test_adlib.py / soundmodel.py.
#include "bridge.hpp"
#include "host_stub.hpp"
#include "platform/platform.hpp"
#include "sound/sound.hpp"

#include <string>

#if defined(_WIN32)
#define GB_ADLIB_EXPORT extern "C" __declspec(dllexport)
#else
#define GB_ADLIB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace gb;

namespace {
FarPtr far_arg(const u16 *a, int i) { return {a[i], a[i + 1]}; }
u32 ax_bx(const Regs &r) { return u32(r.ax) << 16 | r.bx; }
u32 cx_dx(const Regs &r) { return u32(r.cx) << 16 | r.dx; }
void set_ax_bx(Regs &r, u32 v)
{
    r.ax = u16(v >> 16);
    r.bx = u16(v);
}
std::string load_error;
} // namespace

// ---- GB.EXE 1af5 (the AdLib back end) and the menu timer
BRIDGE(adlib_note_on) { adlib_note_on(a[0], a[1], a[2]); }
BRIDGE(adlib_note_off) { adlib_note_off(a[0]); }
BRIDGE(adlib_program) { adlib_program(a[0], a[1], u8(r.ax >> 8)); }
BRIDGE(adlib_driver_init) { adlib_driver_init(); }
BRIDGE(adlib_call) { adlib_call(a[0], a[1], a[2]); }
BRIDGE(menu_timer_isr) { menu_timer_isr(); }
BRIDGE(timer_install) { timer_install(); }
BRIDGE(timer_restore) { timer_restore(); }

// ---- ADLIB.COM (names as in adlib_driver.md; the test registers their addresses)
BRIDGE(adlib_install) { adlib_install(); }
BRIDGE(adl_int65_handler) { adl_int65_handler(r.si, {r.bx, r.es}); }
BRIDGE(adl_install_int65) { r.ax = adl_install_int65(); }
BRIDGE(adl_driver_installed) { r.ax = adl_driver_installed(); }
BRIDGE(adl_pit_set_ch0) { adl_pit_set_ch0(r.ax); }
BRIDGE(adl_clock_install) { adl_clock_install(); }
BRIDGE(adl_clock_isr) { adl_clock_isr(); }
BRIDGE(adl_snd_output) { adl_snd_output(a[0], a[1]); }
BRIDGE(adl_init_event_pool) { adl_init_event_pool(); }
BRIDGE(adl_clear_voice_flags) { adl_clear_voice_flags(); }
BRIDGE(adl_init_voice_state) { adl_init_voice_state(); }
BRIDGE(adl_clear_event_ptrs) { adl_clear_event_ptrs(); }
BRIDGE(adl_set_tempo) { adl_set_tempo(a[0]); }
BRIDGE(adl_driver_setup) { adl_driver_setup(a[0], a[1], a[2], a[3]); }
BRIDGE(adl_fn_init) { adl_fn_init(); }
BRIDGE(adl_set_mode) { adl_set_mode(a[0]); }
BRIDGE(adl_set_fn0a_value) { adl_set_fn0a_value(a[0]); }
BRIDGE(adl_seq_tick) { r.ax = adl_seq_tick(a[0], a[1]); }
BRIDGE(adl_event_slot_a) { r.ax = adl_event_slot_a(a[0], a[1]); }
BRIDGE(adl_event_slot_b) { r.ax = adl_event_slot_b(a[0], a[1]); }
BRIDGE(adl_init_event_queue) { adl_init_event_queue(); }
BRIDGE(adl_set_pitch_range) { adl_set_pitch_range(a[0]); }
BRIDGE(adl_set_wave_sel) { adl_set_wave_sel(a[0]); }
BRIDGE(adl_calc_prem_fnum) { set_ax_bx(r, adl_calc_prem_fnum(a[0], a[1])); }
BRIDGE(adl_set_fnum) { adl_set_fnum(a[0], a[1], a[2]); }
BRIDGE(adl_init_fnums) { adl_init_fnums(); }
BRIDGE(adl_init_fnum_ptrs) { adl_init_fnum_ptrs(); }
BRIDGE(adl_init_slot_volume) { adl_init_slot_volume(); }
BRIDGE(adl_set_perc_mode) { adl_set_perc_mode(a[0]); }
BRIDGE(adl_set_slot_prm) { adl_set_slot_prm(a[0], a[1], a[2]); }
BRIDGE(adl_snd_set_prm) { adl_snd_set_prm(a[0], a[1]); }
BRIDGE(adl_snd_set_all_prm) { adl_snd_set_all_prm(a[0]); }
BRIDGE(adl_snd_s_ksl_level) { adl_snd_s_ksl_level(a[0]); }
BRIDGE(adl_snd_s_note_sel) { adl_snd_s_note_sel(); }
BRIDGE(adl_snd_s_feed_fm) { adl_snd_s_feed_fm(a[0]); }
BRIDGE(adl_snd_s_att_decay) { adl_snd_s_att_decay(a[0]); }
BRIDGE(adl_snd_s_sus_release) { adl_snd_s_sus_release(a[0]); }
BRIDGE(adl_snd_s_avek) { adl_snd_s_avek(a[0]); }
BRIDGE(adl_snd_s_am_vib_rhythm) { adl_snd_s_am_vib_rhythm(); }
BRIDGE(adl_snd_wave_select) { adl_snd_wave_select(a[0]); }
BRIDGE(adl_note_on) { adl_note_on(a[0], a[1]); }
BRIDGE(adl_note_off) { adl_note_off(a[0]); }
BRIDGE(adl_set_freq) { adl_set_freq(a[0], a[1], a[2]); }
BRIDGE(adl_sound_chut) { adl_sound_chut(a[0]); }
BRIDGE(adl_sound_cold_init) { adl_sound_cold_init(a[0], a[1]); }
BRIDGE(adl_sound_warm_init) { adl_sound_warm_init(); }
BRIDGE(adl_init_slot_params) { adl_init_slot_params(); }
BRIDGE(adl_set_gparam) { adl_set_gparam(far_arg(a, 0)); }
BRIDGE(adl_set_voice_timbre) { adl_set_voice_timbre(a[0], far_arg(a, 1)); }
BRIDGE(adl_set_slot_param) { adl_set_slot_param(a[0], far_arg(a, 1), a[3]); }
BRIDGE(adl_ldiv)
{
    const AdlLdiv d = adl_ldiv(ax_bx(r), cx_dx(r));
    set_ax_bx(r, d.quot);
    r.cx = u16(d.rem >> 16);
    r.dx = u16(d.rem);
}
BRIDGE(adl_lmul) { set_ax_bx(r, adl_lmul(ax_bx(r), cx_dx(r))); }
BRIDGE(adl_inp) { r.ax = adl_inp(a[0]); }

// DOS loads ADLIB.COM into the port's mem[] (adlib_load). Returns 1, or 0 with gb_adlib_error().
GB_ADLIB_EXPORT int gb_adlib_load(const char *path) { return adlib_load(path, load_error) ? 1 : 0; }
GB_ADLIB_EXPORT const char *gb_adlib_error() { return load_error.c_str(); }

// The OPL2 writes (host_opl_write) since the last clear: n pairs (register, value) into out[2n].
GB_ADLIB_EXPORT void gb_opl_clear() { host_stub_opl_clear(); }
GB_ADLIB_EXPORT int gb_opl_log(u8 *out, int max)
{
    const auto &log = host_stub_opl_log();
    for (int i = 0; i < int(log.size()) && i < max; i++) {
        out[2 * i] = log[size_t(i)].reg;
        out[2 * i + 1] = log[size_t(i)].value;
    }
    return int(log.size());
}
