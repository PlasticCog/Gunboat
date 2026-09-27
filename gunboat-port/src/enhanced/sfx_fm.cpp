// AdLib sound effects: the bank of FM instruments and the synthesizer (sfx_fm.hpp).
#include "enhanced/sfx_fm.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace gb {

namespace {

struct Info {
    const char *name, *description;
};

// The effects of the driver's program table (sound.md §2.1) and what plays them in the game.
const Info INFO[SFX_COUNT] = {
    {"Key click", "A key press (the key handler stops the current effect with effect 12 first)"},
    {"Twin .50s", "The bow twin .50 calibre guns and the midship .50 (weapon 4)"},
    {"M60", "The M60 machine guns (weapon 1)"},
    {"Minigun", "The bow minigun (weapon 5): a burst of four shots"},
    {"Objective", "Mission accomplished, an objective reached"},
    {"Ramming", "The boat rams something"},
    {"Engine", "The engine, looping while it runs; the throttles set its pitch and speed"},
    {"Heavy hit", "A heavy hit, a boat destroyed, a missile hit"},
    {"Explosion", "The mortar, the grenade launcher, explosions"},
    {"Light hit", "A light hit"},
    {"Incoming", "Incoming fire"},
    {"Incoming 2", "Incoming fire, the longer call"},
    {"Silence", "Plays nothing: the key handler uses it to stop the current effect"},
    {"Soldier killed", "Added for the AdLib (the original is silent): enemy infantry killed. Plays the notes of the light hit (a falling sweep)"},
    {"Impact: metal", "Added for the AdLib: a bullet hits a vehicle, a boat, a gun, a helicopter, a mine or a buoy. Plays the key click's note"},
    {"Impact: wood", "Added for the AdLib: a bullet hits a hut, a dock, a fort, a bridge, a tree or a rock. Plays the key click's note"},
};

SfxOperator op(u8 attack, u8 decay, u8 sustain, u8 release, u8 multiple, u8 level, u8 waveform = 0,
               bool sustained = true)
{
    SfxOperator o;
    o.attack = attack;
    o.decay = decay;
    o.sustain = sustain;
    o.release = release;
    o.multiple = multiple;
    o.level = level;
    o.waveform = waveform;
    o.sustained = sustained;
    return o;
}

const char *output_name(SfxOutput o)
{
    switch (o) {
    case SfxOutput::Speaker: return "speaker";
    case SfxOutput::Silent: return "silent";
    default: return "adlib";
    }
}

std::string op_text(const SfxOperator &o)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "%u %u %u %u %u %u %u %u %d %d %d %d", o.attack, o.decay, o.sustain,
                  o.release, o.multiple, o.level, o.waveform, o.ksl, o.tremolo, o.vibrato, o.sustained, o.ksr);
    return buf;
}

u8 clamp_u8(int v, int hi) { return u8(v < 0 ? 0 : v > hi ? hi : v); }

void op_parse(const std::string &v, SfxOperator &o)
{
    std::istringstream in(v);
    int f[12];
    for (int &x : f)
        if (!(in >> x)) return;
    o.attack = clamp_u8(f[0], 15);
    o.decay = clamp_u8(f[1], 15);
    o.sustain = clamp_u8(f[2], 15);
    o.release = clamp_u8(f[3], 15);
    o.multiple = clamp_u8(f[4], 15);
    o.level = clamp_u8(f[5], 63);
    o.waveform = clamp_u8(f[6], 3);
    o.ksl = clamp_u8(f[7], 3);
    o.tremolo = f[8] != 0;
    o.vibrato = f[9] != 0;
    o.sustained = f[10] != 0;
    o.ksr = f[11] != 0;
}

std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// The instruments the port ships with: gunboat-port/data/sfx.ini, built in (CMake writes it into
// sfx_bank_default.inc as a string; the editor saves that file when it runs from the source tree).
#if __has_include("sfx_bank_default.inc")
const char *const SHIPPED_BANK =
#include "sfx_bank_default.inc"
    ;
#else
const char *const SHIPPED_BANK = "";
#endif

// A bank's text over b: the effects and keys it has replace b's.
void bank_parse(std::istream &in, SfxBank &b)
{
    std::string line;
    int cur = -1;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            cur = std::atoi(line.c_str() + 1);
            if (cur < 0 || cur >= SFX_COUNT) cur = -1;
            continue;
        }
        const size_t eq = line.find('=');
        if (cur < 0 || eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        SfxPatch &p = b.fx[cur];
        const int n = std::atoi(v.c_str());
        if (k == "output")
            p.output = v == "speaker" ? SfxOutput::Speaker : v == "silent" ? SfxOutput::Silent : SfxOutput::Adlib;
        else if (k == "modulator")
            op_parse(v, p.mod);
        else if (k == "carrier")
            op_parse(v, p.car);
        else if (k == "feedback")
            p.feedback = clamp_u8(n, 7);
        else if (k == "connection")
            p.additive = v == "additive";
        else if (k == "transpose")
            p.transpose = n < -36 ? -36 : n > 36 ? 36 : n;
        else if (k == "volume")
            p.volume = n < 0 ? 0 : n > 100 ? 100 : n;
        else if (k == "retrigger")
            p.retrigger = n != 0;
        else if (k == "jitter")
            p.jitter = n < 0 ? 0 : n > 1200 ? 1200 : n;
    }
}

} // namespace

