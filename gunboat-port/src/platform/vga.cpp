// Mode 13h VGA model (vga.hpp). Converted to C++ from the Test Drive III port's platform/vga.c
// (MIT, (c) 2026 Krzysztof Kania; THIRD_PARTY.md). The DAC and CRTC state is hardware, not game
// memory, so it lives here rather than in mem[].
#include "platform/vga.hpp"

#include <cstring>

#include "host.hpp"
#include "mem.hpp"

namespace gb {

namespace {

u8 dac[256][3];
u16 start;
bool dirty = true;

// Last composed input, to report "changed" only when the screen or the DAC really changed.
u8 last_vram[65536];
u8 last_dac[256][3];
u16 last_start = 0xFFFF;

bool compose(u32 *xrgb)
{
    const u8 *vram = mp(VRAM_SEG, 0);
    if (!dirty && start == last_start && std::memcmp(vram, last_vram, sizeof last_vram) == 0 &&
        std::memcmp(dac, last_dac, sizeof dac) == 0)
        return false;
    dirty = false;
    std::memcpy(last_vram, vram, sizeof last_vram);
    std::memcpy(last_dac, dac, sizeof dac);
    last_start = start;

    u32 pal[256];
    for (int i = 0; i < 256; i++) {
        // 6-bit DAC value to 8 bits: v << 2 | v >> 4, as VGA-to-RGB conversions usually do.
        const u32 r = u32(dac[i][0] << 2 | dac[i][0] >> 4);
        const u32 g = u32(dac[i][1] << 2 | dac[i][1] >> 4);
        const u32 b = u32(dac[i][2] << 2 | dac[i][2] >> 4);
        pal[i] = r << 16 | g << 8 | b;
    }
    for (int i = 0; i < 320 * 200; i++) xrgb[i] = pal[vram[u16(start + i)]];
    return true;
}

} // namespace

void vga_init()
{
    std::memset(dac, 0, sizeof dac);
    start = 0;
    dirty = true;
    host_set_frame_source(compose, 320, 200);
}

void vga_dac_write(u8 index, u8 r, u8 g, u8 b)
{
    dac[index][0] = r & 0x3F;
    dac[index][1] = g & 0x3F;
    dac[index][2] = b & 0x3F;
}

void vga_dac_read(u8 index, u8 *r, u8 *g, u8 *b)
{
    if (r) *r = dac[index][0];
    if (g) *g = dac[index][1];
    if (b) *b = dac[index][2];
}

void vga_set_start(u16 byte_offset) { start = byte_offset; }
u16 vga_start() { return start; }

} // namespace gb
