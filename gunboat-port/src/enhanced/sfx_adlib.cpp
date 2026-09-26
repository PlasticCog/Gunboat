// The AdLib sound effects in the game (sfx_fm.hpp). The host passes every speaker change to the
// filter below with whether the effects driver's timer handler made it. The filter finds the
// effect from the driver's program counter and plays it the way the bank says: on its FM
// instrument (the second OPL2), on the speaker, or not at all. Game memory is only read.
#include "enhanced/sfx_adlib.hpp"

#include "enhanced/sfx_fm.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

void opl_write(void *, u8 reg, u8 value) { host_sfx_opl_write(reg, value); }

SfxSynth synth(opl_write, nullptr);

bool filter(u16 divisor, bool on, bool effects)
{
    if (!effects) return false;
    u16 starts[SFX_COUNT];
    for (int i = 0; i < SFX_COUNT; i++) starts[i] = ds_u16(u16(DS_sfx_programs + 2 * i));
    const int id = sfx_effect_of(ds_u16(DS_sfx_pc), starts);
    if (id < 0) return false;
    switch (synth.bank().fx[id].output) {
    case SfxOutput::Speaker:
        synth.silence();
        return false;
    case SfxOutput::Silent:
        synth.silence();
        return true;
    default:
        synth.speaker(id, divisor, on);
        return true;
    }
}

// After each timer tick: the jitter, and silence once the driver stops (engine_sound_off removes
// its timer without a note-off of its own handler).
void tick()
{
    if (ds_u8(DS_sfx_timer_on) == 0 || ds_u16(DS_sfx_state) == 0) synth.silence();
    else synth.tick();
}

} // namespace

void sfx_adlib_install()
{
    SfxBank bank;
    sfx_bank_load(sfx_bank_path(), bank);
    synth.set_bank(bank);
    synth.reset();
    host_set_sfx_gain(SFX_GAIN);
    host_set_speaker_filter(filter);
    host_set_tick_observer(tick);
}

} // namespace gb