// The first instruments of the port, for what the shipped bank lacks.
static SfxPatch factory_patch(int id);

const char *sfx_name(int id) { return id >= 0 && id < SFX_COUNT ? INFO[id].name : "?"; }

int sfx_program_of(int id)
{
    switch (id) {
    case SFX_SOLDIER_KILLED: return 9;
    case SFX_IMPACT_METAL:
    case SFX_IMPACT_WOOD: return 0;
    default: return id >= 0 && id < SFX_PROGRAMS ? id : 0;
    }
}
const char *sfx_description(int id) { return id >= 0 && id < SFX_COUNT ? INFO[id].description : ""; }

// The first instruments of the port: each program's notes (sound.md §2.1) played by a voice that suits
// what the effect is. Guns and explosions are very short low notes on the speaker: they get a noisy
// modulator (feedback 7, jitter) and a release that rings on after the note. The shipped bank
// (data/sfx.ini) replaces them.
SfxPatch factory_patch(int id)
{
    SfxPatch p;
    switch (id) {
    case 0:  // key click: one 2093 Hz note of two ticks
        p.mod = op(15, 10, 15, 15, 3, 34);
        p.car = op(15, 11, 15, 12, 1, 0, 0, false);
        p.feedback = 4;
        p.volume = 90;
        break;
    case 1:  // twin .50s: a 165 Hz blip of two ticks
        p.mod = op(15, 5, 6, 6, 5, 4);
        p.car = op(15, 6, 10, 7, 1, 0, 0, false);
        p.feedback = 7;
        p.jitter = 300;
        break;
    case 2:  // M60: a 175 Hz blip, lighter
        p.mod = op(15, 6, 6, 7, 7, 6);
        p.car = op(15, 7, 11, 8, 1, 4, 0, false);
        p.feedback = 7;
        p.jitter = 250;
        p.volume = 85;
        break;
    case 3:  // minigun: four blips, 25 ms apart
        p.mod = op(15, 7, 6, 9, 6, 6);
        p.car = op(15, 8, 11, 10, 1, 2, 0, false);
        p.feedback = 7;
        p.jitter = 250;
        break;
    case 4:  // objective: an arpeggio played four times: a bright brass
        p.mod = op(12, 3, 4, 6, 1, 26);
        p.car = op(14, 3, 3, 6, 1, 0);
        p.feedback = 5;
        p.volume = 80;
        break;
    case 5:  // ramming: a rising grind from 33 Hz
        p.mod = op(15, 4, 4, 6, 2, 8);
        p.car = op(15, 3, 3, 6, 1, 0);
        p.feedback = 6;
        p.jitter = 150;
        break;
    case 6:  // engine: low notes changing every two ticks, legato: a rumble
        p.mod = op(15, 2, 2, 8, 1, 18);
        p.car = op(15, 2, 2, 8, 1, 2);
        p.feedback = 6;
        p.jitter = 40;
        p.volume = 60;
        break;
    case 7:  // heavy hit: three low notes alternating six times
        p.mod = op(15, 3, 4, 4, 1, 0);
        p.car = op(15, 3, 3, 4, 1, 0);
        p.feedback = 7;
        p.jitter = 600;
        break;
    case 8:  // explosion: a falling rumble
        p.mod = op(15, 3, 4, 3, 1, 0);
        p.car = op(15, 3, 3, 3, 1, 0);
        p.feedback = 7;
        p.jitter = 500;
        break;
    case 9:  // light hit: a fast falling sweep: a metallic clank
        p.mod = op(15, 5, 6, 6, 4, 20);
        p.car = op(15, 5, 8, 6, 1, 0);
        p.feedback = 2;
        p.retrigger = true;
        break;
    case 10:  // incoming: short calls: a clean whistle
    case 11:
        p.mod = op(15, 15, 15, 15, 1, 63);
        p.car = op(15, 2, 0, 8, 1, 4);
        p.volume = 80;
        break;
    case SFX_SOLDIER_KILLED:  // a short falling cry, lower than the clank's notes
        p.mod = op(14, 3, 3, 5, 1, 22);
        p.car = op(13, 3, 2, 5, 1, 0);
        p.car.vibrato = true;
        p.feedback = 3;
        p.transpose = -19;
        p.volume = 85;
        break;
    case SFX_IMPACT_METAL:  // plink: an inharmonic ping ringing on after the two-tick note
        p.mod = op(15, 5, 15, 7, 7, 22);
        p.car = op(15, 6, 15, 7, 1, 0, 0, false);
        p.feedback = 2;
        p.volume = 70;
        break;
    case SFX_IMPACT_WOOD:  // a dull, noisy knock
        p.mod = op(15, 8, 15, 9, 1, 10);
        p.car = op(15, 8, 15, 9, 1, 0, 0, false);
        p.feedback = 6;
        p.transpose = -24;
        p.jitter = 200;
        p.volume = 75;
        break;
    default:  // 12: silence
        p.mod = op(15, 4, 4, 6, 1, 20);
        p.car = op(15, 4, 4, 6, 1, 0);
        p.output = SfxOutput::Silent;
        break;
    }
    return p;
}

