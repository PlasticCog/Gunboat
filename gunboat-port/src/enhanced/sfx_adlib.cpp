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
// Additions, where the original is silent (the game tells the host of each hit and each destroyed
// target, host_object_hit / host_target_destroyed): a target destroyed by a shot sounds the Explosion
// effect (8; the original plays it only for a class 7 object's wreck 4Bh), enemy infantry killed the
// Soldier killed effect, and a bullet's hit the Impact effect of the object's material (materials.hpp).
// Each is its own driver copy, started as sfx_play would start the program whose notes it plays. And
// where the original reuses a sound: the grenade launcher and the mortar fire with the explosion's
// effect 8 (the game tells the host first, host_shell_fired); on the AdLib they play its notes on
// their own instruments; and where they land they burst with the Grenade impact and Mortar impact
// effects (the explosion's notes; the original is silent there: host_shot_landed).
//
// The game's own driver runs as always, so the game's memory is the original's. Its speaker changes
// are silenced, except for the effects the bank leaves on the speaker. The copies' steps run on the
// game's memory in the host's tick observer and put every byte of it back before the game continues.
#include "enhanced/sfx_adlib.hpp"

#include "enhanced/materials.hpp"
#include "enhanced/sfx_fm.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 STATE = DS_sfx_loop_count;  // DA48: the driver's state, up to the program table
constexpr int STATE_SIZE = DS_sfx_programs - DS_sfx_loop_count;
constexpr int ENGINE = 6;     // the engine's program loops (sound.md §2.1)
constexpr int EXPLOSION = 8;  // explosions; in the original also the mortar and the grenade launcher
constexpr u8 WRECK_WITH_SOUND = 0x4B;  // the wreck whose destruction plays effect 8 in the original

// Object kinds (world.md §6.3). Enemy infantry: 0Ah-0Ch, and 14h in Vietnam (region 0) and the
// practice world (3) (elsewhere a truck or a missile launcher). People: those, the civilians 19h-1Ah
// and 24h-26h (ex-POW, SEALs, US infantry).
bool enemy_infantry(u8 kind)
{
    const u16 region = ds_u16(DS_region);
    return (kind >= 0x0A && kind <= 0x0C) || (kind == 0x14 && (region == 0 || region == 3));
}
bool person(u8 kind) { return enemy_infantry(kind) || kind == 0x19 || kind == 0x1A || (kind >= 0x24 && kind <= 0x26); }
// The dead: the bodies 33h-35h and, in Vietnam, the dead beast 32h (rubble or a statue elsewhere).
bool dead(u8 kind) { return (kind >= 0x33 && kind <= 0x35) || (kind == 0x32 && ds_u16(DS_region) == 0); }
// Dead, destroyed or inanimate: the dead, the wrecks (the downed helicopter 18h, 30h-38h) and the
// scenery (buoys, trees, stumps, statues, rocks: 28h and above).
bool lifeless(u8 kind) { return kind == 0x18 || kind >= 0x28; }

// The impact effect of each material.
int impact_effect(Material m)
{
    switch (m) {
    case Material::Metal: return SFX_IMPACT_METAL;
    case Material::Wood: return SFX_IMPACT_WOOD;
    case Material::Tree: return SFX_IMPACT_TREE;
    case Material::Bridge: return SFX_IMPACT_BRIDGE;
    case Material::Sand: return SFX_IMPACT_SANDBAGS;
    case Material::Flesh: return SFX_IMPACT_FLESH;
    default: return SFX_IMPACT_STONE;
    }
}

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
int launch = -1;            // a shell fired: its effect, which the explosion effect starting next is
int game_launch = -1;       // the launch effect the game's own driver plays (its program is effect 8's)
int last_impact = -1;       // the impact effect the shot being tested started (hit_objects)

int effect_of(u16 pc)
{
    u16 starts[SFX_PROGRAMS];
    for (int i = 0; i < SFX_PROGRAMS; i++) starts[i] = ds_u16(u16(DS_sfx_programs + 2 * i));
    return sfx_effect_of(pc, starts);
}

u16 program_of(int id) { return ds_u16(u16(DS_sfx_programs + 2 * sfx_program_of(id))); }

void stop_all()
{
    for (Driver &d : drivers) d.active = false;
    synth.silence_all();
}

