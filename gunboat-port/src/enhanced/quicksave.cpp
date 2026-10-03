// Quicksave and quickload (quicksave.hpp).
#include "enhanced/quicksave.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "enhanced/debris.hpp"
#include "enhanced/sfx_adlib.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "platform/vga.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr int SAVES_PER_MISSION = 2;
constexpr u32 FILE_VERSION = 1;
constexpr u16 VGA_MODE = 0x13;
enum Path : u16 { PRACTICE = 1, CAMPAIGN = 2 };  // the way to the saved mission (game_main)

struct Snapshot {
    std::unique_ptr<u8[]> mem;   // mem[] as it was
    u16 si = 0;                  // the mission loop's SI (a register it keeps from pass to pass)
    u8 dac[256][3] = {};         // the VGA palette
    u16 start = 0;               // the VGA display start (the page shown)
    u16 spk_divisor = 0;         // the speaker as the effects driver set it
    u8 spk_gate = 0;
    std::vector<u8> machine;     // the host's speaker, AdLib chip and timer rate
    u8 hours = 0, minutes = 0, seconds = 0;  // the mission clock, to show
    u16 path = CAMPAIGN, region = 0, mission = 0, practice = 0;
};

Snapshot snap;
bool have, in_mission;
int left = SAVES_PER_MISSION;
enum class Want { None, Save, Load } want = Want::None;
void (*note)(const char *) = nullptr;
void (*after_load)() = nullptr;
// the resume prompt at start-up: its text, the answer (-1 none yet)
bool prompt_active;
std::string prompt_text;
int prompt_answer = -1;

void say(const char *text)
{
    if (note) note(text);
}

bool demo() { return ds_u16(DS_demo_mode) != 0; }

std::string save_path()
{
    char *dir = SDL_GetPrefPath("", "Gunboat");
    std::string p = dir ? std::string(dir) + "quicksave.sav" : std::string("quicksave.sav");
    SDL_free(dir);
    return p;
}

// The game's GB.EXE as a number: a save of another game file is not resumed.
u32 game_fingerprint()
{
    static u32 cached;
    static bool done;
    if (done) return cached;
    done = true;
    char *path = host_game_path("GB.EXE", false);
    size_t n = 0;
    void *data = path ? SDL_LoadFile(path, &n) : nullptr;
    SDL_free(path);
    u32 h = 2166136261u;  // FNV-1a
    for (size_t i = 0; data && i < n; i++) h = (h ^ static_cast<const u8 *>(data)[i]) * 16777619u;
    SDL_free(data);
    return cached = h;
}

// The resident Ad Lib driver (ADLIB.COM, run before GB.EXE when the music is AdLib) hooks INT 65h.
bool adlib_in(const u8 *m) { return (m[0x65 * 4] | m[0x65 * 4 + 1] | m[0x65 * 4 + 2] | m[0x65 * 4 + 3]) != 0; }

std::string describe(const Snapshot &s)
{
    static const char *const REGIONS[] = {"Vietnam", "Colombia", "Panama"};
    static const char *const PRACTICE_NAMES[] = {"Gunnery", "Grenade", "Pilot"};
    char t[96];
    if (s.practice >= 1 && s.practice <= 3)
        std::snprintf(t, sizeof t, "%s practice, mission time %02u:%02u:%02u", PRACTICE_NAMES[s.practice - 1],
                      s.hours, s.minutes, s.seconds);
    else
        std::snprintf(t, sizeof t, "%s, mission %u, mission time %02u:%02u:%02u",
                      s.region < 3 ? REGIONS[s.region] : "Practice", s.mission, s.hours, s.minutes, s.seconds);
    return t;
}

// ---- the file: quicksave.sav in the settings folder, written at each save, removed when its
// mission ends or another starts

struct Header {
    char magic[4];
    u32 version, fingerprint, mem_size, machine_size;
    u16 path, si, region, mission, practice, video_mode, start, spk_divisor;
    u8 left, hours, minutes, seconds, adlib, spk_gate;
};

