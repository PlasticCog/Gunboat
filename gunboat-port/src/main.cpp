// Gunboat (Accolade, 1990): faithful C++/SDL3 port of GB.EXE. Entry point.
//
// usage: gunboat [--launcher | --no-launcher] [--game-dir DIR] [--scale N] [--fullscreen | --window]
//                [--fps N] [--sound adlib|speaker] [--original | --enhanced] [--view original|hires]
//                [--motion original|smooth] [--widescreen on|off] [--aspect 4:3|square]
//                [--filter sharp|nearest|smooth|crt] [--check] [--host-test]
//   The player's settings (gunboat.ini, src/enhanced/settings.hpp) give the defaults; the options
//   override them for this run. The launcher (src/enhanced/launcher.cpp) shows first unless the
//   settings say not to or --no-launcher is given; it saves the settings when the game starts.
//   --game-dir   folder with the original game files
//   --scale      initial window scale: 320x240 times N (default 3)
//   --fullscreen start in full screen (Alt+Enter switches); --window: in a window
//   --fps        frames per second of the 3D stations (default 15: the mission clock then runs in
//                real time, 15 simulation passes per game second; 0 = as fast as possible, the
//                original's rule; PORT, simulation.md §1.1)
//   --sound      adlib: the machine has an AdLib and the player ran ADLIB.COM (the Ad Lib sound
//                driver V1.51 of the game folder) before GB.EXE: its resident image is installed
//                first (sound/adlib_driver.cpp) and the title music plays VALK12.MUS on the OPL2.
//                speaker: no AdLib driver: the music plays VALKPC.MUS on the PC speaker. Default:
//                adlib when the game folder has ADLIB.COM, else speaker. (The effects use the
//                speaker either way, as in the original.)
//   --original   no enhancements: the picture exactly as the original drew it (F11 in the game
//                switches); --enhanced: all of them. Or one by one: --view hires (the 3D view at the
//                window's resolution), --motion smooth (60 fps, interpolated), --widescreen on (the
//                world beside the picture in a wide window). --aspect and --filter: the picture.
//   --check      load and verify GB.EXE, print a summary and exit (no window)
//   --host-test  developer check of the SDL host: runs the three timer rates for a moment and
//                compares the interrupts counted with the PIT rates (use SDL_VIDEO_DRIVER=dummy)
//
// Without --check / --host-test it runs the game: main (0000:0000, game_flow.md §1).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "enhanced/launcher.hpp"
#include "enhanced/present.hpp"
#include "enhanced/settings.hpp"
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
                 "usage: %s [--launcher | --no-launcher] [--game-dir DIR] [--scale N] [--fullscreen | --window] [--fps N]\n"
                 "          [--sound adlib|speaker] [--original | --enhanced] [--view original|hires]\n"
                 "          [--motion original|smooth] [--widescreen on|off] [--aspect 4:3|square]\n"
                 "          [--filter sharp|nearest|smooth|crt] [--check] [--host-test]\n",
                 prog);
    return 2;
}