// Effect `id` (program `program`) starts on its own driver: the game's driver's state with that
// program requested, as sfx_play leaves it (voice 0's program, state 1), its speaker off; its notes
// at `gain` percent of the instrument's volume.
void start(int id, u16 program, int gain = 100)
{
    if (ds_u16(DS_sound_muted) != 0 || ds_u8(DS_sfx_timer_on) == 0) return;
    if (id < 0 || synth.bank().fx[id].output != SfxOutput::Adlib) return;
    Driver &d = drivers[id];
    if (id == ENGINE && d.active) return;
    synth.silence(id);
    synth.set_gain(id, gain);
    for (int i = 0; i < STATE_SIZE; i++) d.state[i] = ds_u8(u16(STATE + i));
    auto put16 = [&](u16 at, u16 v) {
        d.state[at - STATE] = u8(v);
        d.state[at - STATE + 1] = u8(v >> 8);
    };
    put16(DS_sfx_program, program);
    put16(DS_sfx_state, 1);
    u8 gate;
    spk_hw_state(&d.divisor, &gate);
    d.gate = 0;
    d.active = true;
}

void stop(int id)
{
    drivers[id].active = false;
    synth.silence(id);
}

// sfx_play has started the program at `program`: its effect, or the launch effect a shell fired just
// before it is (the explosion's notes on the launcher's instrument).
void on_sfx_play(u16 program)
{
    int id = effect_of(program);
    game_launch = -1;
    if (launch >= 0 && id == EXPLOSION) id = game_launch = launch;
    launch = -1;
    start(id, program);
}

// A shot lands: a grenade (weapon 2) or a mortar shell (3) bursts.
void on_shot_landed(u16, u16, u8 weapon)
{
    if (weapon == 2) start(SFX_GRENADE_IMPACT, program_of(SFX_GRENADE_IMPACT));
    else if (weapon == 3) start(SFX_MORTAR_IMPACT, program_of(SFX_MORTAR_IMPACT));
}

// The grenade launcher (weapon 2) or the mortar (3) fires: its sound starts next.
void on_shell_fired(u8 weapon) { launch = weapon == 2 ? SFX_GRENADE_LAUNCHER : SFX_MORTAR; }

// A shot has destroyed a target of kind `old`: enemy infantry cry out; other people, buoys, trees and
// the like make no sound; the rest explodes, unless the original plays the explosion itself.
void on_target_destroyed(u8 old, u8 wreck)
{
    const int impact = last_impact;
    last_impact = -1;
    if (enemy_infantry(old)) {
        // the soldier's cry, not the bullet's thwack as well (it has not sounded yet: no tick since)
        if (impact == SFX_IMPACT_FLESH) stop(SFX_IMPACT_FLESH);
        start(SFX_SOLDIER_KILLED, program_of(SFX_SOLDIER_KILLED));
    } else if (person(old) || old >= 0x28 || wreck == WRECK_WITH_SOUND) {
        return;
    } else {
        start(EXPLOSION, program_of(EXPLOSION));
    }
}

// A shot has hit an object of kind `kind`: a bullet's impact on its material (materials.hpp: metal,
// wood, a tree, a bridge, stone, sandbags, flesh), at three quarters of the volume on what is dead,
// destroyed or inanimate. Not for the grenades and the mortar (their own explosion sounds), not on the
// wrecks the shot passes through.
void on_object_hit(u8 kind, u16)
{
    last_impact = -1;
    const u8 weapon = ds_u8(DS_vec_product_hi);  // hit_objects' weapon: 2 and 3 are the explosive ones
    if (weapon == 2 || weapon == 3 || kind == 0x30 || kind == 0x31) return;
    const int id = impact_effect(material_of(kind));
    start(id, program_of(id), dead(kind) || lifeless(kind) ? 75 : 100);
    last_impact = id;
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
    int id = effect_of(ds_u16(DS_sfx_pc));
    if (id == EXPLOSION && game_launch >= 0) id = game_launch;  // a launch: its own effect's output
    if (id < 0 || synth.bank().fx[id].output == SfxOutput::Speaker) return false;
    on = false;
    return false;
}

// After each timer tick (the game's driver has made its step): one step of every effect's driver.
void tick()
{
    launch = -1;  // a shell fired and its sound come together (no tick between): none left over
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
    host_add_sfx_play_observer(on_sfx_play);
    host_add_target_destroyed_observer(on_target_destroyed);
    host_add_object_hit_observer(on_object_hit);
    host_add_shell_fired_observer(on_shell_fired);
    host_add_shot_landed_observer(on_shot_landed);
}

} // namespace gb