bool write_file()
{
    Header h{};
    std::memcpy(h.magic, "GBQS", 4);
    h.version = FILE_VERSION;
    h.fingerprint = game_fingerprint();
    h.mem_size = MEM_SIZE;
    h.machine_size = u32(snap.machine.size());
    h.path = snap.path;
    h.si = snap.si;
    h.region = snap.region;
    h.mission = snap.mission;
    h.practice = snap.practice;
    h.video_mode = VGA_MODE;
    h.start = snap.start;
    h.spk_divisor = snap.spk_divisor;
    h.left = u8(left);
    h.hours = snap.hours;
    h.minutes = snap.minutes;
    h.seconds = snap.seconds;
    h.adlib = adlib_in(snap.mem.get());
    h.spk_gate = snap.spk_gate;
    const std::string path = save_path(), tmp = path + ".new";
    SDL_IOStream *io = SDL_IOFromFile(tmp.c_str(), "wb");
    if (!io) return false;
    bool ok = SDL_WriteIO(io, &h, sizeof h) == sizeof h && SDL_WriteIO(io, snap.dac, sizeof snap.dac) == sizeof snap.dac &&
              SDL_WriteIO(io, snap.machine.data(), snap.machine.size()) == snap.machine.size() &&
              SDL_WriteIO(io, snap.mem.get(), MEM_SIZE) == MEM_SIZE;
    ok = SDL_CloseIO(io) && ok;
    return ok && SDL_RenamePath(tmp.c_str(), path.c_str());  // whole, or the old one stays
}

void delete_file() { SDL_RemovePath(save_path().c_str()); }

// The file's header (and, with `all`, the whole save into s); false when there is none or it is
// not a quicksave of this build.
bool read_file(Header &h, Snapshot *s)
{
    SDL_IOStream *io = SDL_IOFromFile(save_path().c_str(), "rb");
    if (!io) return false;
    bool ok = SDL_ReadIO(io, &h, sizeof h) == sizeof h && std::memcmp(h.magic, "GBQS", 4) == 0 &&
              h.version == FILE_VERSION && h.mem_size == MEM_SIZE &&
              h.machine_size == host_machine_save().size();
    if (ok && s) {
        if (!s->mem) s->mem.reset(new u8[MEM_SIZE]);
        s->machine.resize(h.machine_size);
        ok = SDL_ReadIO(io, s->dac, sizeof s->dac) == sizeof s->dac &&
             SDL_ReadIO(io, s->machine.data(), s->machine.size()) == s->machine.size() &&
             SDL_ReadIO(io, s->mem.get(), MEM_SIZE) == MEM_SIZE;
        s->si = h.si;
        s->start = h.start;
        s->spk_divisor = h.spk_divisor;
        s->spk_gate = h.spk_gate;
        s->hours = h.hours;
        s->minutes = h.minutes;
        s->seconds = h.seconds;
        s->path = h.path;
        s->region = h.region;
        s->mission = h.mission;
        s->practice = h.practice;
    }
    SDL_CloseIO(io);
    return ok;
}

// ---- saving and loading at the mission loop's pass

void save(u16 si)
{
    if (!snap.mem) snap.mem.reset(new u8[MEM_SIZE]);
    std::memcpy(snap.mem.get(), mem, MEM_SIZE);
    snap.si = si;
    for (int i = 0; i < 256; i++) vga_dac_read(u8(i), &snap.dac[i][0], &snap.dac[i][1], &snap.dac[i][2]);
    snap.start = vga_start();
    spk_hw_state(&snap.spk_divisor, &snap.spk_gate);
    snap.machine = host_machine_save();
    snap.hours = ds_u8(DS_clock_hours);
    snap.minutes = ds_u8(DS_clock_minutes);
    snap.seconds = ds_u8(DS_clock_seconds);
    snap.practice = ds_u16(DS_practice_mode);
    snap.path = snap.practice != 0 ? PRACTICE : CAMPAIGN;
    snap.region = ds_u16(DS_region);
    snap.mission = ds_u16(DS_mission_number);
    have = true;
    left--;
    const bool kept = write_file();
    char text[96];
    std::snprintf(text, sizeof text, "Quicksaved (%s left in this mission)%s",
                  left == 0 ? "none" : left == 1 ? "one" : "two", kept ? "" : " - not written to disk!");
    say(text);
}