void sfx_bank_defaults(SfxBank &b)
{
    for (int i = 0; i < SFX_COUNT; i++) b.fx[i] = factory_patch(i);
    std::istringstream in(SHIPPED_BANK);
    bank_parse(in, b);
}

SfxPatch sfx_default_patch(int id)
{
    SfxBank b;
    sfx_bank_defaults(b);
    return b.fx[id >= 0 && id < SFX_COUNT ? id : 0];
}

std::string sfx_patch_text(const SfxPatch &p)
{
    std::string s;
    s += std::string("output = ") + output_name(p.output) + "\n";
    s += "modulator = " + op_text(p.mod) + "\n";
    s += "carrier = " + op_text(p.car) + "\n";
    s += "feedback = " + std::to_string(p.feedback) + "\n";
    s += std::string("connection = ") + (p.additive ? "additive" : "fm") + "\n";
    s += "transpose = " + std::to_string(p.transpose) + "\n";
    s += "volume = " + std::to_string(p.volume) + "\n";
    s += "retrigger = " + std::to_string(int(p.retrigger)) + "\n";
    s += "jitter = " + std::to_string(p.jitter) + "\n";
    return s;
}

bool sfx_bank_load(const std::string &path, SfxBank &b)
{
    sfx_bank_defaults(b);
    std::ifstream in(path);
    if (!in) return false;
    bank_parse(in, b);
    return true;
}

bool sfx_bank_save(const std::string &path, const SfxBank &b)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "# Gunboat: the AdLib sound effects (edit with gunboat_sfx_editor).\n"
           "# An operator is: attack decay sustain release multiple level waveform ksl tremolo vibrato\n"
           "# sustained ksr (the OPL2's fields). output = adlib | speaker | silent.\n";
    for (int i = 0; i < SFX_COUNT; i++) out << "\n[" << i << "]  # " << sfx_name(i) << "\n" << sfx_patch_text(b.fx[i]);
    return bool(out);
}

std::string sfx_bank_path()
{
    char *dir = SDL_GetPrefPath("", "Gunboat");
    std::string p = dir ? std::string(dir) + "sfx.ini" : std::string("sfx.ini");
    SDL_free(dir);
    return p;
}

std::string sfx_source_bank_path()
{
    namespace fs = std::filesystem;
    const char *base = SDL_GetBasePath();  // the program's folder: gunboat-port/build/ in the tree
    if (!base) return {};
    std::error_code ec;
    const fs::path port = fs::path(base).parent_path().parent_path();
    if (!fs::exists(port / "src" / "enhanced" / "sfx_fm.hpp", ec) || !fs::exists(port / "CMakeLists.txt", ec))
        return {};
    return (port / "data" / "sfx.ini").string();
}

int sfx_effect_of(u16 pc, const u16 starts[SFX_PROGRAMS])
{
    int best = -1;
    for (int i = 0; i < SFX_PROGRAMS; i++)
        if (starts[i] <= pc && (best < 0 || starts[i] > starts[best])) best = i;
    if (best >= 0 && pc - starts[best] > 0x100) best = -1;
    return best;
}

// ---- the synthesizer

void SfxSynth::reset()
{
    w(0x01, 0x20);  // waveform select on
    w(0x08, 0x00);
    w(0xBD, 0x00);
    for (int ch = 0; ch < 9; ch++) w(u8(0xB0 + ch), 0);
    for (u8 o = 0; o < 0x16; o++) w(u8(0x40 + o), 0x3F);
    for (Player &p : players_) p = Player();
    for (Channel &c : channels_) c = Channel();
}

