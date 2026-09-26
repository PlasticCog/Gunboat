// Differential-test bridge: the port's core as a DLL (gb_difftest) that gbdiff.py drives next to
// the original code in Unicorn. The ported functions register themselves in bridge_*.cpp
// (bridge.hpp); this file holds the registry and the exports.
#include "bridge.hpp"

#include <map>
#include <string>

#include "host_stub.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "platform/vga.hpp"

#if defined(_WIN32)
#define GB_EXPORT extern "C" __declspec(dllexport)
#else
#define GB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace gb;

namespace {

std::map<std::string, BridgeCall> &registry()
{
    static std::map<std::string, BridgeCall> r;
    return r;
}

std::string last_error;

} // namespace

gb::BridgeEntry::BridgeEntry(const char *name, BridgeCall call) { registry()[name] = call; }

GB_EXPORT u8 *gb_mem() { return mem; }
GB_EXPORT u32 gb_mem_size() { return MEM_SIZE; }
GB_EXPORT const char *gb_error() { return last_error.c_str(); }
GB_EXPORT void gb_set_game_dir(const char *dir) { host_stub_set_game_dir(dir); }

// Loads GB.EXE into mem[] as the game does at start-up. Returns 1, or 0 with gb_error().
GB_EXPORT int gb_load_exe(const char *path)
{
    ExeInfo info;
    return mem_load_exe(path, info, last_error) ? 1 : 0;
}

GB_EXPORT int gb_has(const char *name) { return registry().count(name) ? 1 : 0; }

// Runs one ported function on mem[]. Returns 1; 0 if the function is unknown or the port stopped
// (a fatal error such as a divide error, or the program's exit), with the message in gb_error().
GB_EXPORT int gb_call(const char *name, Regs *regs, const u16 *stack_args)
{
    const auto it = registry().find(name);
    if (it == registry().end()) {
        last_error = std::string("not ported: ") + name;
        return 0;
    }
    try {
        it->second(*regs, stack_args);
    } catch (const HostFatal &e) {
        last_error = e.what();
        return 0;
    } catch (const HostExit &e) {
        last_error = e.what();
        return 0;
    }
    return 1;
}

// DOS files: close everything, open a game file, and the position of a handle (-1 = closed).
GB_EXPORT void gb_dos_reset() { dos_close_all(); }
GB_EXPORT int gb_dos_open(const char *name, const char *mode) { return dos_open_name(name, mode, false); }
GB_EXPORT int gb_dos_tell(int fh) { return dos_tell(s16(fh)); }

// The VGA DAC, 256 x RGB 6-bit, for tests of palette code.
GB_EXPORT void gb_dac_read(u8 *rgb768)
{
    for (int i = 0; i < 256; i++) vga_dac_read(u8(i), &rgb768[3 * i], &rgb768[3 * i + 1], &rgb768[3 * i + 2]);
}
GB_EXPORT void gb_dac_write(const u8 *rgb768)
{
    for (int i = 0; i < 256; i++) vga_dac_write(u8(i), rgb768[3 * i], rgb768[3 * i + 1], rgb768[3 * i + 2]);
}
