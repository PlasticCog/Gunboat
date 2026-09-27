// gunboat_sfx_editor: edits the AdLib sound effects of the Gunboat port, effect by effect
// (src/enhanced/sfx_fm.hpp).
//
// It runs the original's effects driver (the port's, from the core library) on GB.EXE's own effect
// programs, which gives each effect's notes exactly as the game plays them. Those notes can then be
// heard on the PC speaker, as in the original, or on the FM instrument being edited, through the same
// synthesizer the game uses. Save writes sfx.ini to the settings folder; the game reads it when its
// "Sound effects" setting is AdLib (launcher, or --effects adlib).
//
// C++, SDL3 and Dear ImGui (vendor/imgui, MIT); the OPL2 is Nuked-OPL3 (vendor/nuked-opl3).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "enhanced/icon_image.hpp"
#include "enhanced/settings.hpp"
#include "enhanced/sfx_fm.hpp"
#include "host_stub.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "mem.hpp"
#include "opl3.h"
#include "sound/sound.hpp"
#include "symbols.hpp"

using namespace gb;

namespace {

constexpr int RATE = 44100;
constexpr double TICK_HZ = 1193182.0 / 0x13B1;  // the effects timer (sound.md §2.2): 236.7 Hz
constexpr int SPEAKER_AMPLITUDE = 5000;         // as the game's host mixes the speaker
constexpr int MAX_TICKS = int(TICK_HZ * 10);    // an effect is cut after 10 s
constexpr int ENGINE_TICKS = int(TICK_HZ * 3);  // the engine loops: 3 s of it

// ---- the effects' notes, from the original's driver

// One speaker change of the driver: at driver tick `tick`, PIT divisor and gate.
struct Change {
    int tick;
    u16 divisor;
    bool on;
};
struct Score {
    std::vector<Change> changes;
    int ticks = 0;  // how long the driver ran
};

std::string game_dir, game_error;
bool game_loaded;

std::string find_file(const std::string &dir, const char *name)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        const std::string n = entry.path().filename().string();
        if (n.size() != std::strlen(name)) continue;
        bool same = true;
        for (size_t i = 0; same && i < n.size(); i++) same = std::tolower(u8(n[i])) == std::tolower(u8(name[i]));
        if (same) return entry.path().string();
    }
    return {};
}

bool load_game(const std::string &dir)
{
    game_dir = dir;
    const std::string exe = find_file(dir, "GB.EXE");
    if (exe.empty()) {
        game_error = "No GB.EXE in " + dir + ". The notes of the effects come from the original game.";
        return game_loaded = false;
    }
    ExeInfo info;
    std::string err;
    if (!mem_load_exe(exe, info, err)) {
        game_error = err;
        return game_loaded = false;
    }
    game_error.clear();
    return game_loaded = true;
}

// Runs the driver on effect `id` as the game starts it (the engine: from its throttles, as
// engine_sound_update does each frame) and records the speaker. The driver's state is set up as
// sfx_install leaves it, without the timer: each driver tick is one sfx_timer_tick.
Score capture(int id, int throttle)
{
    Score sc;
    if (!game_loaded) return sc;
    sfx_speaker_init();
    ds_u8(DS_sfx_device_tandy) = 0;
    ds_u8(DS_sfx_timer_on) = 1;
    ds_u16(DS_sound_muted) = 0;
    host_stub_speaker_clear();
    int limit = MAX_TICKS;
    if (id == 6) {
        ds_u16(DS_e_toggle) = 0;
        ds_u8(DS_throttle) = u8(throttle);
        ds_u8(DS_throttle + 1) = u8(throttle);
        engine_sound_update();
        limit = ENGINE_TICKS;
    } else {
        sfx_play(u16(sfx_program_of(id)));
    }
    for (int t = 0; t < limit; t++) {
        const size_t before = host_stub_speaker_log().size();
        sfx_timer_tick();
        const auto &log = host_stub_speaker_log();
        for (size_t k = before; k < log.size(); k++) sc.changes.push_back({t, log[k].divisor, log[k].on});
        sc.ticks = t + 1;
        if (ds_u16(DS_sfx_state) == 0) break;
    }
    sc.changes.push_back({sc.ticks, 0, false});  // the driver stops (the engine: cut here)
    ds_u8(DS_sfx_timer_on) = 0;
    return sc;
}