void SfxSynth::program(int ch, const SfxPatch &p, int gain)
{
    static const u8 SLOT[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
    // The heard operators are attenuated by the patch's volume (in the chip's 0.75 dB steps).
    const double volume = p.volume * gain / 100.0;
    const int extra = volume >= 100 ? 0 : volume <= 0 ? 63 : int(std::lround(-20.0 * std::log10(volume / 100.0) / 0.75));
    auto set = [&](u8 o, const SfxOperator &x, bool heard) {
        w(u8(0x20 + o), u8(x.tremolo << 7 | x.vibrato << 6 | x.sustained << 5 | x.ksr << 4 | (x.multiple & 15)));
        w(u8(0x40 + o), u8((x.ksl & 3) << 6 | clamp_u8(x.level + (heard ? extra : 0), 63)));
        w(u8(0x60 + o), u8((x.attack & 15) << 4 | (x.decay & 15)));
        w(u8(0x80 + o), u8((x.sustain & 15) << 4 | (x.release & 15)));
        w(u8(0xE0 + o), u8(x.waveform & 3));
    };
    set(SLOT[ch], p.mod, p.additive);
    set(u8(SLOT[ch] + 3), p.car, true);
    w(u8(0xC0 + ch), u8((p.feedback & 7) << 1 | (p.additive ? 1 : 0)));
}

// The player's channel's pitch: the speaker's frequency 1193182 / divisor, transposed and jittered.
void SfxSynth::frequency(int player, bool key)
{
    const Player &pl = players_[player];
    const int ch = pl.ch;
    if (ch < 0) return;
    const SfxPatch &p = bank_.fx[pl.id < 0 ? 0 : pl.id];
    double f = 1193182.0 / (pl.divisor ? pl.divisor : 65536);
    f *= std::pow(2.0, (p.transpose * 100 + pl.cents) / 1200.0);
    int block = 0;
    double fnum = f * double(1 << 20) / 49716.0;
    while (fnum > 1023.0 && block < 7) {
        fnum /= 2;
        block++;
    }
    const int n = fnum > 1023.0 ? 1023 : fnum < 1.0 ? 1 : int(std::lround(fnum));
    w(u8(0xA0 + ch), u8(n));
    w(u8(0xB0 + ch), u8((key ? 0x20 : 0) | block << 2 | (n >> 8)));
}

// A channel for a new note of `player`. A player keeps at most PLAYER_CHANNELS: with that many its
// own oldest one is used again (each effect sounds as with the three channels of one driver: a
// note's release rings on while the next two play). Else a free one released longest ago, else (all
// sounding) the one keyed on longest ago, whose player's note ends.
int SfxSynth::take_channel(int player)
{
    int best = -1, own = 0;
    for (int c = 0; c < CHANNELS; c++)
        if (channels_[c].player == player) {
            own++;
            if (best < 0 || channels_[c].since < channels_[best].since) best = c;
        }
    if (own < PLAYER_CHANNELS) {
        best = -1;
        for (int c = 0; c < CHANNELS; c++)
            if (!channels_[c].key_on && (best < 0 || channels_[c].since < channels_[best].since)) best = c;
    }
    if (best < 0) {
        for (int c = 0; c < CHANNELS; c++)
            if (best < 0 || channels_[c].since < channels_[best].since) best = c;
        silence(channels_[best].player);
    }
    channels_[best].player = player;
    channels_[best].key_on = true;
    channels_[best].since = ++clock_;
    return best;
}

void SfxSynth::speaker(int player, int id, u16 divisor, bool on)
{
    if (player < 0 || player >= PLAYERS || id < 0 || id >= SFX_COUNT) return;
    Player &pl = players_[player];
    if (id != pl.id) {
        silence(player);
        pl.id = id;
    }
    const SfxPatch &p = bank_.fx[id];
    if (!on) {
        pl.divisor = divisor;
        silence(player);
        return;
    }
    if (pl.key_on && !p.retrigger) {  // a new pitch during the note: it glides on
        pl.divisor = divisor;
        frequency(player, true);
        return;
    }
    silence(player);
    pl.ch = take_channel(player);
    program(pl.ch, p, gain_[player]);
    pl.divisor = divisor;
    pl.cents = 0;
    frequency(player, true);
    pl.key_on = true;
}

void SfxSynth::tick()
{
    for (int k = 0; k < PLAYERS; k++) {
        Player &pl = players_[k];
        if (!pl.key_on || pl.id < 0) continue;
        const int j = bank_.fx[pl.id].jitter;
        if (j <= 0) continue;
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        pl.cents = int(rng_ % u32(2 * j + 1)) - j;
        frequency(k, true);
    }
}

void SfxSynth::silence(int player)
{
    if (player < 0 || player >= PLAYERS) return;
    Player &pl = players_[player];
    if (!pl.key_on) return;
    frequency(player, false);
    pl.key_on = false;
    if (pl.ch >= 0 && channels_[pl.ch].player == player) {
        channels_[pl.ch].key_on = false;
        channels_[pl.ch].since = ++clock_;
    }
}

void SfxSynth::silence_all()
{
    for (int k = 0; k < PLAYERS; k++) silence(k);
}

} // namespace gb