// A fatal start-up error: printed, and shown in a message box once the window is open.
int fail(bool window, const std::string &msg)
{
    std::fprintf(stderr, "%s\n", msg.c_str());
    if (window) {
        host_error_box(msg.c_str());
        host_shutdown();
    }
    return 1;
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
    Settings st;
    settings_load(st);
    bool check = false, host_test = false, force_launcher = false, no_launcher = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : nullptr;
        auto is = [&](const char *opt) { return !std::strcmp(a, opt); };
        auto val = [&](const char *opt, const char *x) {
            if (!is(opt) || !v || std::strcmp(v, x)) return false;
            i++;
            return true;
        };
        if (is("--game-dir") && v) { st.game_dir = v; i++; }
        else if (is("--scale") && v) { st.window_scale = std::max(1, std::atoi(v)); i++; }
        else if (is("--fullscreen")) st.fullscreen = true;
        else if (is("--window")) st.fullscreen = false;
        else if (is("--fps") && v) { st.fps = std::atoi(v); i++; }
        else if (val("--sound", "adlib")) st.sound = Sound::Adlib;
        else if (val("--sound", "speaker")) st.sound = Sound::Speaker;
        else if (is("--original")) st.set_original();
        else if (is("--enhanced")) st.set_enhanced();
        else if (val("--view", "original")) st.hires_view = false;
        else if (val("--view", "hires")) st.hires_view = true;
        else if (val("--motion", "original")) st.smooth_motion = false;
        else if (val("--motion", "smooth")) st.smooth_motion = true;
        else if (val("--widescreen", "off")) st.widescreen = false;
        else if (val("--widescreen", "on")) st.widescreen = true;
        else if (val("--aspect", "4:3")) st.aspect = Aspect::Crt43;
        else if (val("--aspect", "square")) st.aspect = Aspect::Square;
        else if (val("--filter", "sharp")) st.filter = Filter::Sharp;
        else if (val("--filter", "nearest")) st.filter = Filter::Nearest;
        else if (val("--filter", "smooth")) st.filter = Filter::Smooth;
        else if (val("--filter", "crt")) st.filter = Filter::Crt;
        else if (is("--launcher")) force_launcher = true;
        else if (is("--no-launcher")) no_launcher = true;
        else if (is("--check")) check = true;
        else if (is("--host-test")) host_test = true;
        else if (is("--help") || is("-h")) {
            usage(argv[0]);
            return 0;
        }
        else return usage(argv[0]);
    }
    if (st.game_dir.empty()) st.game_dir = ".";
    {  // saved and shown as an absolute path
        std::error_code ec;
        const auto abs = std::filesystem::absolute(st.game_dir, ec);
        if (!ec) st.game_dir = abs.lexically_normal().string();
    }
    host_set_frame_rate(st.fps);

    if (host_test) {
        if (!host_init(st.game_dir.c_str(), st.window_scale, false)) return 1;
        vga_init();
        bool ok = host_test_rate("menus", PIT_DIV_MENU, 1.0);
        ok = host_test_rate("missions", PIT_DIV_MISSION, 0.5) && ok;
        ok = host_test_rate("BIOS", PIT_DIV_BIOS, 0.6) && ok;
        host_shutdown();
        return ok ? 0 : 1;
    }

    // The launcher opens the window first; the settings it returns are saved.
    bool window = false;
    if (!check && (force_launcher || (st.launcher && !no_launcher))) {
        if (!host_init(st.game_dir.c_str(), st.window_scale, st.fullscreen)) return 1;
        window = true;
        if (!launcher_run(st)) {
            host_shutdown();
            return 0;
        }
        settings_save(st);
        host_set_frame_rate(st.fps);
    }

    const std::string exe = find_game_file(st.game_dir, "GB.EXE");
    ExeInfo info;
    std::string err;
    if (!mem_load_exe(exe, info, err)) return fail(window, err);
    if (check) {
        std::printf("GB.EXE ok (%s): image %u bytes, %u relocations, at %04X:0000, DGROUP %04X\n",
                    info.packed ? "EXEPACK" : "unpacked", unsigned(info.image_size), unsigned(info.relocations),
                    LOAD_SEG, DGROUP);
        return 0;
    }

    // AdLib: DOS loads ADLIB.COM below GB.EXE (checked before the game's window opens).
    bool adlib = false;
    if (st.sound != Sound::Speaker) {
        const std::string com = find_game_file(st.game_dir, "ADLIB.COM");
        std::error_code ec;
        const bool found = std::filesystem::exists(com, ec);
        if (st.sound == Sound::Adlib && !found)
            return fail(window, "--sound adlib: the game folder has no ADLIB.COM (the Ad Lib sound driver)");
        if (found) {
            if (!adlib_load(com, err)) return fail(window, err);
            adlib = true;
        }
    }

    // The machine as DOS leaves it to GB.EXE, then the program (it ends through the runtime's exit).
    dos_heap_init();
    bios_init();
    if (!window && !host_init(st.game_dir.c_str(), st.window_scale, st.fullscreen)) return 1;
    host_set_game_dir(st.game_dir.c_str());
    vga_init();
    enhanced_install(st);
    host_set_kbd_handler(kbd_byte);
    host_set_focus_lost_handler(kbd_focus_lost);
    host_reset_clock();
    host_set_timer(PIT_DIV_BIOS, timer_interrupt);
    if (adlib) adlib_install();  // ADLIB.COM runs and stays resident: INT 65h, INT 8, the OPL2 set up
    game_main();
}