// ---- rendering the notes to samples (stereo s16)

struct Ticker {  // samples per driver tick, with the fraction carried
    double frac = 0;
    int next()
    {
        frac += RATE / TICK_HZ;
        const int n = int(frac);
        frac -= n;
        return n;
    }
};

std::vector<s16> render_speaker(const Score &sc)
{
    std::vector<s16> out;
    Ticker ticker;
    u16 div = 0;
    bool on = false;
    double phase = 0;
    size_t k = 0;
    for (int t = 0; t <= sc.ticks; t++) {
        while (k < sc.changes.size() && sc.changes[k].tick == t) {
            div = sc.changes[k].divisor;
            on = sc.changes[k].on;
            k++;
        }
        const double step = 1193182.0 / (div ? div : 65536) / RATE;
        for (int n = ticker.next(); n > 0; n--) {
            s16 s = 0;
            if (on) {
                s = s16(phase < 0.5 ? SPEAKER_AMPLITUDE : -SPEAKER_AMPLITUDE);
                phase += step;
                phase -= int(phase);
            }
            out.push_back(s);
            out.push_back(s);
        }
    }
    out.resize(out.size() + RATE / 10 * 2, 0);
    return out;
}

void chip_write(void *chip, u8 reg, u8 value) { OPL3_WriteReg(static_cast<opl3_chip *>(chip), reg, value); }

std::vector<s16> render_fm(const Score &sc, int id, const SfxPatch &patch)
{
    static opl3_chip chip;  // large: not on the stack
    OPL3_Reset(&chip, RATE);
    SfxSynth synth(chip_write, &chip);
    SfxBank bank;
    sfx_bank_defaults(bank);
    bank.fx[id] = patch;
    synth.set_bank(bank);
    synth.reset();
    std::vector<s16> out;
    Ticker ticker;
    size_t k = 0;
    auto generate = [&](int n) {
        const size_t at = out.size();
        out.resize(at + size_t(2 * n));
        if (n > 0) OPL3_GenerateStream(&chip, &out[at], uint32_t(n));
        for (size_t i = at; i < out.size(); i++) out[i] = s16(std::clamp(out[i] * SFX_GAIN, -32768, 32767));
    };
    for (int t = 0; t <= sc.ticks; t++) {
        while (k < sc.changes.size() && sc.changes[k].tick == t) {
            synth.speaker(id, id, sc.changes[k].divisor, sc.changes[k].on);
            k++;
        }
        synth.tick();
        generate(ticker.next());
    }
    synth.silence_all();
    // the release, until it has died away (at most 3 s)
    for (int block = 0; block < 3 * 20; block++) {
        const size_t at = out.size();
        generate(RATE / 20);
        int peak = 0;
        for (size_t i = at; i < out.size(); i++) peak = std::max(peak, std::abs(int(out[i])));
        if (peak < 40) break;
    }
    return out;
}

// ---- playback

SDL_AudioStream *audio;
Uint64 play_start_ns, play_len_ns;
int play_id = -1;
double play_ticks_len;  // the driver ticks in the played score (for the cursor)

void play(const std::vector<s16> &pcm, int id, const Score &sc)
{
    if (!audio) return;
    SDL_ClearAudioStream(audio);
    SDL_PutAudioStreamData(audio, pcm.data(), int(pcm.size() * sizeof(s16)));
    play_start_ns = SDL_GetTicksNS();
    play_len_ns = Uint64(pcm.size() / 2) * SDL_NS_PER_SECOND / RATE;
    play_id = id;
    play_ticks_len = sc.ticks;
}

void stop()
{
    if (audio) SDL_ClearAudioStream(audio);
    play_id = -1;
}

bool playing() { return play_id >= 0 && SDL_GetTicksNS() - play_start_ns < play_len_ns; }

// ---- the editor's state

SfxBank bank, saved;
int sel = 1;
int throttle = 40;
bool play_on_change = true;
bool want_preview;
std::string status;
Uint64 status_until;
SfxPatch clipboard;
bool have_clipboard;

