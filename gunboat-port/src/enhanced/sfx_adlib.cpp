// The AdLib sound effects in the game (sfx_fm.hpp).
//
// The original's effects driver plays one effect at a time: each sfx_play replaces the effect that
// is playing (on the PC speaker, one sound is all there is), so a machine gun firing cuts the engine,
// the explosions and the hits. On the AdLib the effects sound together: every effect that starts
// (sfx_play tells the host, host_sfx_play) gets its own copy of the driver, the ported driver itself
// (sfx_timer_tick) run on a copy of the driver's state (DS:DA48-DB1D) with its own speaker hardware,
// one step per timer tick like the game's; its speaker changes play on that effect's FM instrument
// (a player of the synthesizer). A new start of the same effect restarts only its copy; the engine's
// keeps looping while it runs.
//
// The game's own driver runs as always, so the game's memory is the original's. Its speaker changes
// are silenced, except for the effects the bank leaves on the speaker. The copies' steps run on the
// game's memory in the host's tick observer and put every byte of it back before the game continues.
#include "enhanced/sfx_adlib.hpp"

#include "enhanced/sfx_fm.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 STATE = DS_sfx_loop_count;  // DA48: the driver's state, up to the program table
constexpr int STATE_SIZE = DS_sfx_programs - DS_sfx_loop_count;
constexpr int ENGINE = 6;  // the engine's program loops (sound.md §2.1)

struct Driver {
    bool active = false;
    u8 state[STATE_SIZE];
    u16 divisor = 0;  // its speaker: PIT channel 2's divisor and the port 61h gate bits
    u8 gate = 0;
};

void opl_write(void *, u8 reg, u8 value) { host_sfx_opl_write(reg, value); }

SfxSynth synth(opl_write, nullptr);
Driver drivers[SFX_COUNT];  // one per effect
int running = -1;           // the driver whose step runs now

int effect_of(u16 pc)
{
    u16 starts[SFX_COUNT];
    for (int i = 0; i < SFX_COUNT; i++) starts[i] = ds_u16(u16(DS_sfx_programs + 2 * i));
    return sfx_effect_of(pc, starts);
}

void stop_all()
{
    for (Driver &d : drivers) d.active = false;
    synth.silence_all();
}

// sfx_play has started the program at `program`: the effect's own driver starts over from the
// game's driver as sfx_play left it (the effect requested), its speaker off.
void on_sfx_play(u16 program)
{
    if (ds_u16(DS_sound_muted) != 0) return;
    const int id = effect_of(program);
    if (id < 0 || synth.bank().fx[id].output != SfxOutput::Adlib) return;
    Driver &d = drivers[id];
    if (id == ENGINE && d.active) return;
    synth.silence(id);
    for (int i = 0; i < STATE_SIZE; i++) d.state[i] = ds_u8(u16(STATE + i));
    u8 gate;
    spk_hw_state(&d.divisor, &gate);
    d.gate = 0;
    d.active = true;
}

// Every speaker change. A driver copy's: its effect's instrument plays it. The game's driver's: the
// speaker plays the effects the bank leaves on it; for the others (their copies play them, or they
// are silent) the speaker goes quiet, as it does when the one driver moves on to another effect.
bool filter(u16 divisor, bool &on, bool effects)
{
    if (running >= 0) {
        synth.speaker(running, running, divisor, on);
        return true;
    }
    if (!effects) return false;
    const int id = effect_of(ds_u16(DS_sfx_pc));
    if (id < 0 || synth.bank().fx[id].output == SfxOutput::Speaker) return false;
    on = false;
    return false;
}

// After each timer tick (the game's driver has made its step): one step of every effect's driver.
void tick()
{
    if (ds_u8(DS_sfx_timer_on) == 0 || ds_u16(DS_sound_muted) != 0) {
        stop_all();
        return;
    }
    bool any = false;
    for (const Driver &d : drivers) any |= d.active;
    if (any) {
        u8 game[STATE_SIZE];
        for (int i = 0; i < STATE_SIZE; i++) game[i] = ds_u8(u16(STATE + i));
        u16 game_divisor;
        u8 game_gate;
        spk_hw_state(&game_divisor, &game_gate);
        for (int k = 0; k < SFX_COUNT; k++) {
            Driver &d = drivers[k];
            if (!d.active) continue;
            for (int i = 0; i < STATE_SIZE; i++) ds_u8(u16(STATE + i)) = d.state[i];
            spk_hw_set(d.divisor, d.gate);
            running = k;
            sfx_timer_tick();
            running = -1;
            for (int i = 0; i < STATE_SIZE; i++) d.state[i] = ds_u8(u16(STATE + i));
            spk_hw_state(&d.divisor, &d.gate);
            if (ds_u16(DS_sfx_state) == 0) {  // the effect has ended
                d.active = false;
                synth.silence(k);
            }
        }
        for (int i = 0; i < STATE_SIZE; i++) ds_u8(u16(STATE + i)) = game[i];
        spk_hw_set(game_divisor, game_gate);
    }
    synth.tick();
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
    host_set_sfx_play_observer(on_sfx_play);
}

} // namespace gb
