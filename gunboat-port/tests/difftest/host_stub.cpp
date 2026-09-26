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
void host_set_timer(u16, void (*handler)()) { tick_handler = handler; }
void host_set_frame_source(bool (*)(u32 *), int, int) {}
void host_pump()
{
    if (tick_handler) tick_handler();
}
void host_wait_vretrace() { host_pump(); }
void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_focus_lost_handler(void (*handler)()) { focus_lost_handler = handler; }
bool host_joy_read(s16 *, s16 *, u8 *) { return false; }
void host_opl_write(u8, u8) {}
void host_speaker(u16, bool) {}

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

} // namespace gb
