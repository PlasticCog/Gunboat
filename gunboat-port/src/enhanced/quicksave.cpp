// Quicksave and quickload (quicksave.hpp).
#include "enhanced/quicksave.hpp"

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

struct Snapshot {
    std::unique_ptr<u8[]> mem;   // mem[] as it was
    u16 si = 0;                  // the mission loop's SI (a register it keeps from pass to pass)
    u8 dac[256][3] = {};         // the VGA palette
    u16 start = 0;               // the VGA display start (the page shown)
    u16 spk_divisor = 0;         // the speaker as the effects driver set it
    u8 spk_gate = 0;
    std::vector<u8> machine;     // the host's speaker and AdLib chip
    u8 hours = 0, minutes = 0, seconds = 0;  // the mission clock, to show
};

Snapshot snap;
bool have, in_mission;
int left = SAVES_PER_MISSION;
enum class Want { None, Save, Load } want = Want::None;
void (*note)(const char *) = nullptr;
void (*after_load)() = nullptr;

void say(const char *text)
{
    if (note) note(text);
}

bool demo() { return ds_u16(DS_demo_mode) != 0; }

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
    have = true;
    left--;
    char text[80];
    std::snprintf(text, sizeof text, "Quicksaved (%s left in this mission)",
                  left == 0 ? "none" : left == 1 ? "one" : "two");
    say(text);
}

u16 load()
{
    std::memcpy(mem, snap.mem.get(), MEM_SIZE);
    for (int i = 0; i < 256; i++) vga_dac_write(u8(i), snap.dac[i][0], snap.dac[i][1], snap.dac[i][2]);
    vga_set_start(snap.start);
    spk_hw_set(snap.spk_divisor, snap.spk_gate);
    host_machine_load(snap.machine);
    debris_clear();
    sfx_adlib_reset();
    if (after_load) after_load();
    say(("Quickloaded: back to " + quicksave_time()).c_str());
    return snap.si;
}

void on_start()
{
    in_mission = true;
    have = false;
    left = SAVES_PER_MISSION;
    want = Want::None;
}

void on_end()
{
    in_mission = false;
    have = false;
    want = Want::None;
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

} // namespace

void quicksave_install() { host_set_mission_handlers(on_start, on_pass, on_end); }

void quicksave_request_save()
{
    if (!in_mission) say("Quicksaves work during a mission");
    else if (demo()) say("No quicksaves in the demo");
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

} // namespace gb