// The saved game back into mem[] and the machine.
void restore()
{
    std::memcpy(mem, snap.mem.get(), MEM_SIZE);
    for (int i = 0; i < 256; i++) vga_dac_write(u8(i), snap.dac[i][0], snap.dac[i][1], snap.dac[i][2]);
    vga_set_start(snap.start);
    spk_hw_set(snap.spk_divisor, snap.spk_gate);
    host_machine_load(snap.machine);
    debris_clear();
    sfx_adlib_reset();
    if (after_load) after_load();
}

u16 load()
{
    restore();
    say(("Quickloaded: back to " + quicksave_time()).c_str());
    return snap.si;
}

// A mission starts (not the title's demo, which leaves the quicksave alone): two saves again, and
// the last mission's quicksave is gone.
void on_start()
{
    if (demo()) return;
    in_mission = true;
    have = false;
    left = SAVES_PER_MISSION;
    want = Want::None;
    delete_file();
}

void on_end()
{
    if (demo()) return;
    in_mission = false;
    have = false;
    want = Want::None;
    delete_file();
}

// The top of a pass of the mission loop: between two frames.
u16 on_pass(u16 si)
{
    const Want w = want;
    want = Want::None;
    if (w == Want::Save && left > 0 && !demo()) save(si);
    else if (w == Want::Load && have) si = load();
    return si;
}

// game_main, after its set-up: a quicksave on disk is offered; resumed, it is this run's.
u16 on_resume(u16 *si)
{
    Header h{};
    if (!read_file(h, nullptr)) return 0;
    if (h.fingerprint != game_fingerprint()) {
        say("The quicksave was made with another GB.EXE: not resumed");
        return 0;
    }
    if (ds_u16(DS_video_mode) != VGA_MODE) {
        say("The quicksave needs the VGA video card (launcher)");
        return 0;
    }
    if (bool(h.adlib) != adlib_in(mem)) {
        say(h.adlib ? "The quicksave was made with AdLib music: choose it in the launcher to resume"
                    : "The quicksave was made with the PC speaker's music: choose it in the launcher to resume");
        return 0;
    }
    Snapshot s;
    if (!read_file(h, &s)) {
        say("The quicksave could not be read");
        return 0;
    }
    prompt_text = describe(s);
    prompt_answer = -1;
    prompt_active = true;
    host_set_paused(true);  // host_pump holds here, drawing the prompt, until it is answered
    while (prompt_answer < 0) host_pump();
    prompt_active = false;
    if (prompt_answer == 0) return 0;  // kept for another time
    snap = std::move(s);
    restore();
    in_mission = true;
    have = true;
    left = h.left;
    want = Want::None;
    say(("Resumed: " + describe(snap)).c_str());
    *si = snap.si;
    return snap.path;
}

} // namespace

void quicksave_install()
{
    host_set_mission_handlers(on_start, on_pass, on_end);
    host_set_resume_handler(on_resume);
}

void quicksave_request_save()
{
    if (!in_mission) say("Quicksaves work during a mission");
    else if (demo()) say("No quicksaves in the demo");
    else if (ds_u16(DS_video_mode) != VGA_MODE) say("Quicksaves need the VGA video card");
    else if (left <= 0) say("No quicksaves left in this mission");
    else want = Want::Save;
}

void quicksave_request_load()
{
    if (!in_mission) say("Quicksaves work during a mission");
    else if (!have) say("No quicksave in this mission yet");
    else want = Want::Load;
}

bool quicksave_in_mission() { return in_mission && !demo(); }
int quicksave_saves_left() { return left; }
bool quicksave_have() { return have; }

std::string quicksave_time()
{
    char t[16];
    std::snprintf(t, sizeof t, "%02u:%02u:%02u", snap.hours, snap.minutes, snap.seconds);
    return t;
}

void quicksave_set_note(void (*n)(const char *)) { note = n; }
void quicksave_set_after_load(void (*f)()) { after_load = f; }

bool quicksave_prompt(std::string &text)
{
    if (!prompt_active) return false;
    text = prompt_text;
    return true;
}

void quicksave_answer(bool resume)
{
    if (!prompt_active) return;
    prompt_answer = resume ? 1 : 0;
    host_set_paused(false);
}

} // namespace gb
