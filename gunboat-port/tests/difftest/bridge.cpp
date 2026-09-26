// Differential-test bridge: the port's core as a DLL (gb_difftest) that gbdiff.py drives next to
// the original code in Unicorn. The ported functions register themselves in bridge_*.cpp
// (bridge.hpp); this file holds the registry and the exports.
#include "bridge.hpp"

#include <map>
#include <string>

#include "host.hpp"
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
GB_EXPORT void gb_set_pit2(int v) { host_stub_set_pit2(u8(v)); }

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

// The timer model of a test (gbdiff.Harness.set_tick): host_pump() runs one tick of this kind.
namespace {
void tick_counter() { ds_u16(0x08C0) = u16(ds_u16(0x08C0) + 1); }
// Keys that arrive during a call: one per tick, into isr_key_code (0 = no key at that tick).
std::string key_queue;
int tick_kind;
void test_tick()
{
    if (tick_kind == 1 || tick_kind == 3) tick_counter();
    if (tick_kind == 2 || tick_kind == 3) bios_tick();
    if (!key_queue.empty()) {
        const u8 k = u8(key_queue.front());
        key_queue.erase(0, 1);
        if (k) ds_u8(0xDA42) = k;
    }
}
} // namespace
GB_EXPORT void gb_set_keys(const u8 *keys, int n) { key_queue.assign(reinterpret_cast<const char *>(keys), size_t(n)); }
GB_EXPORT void gb_set_tick(int kind)
{
    tick_kind = kind;
    host_stub_set_test_tick(kind || !key_queue.empty() ? test_tick : nullptr);
}
GB_EXPORT int gb_timer_divisor() { return host_stub_timer_divisor(); }

// The speaker calls (host_speaker) since the last clear: n pairs (divisor, on) into out[2n].
GB_EXPORT void gb_speaker_clear() { host_stub_speaker_clear(); }
GB_EXPORT int gb_speaker_log(u16 *out, int max)
{
    const auto &log = host_stub_speaker_log();
    int n = 0;
    for (const auto &e : log) {
        if (n >= max) break;
        out[2 * n] = e.divisor;
        out[2 * n + 1] = e.on ? 1 : 0;
        n++;
    }
    return int(log.size());
}

// DOS files: close everything, open a game file, and the position of a handle (-1 = closed).
GB_EXPORT void gb_dos_reset() { dos_close_all(); }
GB_EXPORT int gb_dos_open(const char *name, const char *mode) { return dos_open_name(name, mode, false); }
GB_EXPORT int gb_dos_tell(int fh) { return dos_tell(s16(fh)); }

// The DAC writes of a call, in order (index, r, g, b): gb_dac_trace_start before the call.
namespace {
std::string dac_log;
void dac_logger(u8 i, u8 r, u8 g, u8 b) { dac_log += std::string{char(i), char(r), char(g), char(b)}; }
} // namespace
GB_EXPORT void gb_dac_trace_start()
{
    dac_log.clear();
    vga_set_dac_trace(dac_logger);
}
GB_EXPORT int gb_dac_trace_size() { return int(dac_log.size()); }
GB_EXPORT const char *gb_dac_trace_data() { return dac_log.data(); }

// The VGA DAC, 256 x RGB 6-bit, for tests of palette code.
GB_EXPORT void gb_dac_read(u8 *rgb768)
{
    for (int i = 0; i < 256; i++) vga_dac_read(u8(i), &rgb768[3 * i], &rgb768[3 * i + 1], &rgb768[3 * i + 2]);
}
GB_EXPORT void gb_dac_write(const u8 *rgb768)
{
    for (int i = 0; i < 256; i++) vga_dac_write(u8(i), rgb768[3 * i], rgb768[3 * i + 1], rgb768[3 * i + 2]);
}
