// Gunboat (Accolade, 1990): faithful C++/SDL3 port of GB.EXE. Entry point.
//
// usage: gunboat [--game-dir DIR] [--scale N] [--fullscreen] [--fps N] [--sound adlib|speaker]
//                [--check] [--host-test]
//   --game-dir   folder with the original game files (default: the current folder)
//   --scale      initial window scale: 320x240 times N (default 3)
//   --fullscreen start in full screen (Alt+Enter switches)
//   --fps        frames per second of the 3D stations (default 15: the mission clock then runs in
//                real time, 15 simulation passes per game second; 0 = as fast as possible, the
//                original's rule; PORT, simulation.md §1.1)
//   --sound      adlib: the machine has an AdLib and the player ran ADLIB.COM (the Ad Lib sound
//                driver V1.51 of the game folder) before GB.EXE: its resident image is installed
//                first (sound/adlib_driver.cpp) and the title music plays VALK12.MUS on the OPL2.
//                speaker: no AdLib driver: the music plays VALKPC.MUS on the PC speaker. Default:
//                adlib when the game folder has ADLIB.COM, else speaker. (The effects use the
//                speaker either way, as in the original.)
//   --check      load and verify GB.EXE, print a summary and exit (no window)
//   --host-test  developer check of the SDL host: runs the three timer rates for a moment and
//                compares the interrupts counted with the PIT rates (use SDL_VIDEO_DRIVER=dummy)
//
// Without --check / --host-test it runs the game: main (0000:0000, game_flow.md §1) as far as it
// is ported (phase 4: the start-up, the title and the menu).
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "game/flow.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "platform/vga.hpp"
#include "sound/sound.hpp"

using namespace gb;

namespace {

int usage(const char *prog)
{
    std::fprintf(stderr,
                 "usage: %s [--game-dir DIR] [--scale N] [--fullscreen] [--fps N] [--sound adlib|speaker] [--check]"
                 " [--host-test]\n",
                 prog);
    return 2;
}

// Case-insensitive lookup of a game file (the host does the same once it is running).
std::string find_game_file(const std::string &dir, const char *name)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        const std::string n = entry.path().filename().string();
        bool same = n.size() == std::strlen(name);
        for (size_t i = 0; same && i < n.size(); i++)
            same = std::tolower(u8(n[i])) == std::tolower(u8(name[i]));
        if (same) return entry.path().string();
    }
    return (fs::path(dir) / name).string();
}

// --host-test: timer interrupts counted against the PIT rates GB.EXE programs.
unsigned long host_test_ticks;
void host_test_tick() { host_test_ticks++; }

bool host_test_rate(const char *what, u16 divisor, double seconds)
{
    host_test_ticks = 0;
    host_set_timer(divisor, host_test_tick);
    const auto t0 = std::chrono::steady_clock::now();
    double elapsed = 0;
    while (elapsed < seconds) {
        host_pump();
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    host_set_timer(divisor, nullptr);
    const double want = elapsed * PIT_HZ / (divisor ? divisor : 65536);
    const double slack = 2 + want * 0.02;  // the first and last interrupt, and a busy machine
    const bool ok = host_test_ticks + slack >= want && host_test_ticks <= want + slack;
    std::printf("%-8s %5.1f Hz: %lu interrupts in %.2f s, expected %.1f  %s\n", what,
                double(PIT_HZ) / (divisor ? divisor : 65536), host_test_ticks, elapsed, want, ok ? "ok" : "WRONG");
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    std::string dir = ".";
    int scale = 3;
    bool check = false, fullscreen = false, host_test = false;
    const char *sound = nullptr;  // --sound adlib / speaker; null: the default
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : nullptr;
        if (!std::strcmp(a, "--game-dir") && v) { dir = v; i++; }
        else if (!std::strcmp(a, "--scale") && v) { scale = std::atoi(v); i++; }
        else if (!std::strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!std::strcmp(a, "--fps") && v) { host_set_frame_rate(std::atoi(v)); i++; }
        else if (!std::strcmp(a, "--sound") && v && (!std::strcmp(v, "adlib") || !std::strcmp(v, "speaker"))) {
            sound = v;
            i++;
        }
        else if (!std::strcmp(a, "--check")) check = true;
        else if (!std::strcmp(a, "--host-test")) host_test = true;
        else return usage(argv[0]);
    }

    if (host_test) {
        if (!host_init(dir.c_str(), scale, false)) return 1;
        vga_init();
        bool ok = host_test_rate("menus", PIT_DIV_MENU, 1.0);
        ok = host_test_rate("missions", PIT_DIV_MISSION, 0.5) && ok;
        ok = host_test_rate("BIOS", PIT_DIV_BIOS, 0.6) && ok;
        host_shutdown();
        return ok ? 0 : 1;
    }

    const std::string exe = find_game_file(dir, "GB.EXE");
    ExeInfo info;
    std::string err;
    if (!mem_load_exe(exe, info, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (check) {
        std::printf("GB.EXE ok (%s): image %u bytes, %u relocations, at %04X:0000, DGROUP %04X\n",
                    info.packed ? "EXEPACK" : "unpacked", unsigned(info.image_size), unsigned(info.relocations),
                    LOAD_SEG, DGROUP);
        return 0;
    }

    // --sound adlib: DOS loads ADLIB.COM below GB.EXE (checked before a window opens).
    bool adlib = false;
    if (!sound || !std::strcmp(sound, "adlib")) {
        const std::string com = find_game_file(dir, "ADLIB.COM");
        std::error_code ec;
        const bool found = std::filesystem::exists(com, ec);
        if (sound && !found) {
            std::fprintf(stderr, "--sound adlib: the game folder has no ADLIB.COM (the Ad Lib sound driver)\n");
            return 1;
        }
        if (found) {
            if (!adlib_load(com, err)) {
                std::fprintf(stderr, "%s\n", err.c_str());
                return 1;
            }
            adlib = true;
        }
    }

    // The machine as DOS leaves it to GB.EXE, then the program (it ends through the runtime's exit).
    dos_heap_init();
    bios_init();
    if (!host_init(dir.c_str(), scale, fullscreen)) return 1;
    vga_init();
    host_set_kbd_handler(kbd_byte);
    host_set_focus_lost_handler(kbd_focus_lost);
    host_set_timer(PIT_DIV_BIOS, timer_interrupt);
    if (adlib) adlib_install();  // ADLIB.COM runs and stays resident: INT 65h, INT 8, the OPL2 set up
    game_main();
}