bool dirty(int id) { return sfx_patch_text(bank.fx[id]) != sfx_patch_text(saved.fx[id]); }
bool any_dirty()
{
    for (int i = 0; i < SFX_COUNT; i++)
        if (dirty(i)) return true;
    return false;
}

void say(const std::string &s)
{
    status = s;
    status_until = SDL_GetTicksNS() + 4 * SDL_NS_PER_SECOND;
}

void preview(bool fm)
{
    if (!game_loaded) {
        say("No game loaded: choose the game folder first.");
        return;
    }
    const Score sc = capture(sel, throttle);
    play(fm ? render_fm(sc, sel, bank.fx[sel]) : render_speaker(sc), sel, sc);
}

// The player's bank; run from a build in the port's source tree, also the shipped bank
// (gunboat-port/data/sfx.ini), which the next build builds into the game and the editor.
void save()
{
    const std::string path = sfx_bank_path(), source = sfx_source_bank_path();
    if (!sfx_bank_save(path, bank)) {
        say("Could not write " + path);
        return;
    }
    saved = bank;
    if (source.empty()) say("Saved " + path);
    else if (sfx_bank_save(source, bank)) say("Saved " + path + " and " + source + " (the next build ships it)");
    else say("Saved " + path + "; could not write " + source);
}

std::mutex picked_mutex;
std::string picked_dir;
bool picked;

void SDLCALL folder_picked(void *, const char *const *files, int)
{
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(picked_mutex);
    picked_dir = files[0];
    picked = true;
}

// ---- widgets

void tip(const char *text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}

bool slider(const char *label, u8 &v, int lo, int hi, const char *help)
{
    int x = v;
    const bool c = ImGui::SliderInt(label, &x, lo, hi);
    if (c) v = u8(x);
    tip(help);
    return c;
}

bool slider(const char *label, int &v, int lo, int hi, const char *help, const char *fmt = "%d")
{
    const bool c = ImGui::SliderInt(label, &v, lo, hi, fmt);
    tip(help);
    return c;
}

bool check(const char *label, bool &v, const char *help)
{
    const bool c = ImGui::Checkbox(label, &v);
    tip(help);
    return c;
}

bool operator_editor(const char *id, SfxOperator &o)
{
    bool c = false;
    ImGui::PushID(id);
    ImGui::PushItemWidth(-90);
    c |= slider("Attack", o.attack, 0, 15, "Attack rate: 15 is instant, 0 never rises.");
    c |= slider("Decay", o.decay, 0, 15, "Decay rate to the sustain level: 15 fastest.");
    c |= slider("Sustain", o.sustain, 0, 15, "Sustain level, as an attenuation in 3 dB steps: 0 is full level.");
    c |= slider("Release", o.release, 0, 15, "Release rate after the note ends: 15 fastest. Low values let short effects ring on.");
    c |= slider("Multiple", o.multiple, 0, 15, "Frequency multiple of the note (0 = one half). Different multiples of the two operators give brighter or metallic tones.");
    c |= slider("Level", o.level, 0, 63, "Output level, as an attenuation in 0.75 dB steps: 0 is loudest. On the modulator (FM) it sets how much it colours the carrier.");
    static const char *const WAVES[] = {"Sine", "Half sine", "Absolute sine", "Quarter sine"};
    int w = o.waveform;
    if (ImGui::Combo("Waveform", &w, WAVES, 4)) {
        o.waveform = u8(w);
        c = true;
    }
    tip("The operator's waveform (OPL2).");
    c |= slider("Key scale", o.ksl, 0, 3, "Key scale level: higher notes quieter (0 off, 3 most).");
    ImGui::PopItemWidth();
    c |= check("Sustained", o.sustained, "Hold the sustain level while the note is on. Off: the sound keeps decaying at the release rate after the decay.");
    ImGui::SameLine();
    c |= check("KSR", o.ksr, "Key scale rate: higher notes have faster envelopes.");
    c |= check("Tremolo", o.tremolo, "Amplitude vibrato.");
    ImGui::SameLine();
    c |= check("Vibrato", o.vibrato, "Frequency vibrato.");
    ImGui::PopID();
    return c;
}

