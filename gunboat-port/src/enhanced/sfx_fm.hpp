#pragma once
// AdLib sound effects, an optional enhancement (src/enhanced/). The original plays its 13 sound
// effects on the PC speaker. With this option each effect can sound on an FM voice of a second,
// emulated OPL2 chip instead. The effects driver still runs exactly as the original's: every note it
// gives the speaker sounds with the effect's FM instrument, at the same pitch, for as long as the
// speaker would sound it. Only what reaches the ear changes.
//
// The instruments are a bank of patches, one per effect: the driver's 13 and the port's additions. The port ships with the bank in gunboat-port/data/sfx.ini,
// built into the program. The player edits it with the sound-effects editor (tools/sfx_editor), which
// saves sfx.ini in the settings folder next to gunboat.ini (and, run from a build in the source
// tree, data/sfx.ini too, so the next build ships the edits); the game reads the player's file at
// start-up, the built-in bank for what it lacks. This file has no SDL or game dependencies apart from the settings folder, so the
// editor shares it.
#include <string>
#include <vector>

#include "types.hpp"

namespace gb {

constexpr int SFX_PROGRAMS = 13;  // the effect programs of the driver (sound.md §2.1)
// The bank's effects: the driver's 13, then the port's additions for the AdLib, sounded where the
// original has none (sfx_adlib.cpp), each playing the notes of one of the driver's programs.
constexpr int SFX_SOLDIER_KILLED = 13;    // enemy infantry killed: effect 9's falling sweep
constexpr int SFX_IMPACT_METAL = 14;      // a bullet hits a vehicle, boat, gun, helicopter: effect 0's note
constexpr int SFX_IMPACT_WOOD = 15;       // a bullet hits a hut, dock, house, sampan: effect 0's note
constexpr int SFX_IMPACT_BRIDGE = 16;     // a bullet hits a bridge: effect 0's note
constexpr int SFX_GRENADE_LAUNCHER = 17;  // the grenade launcher fires: effect 8's notes (the original's)
constexpr int SFX_MORTAR = 18;            // the mortar fires: effect 8's notes (the original's)
constexpr int SFX_IMPACT_TREE = 19;       // a bullet hits a tree: effect 0's note
constexpr int SFX_IMPACT_STONE = 20;      // a bullet hits a fort, statue, rubble, rock: effect 0's note
constexpr int SFX_IMPACT_SANDBAGS = 21;   // a bullet hits a mortar nest's sandbags: effect 0's note
constexpr int SFX_IMPACT_FLESH = 22;      // a bullet hits a person, a body, an animal: effect 0's note
constexpr int SFX_GRENADE_IMPACT = 23;    // a grenade bursts where it lands: effect 8's notes
constexpr int SFX_MORTAR_IMPACT = 24;     // a mortar shell bursts where it lands: effect 8's notes
constexpr int SFX_COUNT = 25;
// The driver's program (0..12) whose notes effect `id` plays.
int sfx_program_of(int id);
// A patch's volume goes up to 400%: the chip plays every effect SFX_HEADROOM steps (0.75 dB each: 12 dB)
// below its levels at 100%, and its output is mixed at SFX_GAIN, four times the twice it needs (one FM
// voice against the speaker's full square wave). So 100% sounds as the patch's levels, and a louder
// volume takes back some of the headroom: the heard operators' levels only, so the sound keeps its
// envelope and timbre.
constexpr int SFX_HEADROOM = 16;
// The effects driver's tick (sound.md §2.2: the effects timer): 236.7 a second.
constexpr double SFX_TICK_HZ = 1193182.0 / 0x13B1;
constexpr int SFX_MAX_VOLUME = 400;
constexpr int SFX_GAIN = 8;

// Where an effect is heard: on its FM instrument, on the PC speaker as in the original, or not at all.
enum class SfxOutput : u8 { Adlib, Speaker, Silent };

// One OPL2 operator. The ranges are the chip's.
struct SfxOperator {
    u8 attack = 15;   // 0-15, 15 fastest
    u8 decay = 4;     // 0-15, 15 fastest
    u8 sustain = 4;   // 0-15 attenuation of the sustain level (3 dB steps), 0 loudest
    u8 release = 6;   // 0-15, 15 fastest
    u8 multiple = 1;  // 0-15 frequency multiple (0 = 1/2)
    u8 level = 0;     // 0-63 attenuation (0.75 dB steps), 0 loudest
    u8 waveform = 0;  // 0 sine, 1 half sine, 2 absolute sine, 3 quarter sine
    u8 ksl = 0;       // 0-3 key scale level
    bool tremolo = false, vibrato = false;
    bool sustained = true;  // EG type: hold the sustain level while the key is on
    bool ksr = false;       // key scale rate
};

// A point of an effect's pitch bend: `ms` milliseconds after the effect starts, its notes sound `cents`
// higher (lower when negative) than they are (with the transpose).
struct SfxBendPoint {
    int ms = 0;
    int cents = 0;
};
constexpr int SFX_BEND_POINTS = 16;       // at most, per effect
constexpr int SFX_BEND_MAX_MS = 10000;
constexpr int SFX_BEND_MAX_CENTS = 4800;  // four octaves up or down

struct SfxPatch {
    SfxOutput output = SfxOutput::Adlib;
    SfxOperator mod, car;
    u8 feedback = 0;         // 0-7, the modulator's self-modulation (7: noise-like)
    bool additive = false;   // false: the modulator modulates the carrier (FM); true: both are heard
    int transpose = 0;       // semitones, -36..36
    int volume = 100;        // percent, 0-400 (SFX_HEADROOM)
    bool retrigger = false;  // a new pitch during a note attacks again (else it glides on)
    int jitter = 0;          // cents 0-1200: the pitch wobbles randomly on every driver tick (noise)
    // The pitch bend, in time order (none: the notes as they are): between two points the bend moves
    // in a straight line (in cents), before the first and after the last it holds. It goes on while
    // the last note rings out.
    std::vector<SfxBendPoint> bend;
};
// The bend of patch p at `ms` milliseconds after the effect starts, in cents.
double sfx_bend_cents(const SfxPatch &p, double ms);

struct SfxBank {
    SfxPatch fx[SFX_COUNT];
};

const char *sfx_name(int id);         // a short name: "Key click"
const char *sfx_description(int id);  // what plays it in the game
// The defaults: the instruments the port ships with (gunboat-port/data/sfx.ini, built into the
// program), over the port's first instruments for anything that file lacks.
SfxPatch sfx_default_patch(int id);
void sfx_bank_defaults(SfxBank &b);
// Reads a bank (the defaults for what the file lacks); false when there is no file.
bool sfx_bank_load(const std::string &path, SfxBank &b);
bool sfx_bank_save(const std::string &path, const SfxBank &b);
// The shipped bank's file when the program runs from a build in the port's source tree
// (gunboat-port/build): gunboat-port/data/sfx.ini, which the next build builds in; else empty.
std::string sfx_source_bank_path();
// The patch as the lines sfx.ini keeps it ("key = value", one per line); equal patches, equal text.
std::string sfx_patch_text(const SfxPatch &p);
std::string sfx_bank_path();  // sfx.ini in the settings folder

// The effect whose program holds `pc` (the driver's program counter, a DGROUP offset): the one with
// the largest start not above it, from the driver's table of program starts; -1 if pc is before
// them all or far past them.
int sfx_effect_of(u16 pc, const u16 starts[SFX_PROGRAMS]);

// The FM voices of the effects, writing OPL2 registers through `write`. A player is one effects
// driver's speaker (the game runs one driver per effect, sfx_adlib.cpp, so the effects sound
// together); each note it starts takes one of the chip's nine channels, at most three per player
// (its notes rotate over them, so a release rings on while the next two notes play): a free one whose
// release has run longest, else the one sounding longest.
class SfxSynth {
public:
    static constexpr int PLAYERS = SFX_COUNT;
    using Write = void (*)(void *ctx, u8 reg, u8 value);
    SfxSynth(Write write, void *ctx) : write_(write), ctx_(ctx)
    {
        for (int &g : gain_) g = 100;
    }
    void reset();  // the chip's set-up; every voice off
    void set_bank(const SfxBank &b) { bank_ = b; }
    const SfxBank &bank() const { return bank_; }
    // Player `player`, playing effect `id`, sets its speaker: PIT channel 2's divisor (0 = 65536)
    // and whether the gate is open.
    void speaker(int player, int id, u16 divisor, bool on);
    void tick();                // one step of the drivers (236.7 Hz): the pitch jitter
    void silence(int player);   // the player's sounding note is released
    void silence_all();
    bool sounding(int player) const { return players_[player].key_on; }
    // The player's effect starts now: its pitch bend's time begins.
    void begin(int player)
    {
        if (player >= 0 && player < PLAYERS) players_[player].age = 0;
    }
    // The player's next notes at `percent` of the patch's volume (100: as the patch).
    void set_gain(int player, int percent) { gain_[player] = percent; }

private:
    static constexpr int CHANNELS = 9;
    static constexpr int PLAYER_CHANNELS = 3;
    struct Player {
        int id = -1;       // the effect of its sounding (or last) note
        int ch = -1;       // the channel of that note
        bool key_on = false;
        u16 divisor = 0;
        int cents = 0;     // the jitter now
        int age = 0;       // driver ticks since its effect began (the pitch bend's time)
    };
    struct Channel {
        int player = -1;
        bool key_on = false;
        u32 since = 0;     // when it was keyed on or off
    };
    void w(u8 reg, u8 value) { write_(ctx_, reg, value); }
    void program(int ch, const SfxPatch &p, int gain);
    void frequency(int player, bool key);
    int take_channel(int player);
    Write write_;
    void *ctx_;
    SfxBank bank_{};
    Player players_[PLAYERS];
    int gain_[PLAYERS];
    Channel channels_[CHANNELS];
    u32 clock_ = 0;
    u32 rng_ = 0x1990u;
};

} // namespace gb
