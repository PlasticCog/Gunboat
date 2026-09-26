// Bridge entries: platform (platform.md) and the file and memory helpers of segment 0000
// (game_flow.md §1).
#include "bridge.hpp"
#include "game/flow.hpp"
#include "platform/platform.hpp"

using namespace gb;

namespace {
FarPtr far_arg(const u16 *a, int i) { return {a[i], a[i + 1]}; }
} // namespace

BRIDGE(random) { r.ax = random(); }

BRIDGE(dos_seek) { r.ax = u16(dos_seek(s16(a[0]), a[1], a[2])); }
BRIDGE(dos_open_read) { r.ax = u16(dos_open_read(a[0])); }
BRIDGE(dos_file_size) { r.ax = dos_file_size(s16(a[0])); }
BRIDGE(dos_read) { r.ax = dos_read(far_arg(a, 0), a[2], s16(a[3])); }
BRIDGE(dos_close) { dos_close(s16(a[0])); }

BRIDGE(lzw_alloc) { r.ax = u16(lzw_alloc()); }
BRIDGE(lzw_free) { lzw_free(); }

BRIDGE(name_hash_h1) { r.ax = name_hash_h1(a[0], a[1]); r.dx = 0; }
BRIDGE(name_hash_h2) { r.ax = name_hash_h2(a[0], a[1]); r.dx = 0; }
BRIDGE(name_hash)
{
    const u32 h = name_hash(a[0]);
    r.ax = u16(h);
    r.dx = u16(h >> 16);
}
BRIDGE(archive_open) { r.ax = u16(archive_open(a[0])); }
BRIDGE(file_load_near) { file_load_near(a[0], a[1]); }
BRIDGE(file_load_far) { file_load_far(a[0], far_arg(a, 1)); }
BRIDGE(mem_alloc_all) { mem_alloc_all(); }
BRIDGE(mem_free_all) { mem_free_all(); }