// The effect's notes as a piano roll: time across, pitch up; the cursor while it plays.
void draw_score(const Score &sc)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 110;
    ImGui::Dummy(ImVec2(w, h));
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(20, 24, 36, 255));
    if (sc.ticks <= 0) return;
    const double lo = std::log2(25.0), hi = std::log2(5000.0);
    auto y_of = [&](u16 div) {
        const double f = 1193182.0 / (div ? div : 65536);
        const double v = std::clamp((std::log2(f) - lo) / (hi - lo), 0.0, 1.0);
        return p0.y + h - 4 - float(v) * (h - 8);
    };
    const float span = float(std::max(sc.ticks, 1));
    u16 div = 0;
    bool on = false;
    int from = 0;
    for (const Change &c : sc.changes) {
        if (on && c.tick > from) {
            const float x0 = p0.x + w * from / span, x1 = p0.x + w * c.tick / span, y = y_of(div);
            dl->AddRectFilled(ImVec2(x0, y - 2), ImVec2(std::max(x1 - 1, x0 + 1), y + 2), IM_COL32(120, 200, 255, 255));
        }
        from = c.tick;
        div = c.divisor;
        on = c.on;
    }
    char label[96];
    std::snprintf(label, sizeof label, "%.2f s, %zu speaker changes", sc.ticks / TICK_HZ, sc.changes.size());
    dl->AddText(ImVec2(p0.x + 6, p0.y + 4), IM_COL32(150, 160, 180, 255), label);
    if (playing() && play_id == sel) {
        const double t = double(SDL_GetTicksNS() - play_start_ns) / SDL_NS_PER_SECOND * TICK_HZ;
        if (t <= play_ticks_len) {
            const float x = p0.x + w * float(t / span);
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + h), IM_COL32(255, 200, 80, 255));
        }
    }
}

