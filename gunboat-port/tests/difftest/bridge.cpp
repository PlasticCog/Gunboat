// Differential-test bridge: the port's core as a DLL (gb_difftest) that gbdiff.py drives next to
// the original code in Unicorn. Every ported function with a test is listed in `functions`, under
// its symbols.csv name, with the registers it takes and returns.
#include "game/sim.hpp"
#include "host_stub.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "platform/vga.hpp"

#include <map>
#include <string>

#if defined(_WIN32)
#define GB_EXPORT extern "C" __declspec(dllexport)
#else
#define GB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace gb;

// The register file a test passes in and reads back (same layout as gbdiff.Regs). A function reads
// only its argument registers and writes only the ones it returns; the rest come back unchanged.
struct Regs {
    u16 ax, bx, cx, dx, si, di, bp, es;
};

namespace {

using Call = void (*)(Regs &r, const u16 *stack_args);

const std::map<std::string, Call> functions = {
    {"random", [](Regs &r, const u16 *) { r.ax = random(); }},
    {"vec_scale", [](Regs &r, const u16 *) { r.ax = vec_scale(u8(r.ax)); }},
    {"heading_vector", [](Regs &r, const u16 *) { r.ax = heading_vector(u8(r.ax)); }},
    {"boat_move", [](Regs &, const u16 *) { boat_move(); }},
};

std::string last_error;

} // namespace

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

GB_EXPORT int gb_has(const char *name) { return functions.count(name) ? 1 : 0; }

// Runs one ported function on mem[]. Returns 1; 0 if the function is unknown or the port stopped
// with a fatal error (a divide error, for instance), with the message in gb_error().
GB_EXPORT int gb_call(const char *name, Regs *regs, const u16 *stack_args)
{
    const auto it = functions.find(name);
    if (it == functions.end()) {
        last_error = std::string("not ported: ") + name;
        return 0;
    }
    try {
        it->second(*regs, stack_args);
    } catch (const HostFatal &e) {
        last_error = e.what();
        return 0;
    }
    return 1;
}

// The VGA DAC, 256 x RGB 6-bit, for tests of palette code.
GB_EXPORT void gb_dac_read(u8 *rgb768)
{
    for (int i = 0; i < 256; i++) vga_dac_read(u8(i), &rgb768[3 * i], &rgb768[3 * i + 1], &rgb768[3 * i + 2]);
}
GB_EXPORT void gb_dac_write(const u8 *rgb768)
{
    for (int i = 0; i < 256; i++) vga_dac_write(u8(i), rgb768[3 * i], rgb768[3 * i + 1], rgb768[3 * i + 2]);
}
