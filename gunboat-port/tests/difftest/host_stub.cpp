// host.hpp for the differential tests (see host_stub.hpp).
#include "host_stub.hpp"

#include "host.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace gb {

namespace {
void (*tick_handler)();
void (*kbd_handler)(u8);
void (*focus_lost_handler)();
std::string game_dir = ".";

char *dup(const std::string &s)
{
    char *p = static_cast<char *>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}
} // namespace

void host_stub_set_game_dir(const char *dir) { game_dir = dir; }

bool host_init(const char *dir, int, bool)
{
    game_dir = dir;
    return true;
}
void host_shutdown() {}
namespace {
u16 game_divisor;
std::vector<u16> timer_log;
}
// The game's timer programming is recorded (the divisor log is compared by the sound tests); the
// handler is not run: host_pump() runs the test's tick.
void host_set_timer(u16 divisor, void (*)())
{
    game_divisor = divisor;
    timer_log.push_back(divisor);
}
void host_stub_set_test_tick(void (*tick)()) { tick_handler = tick; }
u16 host_stub_timer_divisor() { return game_divisor; }
const std::vector<u16> &host_stub_timer_log() { return timer_log; }
void host_stub_timer_clear() { timer_log.clear(); }
void host_set_frame_source(bool (*)(u32 *, int *, int *), int, int) {}
void host_pump()
{
    if (tick_handler) tick_handler();
}
// The Unicorn tests model port 3DAh as always in the vertical retrace: no wait, no timer tick.
void host_wait_vretrace() {}
void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_focus_lost_handler(void (*handler)()) { focus_lost_handler = handler; }
namespace {
bool joy_present;
s16 joy_x, joy_y;
u8 joy_buttons;
} // namespace
void host_stub_set_joy(bool present, s16 x, s16 y, u8 buttons)
{
    joy_present = present;
    joy_x = x;
    joy_y = y;
    joy_buttons = buttons;
}
bool host_joy_read(s16 *x, s16 *y, u8 *buttons)
{
    if (!joy_present) return false;
    if (x) *x = joy_x;
    if (y) *y = joy_y;
    if (buttons) *buttons = joy_buttons;
    return true;
}
namespace {
std::vector<OplWrite> opl_log;
}
void host_opl_write(u8 reg, u8 value) { opl_log.push_back({reg, value}); }
const std::vector<OplWrite> &host_stub_opl_log() { return opl_log; }
void host_stub_opl_clear() { opl_log.clear(); }
namespace {
u8 pit2_low;
}
u8 host_pit2_low() { return pit2_low; }
void host_set_frame_rate(int) {}
void host_frame_pace() {}  // the tests' time is the tick model
void host_stub_set_pit2(u8 v) { pit2_low = v; }
namespace {
std::vector<SpeakerEvent> speaker_log;
}
void host_speaker(u16 divisor, bool on) { speaker_log.push_back({divisor, on}); }
const std::vector<SpeakerEvent> &host_stub_speaker_log() { return speaker_log; }
void host_stub_speaker_clear() { speaker_log.clear(); }

char *host_game_path(const char *name, bool create)
{
    namespace fs = std::filesystem;
    const fs::path direct = fs::path(game_dir) / name;
    if (fs::exists(direct)) return dup(direct.string());
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(game_dir, ec)) {
        const std::string n = entry.path().filename().string();
        if (n.size() == std::strlen(name) &&
            std::equal(n.begin(), n.end(), name, [](char a, char b) { return std::tolower(u8(a)) == std::tolower(u8(b)); }))
            return dup(entry.path().string());
    }
    return create ? dup(direct.string()) : nullptr;
}

void host_free(void *p) { std::free(p); }

void host_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    throw HostFatal(msg);
}

void host_exit(int code) { throw HostExit(code); }
void host_error_box(const char *) {}

} // namespace gb