void ui(SDL_Window *window, bool &quit_asked)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // The top bar.
    if (ImGui::Button("Save")) save();
    tip(sfx_source_bank_path().empty()
            ? "Write the instruments to sfx.ini, where the game reads them."
            : "Write the instruments to sfx.ini, where the game reads them, and to the port's data/sfx.ini: the next build ships them.");
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        sfx_bank_load(sfx_bank_path(), bank);
        saved = bank;
        say("Reloaded the saved instruments.");
    }
    tip("Forget the changes since the last save.");
    ImGui::SameLine();
    if (ImGui::Button("All defaults")) {
        sfx_bank_defaults(bank);
        say("Every effect has its default instrument (not saved yet).");
    }
    tip("The instruments the port ships with (data/sfx.ini, built in) for every effect.");
    ImGui::SameLine();
    if (ImGui::Button("Game folder...")) SDL_ShowOpenFolderDialog(folder_picked, nullptr, window, game_dir.c_str(), false);
    tip("The folder with the original GB.EXE: the notes of the effects come from it.");
    ImGui::SameLine();
    ImGui::Checkbox("Play on change", &play_on_change);
    tip("Play the AdLib version whenever a setting is changed.");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", sfx_bank_path().c_str());
    if (!sfx_source_bank_path().empty()) ImGui::TextDisabled("and the build's bank %s", sfx_source_bank_path().c_str());
    if (!game_loaded) ImGui::TextColored(ImVec4(1, 0.55f, 0.4f, 1), "%s", game_error.c_str());
    ImGui::Separator();

    // The effects.
    ImGui::BeginChild("list", ImVec2(230, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < SFX_COUNT; i++) {
        const SfxOutput o = bank.fx[i].output;
        char label[80];
        std::snprintf(label, sizeof label, "%2d  %s%s  [%s]", i, sfx_name(i), dirty(i) ? " *" : "",
                      o == SfxOutput::Adlib ? "AdLib" : o == SfxOutput::Speaker ? "speaker" : "off");
        if (ImGui::Selectable(label, sel == i)) {
            if (sel != i) stop();
            sel = i;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) preview(true);
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Space: play the AdLib version. O: the original speaker. Double-click an effect to play it.");
    ImGui::EndChild();
    ImGui::SameLine();

    // The selected effect.
    ImGui::BeginChild("effect", ImVec2(0, 0), ImGuiChildFlags_Borders);
    SfxPatch &p = bank.fx[sel];
    ImGui::Text("%d  %s", sel, sfx_name(sel));
    ImGui::TextWrapped("%s", sfx_description(sel));
    ImGui::Spacing();
    if (ImGui::Button("Play original (PC speaker)")) preview(false);
    ImGui::SameLine();
    if (ImGui::Button("Play AdLib")) preview(true);
    ImGui::SameLine();
    if (ImGui::Button("Stop")) stop();
    if (sel == 6) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::SliderInt("Throttles", &throttle, 0, 103);
        tip("The engine effect follows the throttles (8 idle, 59 full, 103 with upgraded engines); at a sum below 4 it is off.");
    }
    static Score sc;  // the notes on show: captured again when the effect or the throttles change
    static int sc_sel = -1, sc_throttle = -1;
    static std::string sc_dir;
    if (sc_sel != sel || sc_throttle != throttle || sc_dir != game_dir) {
        sc = capture(sel, throttle);
        sc_sel = sel;
        sc_throttle = throttle;
        sc_dir = game_dir;
    }
    draw_score(sc);

    bool c = false;
    int out = int(p.output);
    ImGui::Text("In the game:");
    ImGui::SameLine();
    c |= ImGui::RadioButton("AdLib instrument", &out, int(SfxOutput::Adlib));
    ImGui::SameLine();
    c |= ImGui::RadioButton("PC speaker (original)", &out, int(SfxOutput::Speaker));
    ImGui::SameLine();
    c |= ImGui::RadioButton("Silent", &out, int(SfxOutput::Silent));
    p.output = SfxOutput(out);
    ImGui::Separator();

    ImGui::PushItemWidth(260);
    int conn = p.additive ? 1 : 0;
    ImGui::Text("Connection:");
    ImGui::SameLine();
    c |= ImGui::RadioButton("FM", &conn, 0);
    tip("The modulator shapes the carrier's tone; only the carrier is heard.");
    ImGui::SameLine();
    c |= ImGui::RadioButton("Additive", &conn, 1);
    tip("Both operators are heard, mixed.");
    p.additive = conn != 0;
    c |= slider("Feedback", p.feedback, 0, 7, "The modulator modulating itself: 0 pure, 7 close to noise (guns, explosions).");
    c |= slider("Transpose", p.transpose, -36, 36, "Semitones up or down from the original's notes.", "%d semitones");
    c |= slider("Volume", p.volume, 0, 100, "The effect's loudness.", "%d%%");
    c |= slider("Jitter", p.jitter, 0, 1200, "The pitch jumps randomly by up to this much on every driver tick (236 a second): noise for shots and explosions.", "%d cents");
    c |= check("Retrigger", p.retrigger, "A new pitch in the middle of a note attacks again. Off: it glides on in the same note (the engine).");
    ImGui::PopItemWidth();
    ImGui::Spacing();

    if (ImGui::BeginTable("ops", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "Modulator");
        c |= operator_editor("mod", p.mod);
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "Carrier");
        c |= operator_editor("car", p.car);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Default instrument")) {
        p = sfx_default_patch(sel);
        c = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        clipboard = p;
        have_clipboard = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_clipboard);
    if (ImGui::Button("Paste")) {
        const SfxOutput keep = p.output;
        p = clipboard;
        p.output = keep;
        c = true;
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    if (c && play_on_change && p.output == SfxOutput::Adlib) want_preview = true;
    if (want_preview && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        want_preview = false;
        preview(true);
    }

    // Keys.
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) preview(true);
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) preview(false);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) sel = (sel + SFX_COUNT - 1) % SFX_COUNT;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) sel = (sel + 1) % SFX_COUNT;
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
    }

    if (!status.empty() && SDL_GetTicksNS() < status_until) {
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetWindowHeight() - ImGui::GetFrameHeight()));
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "%s", status.c_str());
    }

    // Leaving with unsaved changes.
    if (quit_asked) {
        ImGui::OpenPopup("Unsaved changes");
        quit_asked = false;
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save the changed instruments before leaving?");
        if (ImGui::Button("Save")) {
            save();
            SDL_Event q{};
            q.type = SDL_EVENT_QUIT;
            saved = bank;
            SDL_PushEvent(&q);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) {
            saved = bank;
            SDL_Event q{};
            q.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&q);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

// Developer aid (--wav DIR): every effect rendered to DIR as sfxNN_speaker.wav and sfxNN_adlib.wav
// (the bank's instruments, the engine at throttles 40), with their levels printed; no window.
bool write_wav(const std::string &path, const std::vector<s16> &pcm)
{
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32le = [&](u32 v) { std::fwrite(&v, 4, 1, f); };
    auto u16le = [&](u16 v) { std::fwrite(&v, 2, 1, f); };
    const u32 bytes = u32(pcm.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32le(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32le(16);
    u16le(1);
    u16le(2);
    u32le(RATE);
    u32le(RATE * 4);
    u16le(4);
    u16le(16);
    std::fwrite("data", 1, 4, f);
    u32le(bytes);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    return std::fclose(f) == 0;
}

void levels(const std::vector<s16> &pcm, double &seconds, int &peak, double &rms)
{
    seconds = double(pcm.size() / 2) / RATE;
    peak = 0;
    double sum = 0;
    for (s16 v : pcm) {
        peak = std::max(peak, std::abs(int(v)));
        sum += double(v) * v;
    }
    rms = pcm.empty() ? 0 : std::sqrt(sum / double(pcm.size()));
}

int write_wavs(const std::string &dir)
{
    if (!game_loaded) {
        std::fprintf(stderr, "%s\n", game_error.c_str());
        return 1;
    }
    for (int id = 0; id < SFX_COUNT; id++) {
        const Score sc = capture(id, 40);
        const std::vector<s16> sp = render_speaker(sc), fm = render_fm(sc, id, bank.fx[id]);
        char name[64];
        std::snprintf(name, sizeof name, "/sfx%02d_speaker.wav", id);
        write_wav(dir + name, sp);
        std::snprintf(name, sizeof name, "/sfx%02d_adlib.wav", id);
        write_wav(dir + name, fm);
        double s1, s2, r1, r2;
        int p1, p2;
        levels(sp, s1, p1, r1);
        levels(fm, s2, p2, r2);
        std::printf("%2d %-11s %4d ticks %3zu changes | speaker %.2f s peak %5d rms %6.0f | adlib %.2f s peak %5d rms %6.0f\n",
                    id, sfx_name(id), sc.ticks, sc.changes.size(), s1, p1, r1, s2, p2, r2);
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    std::string dir;
    for (int i = 1; i < argc; i++)
        if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) dir = argv[++i];
    if (dir.empty()) {
        Settings st;
        settings_load(st);
        dir = game_dir_of(st);
    }
    host_stub_set_game_dir(dir.c_str());
    load_game(dir);
    sfx_bank_load(sfx_bank_path(), bank);
    saved = bank;
    const char *snapshot = nullptr;  // developer aid: --snapshot FILE saves the window's picture and exits
    for (int i = 1; i + 1 < argc; i++) {
        if (!std::strcmp(argv[i], "--wav")) return write_wavs(argv[i + 1]);
        if (!std::strcmp(argv[i], "--snapshot")) snapshot = argv[i + 1];
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Gunboat - sound effects editor", int(1020 * scale), int(700 * scale),
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (SDL_Surface *icon = SDL_CreateSurfaceFrom(ICON_SIZE, ICON_SIZE, SDL_PIXELFORMAT_RGBA32,
                                                  const_cast<u8 *>(ICON_RGBA), ICON_SIZE * 4)) {
        SDL_SetWindowIcon(window, icon);  // the port's own icon
        SDL_DestroySurface(icon);
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, RATE};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    io.FontGlobalScale = scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool quit_asked = false;
    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (any_dirty()) quit_asked = true;
                else running = false;
            }
        }
        {
            std::lock_guard<std::mutex> lock(picked_mutex);
            if (picked) {
                picked = false;
                host_stub_set_game_dir(picked_dir.c_str());
                say(load_game(picked_dir) ? "Loaded GB.EXE from " + picked_dir : game_error);
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ui(window, quit_asked);
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 12, 16, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        static int frames;
        if (snapshot && ++frames == 5) {
            if (SDL_Surface *shot = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(shot, snapshot);
                SDL_DestroySurface(shot);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
