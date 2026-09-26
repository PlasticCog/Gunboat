#pragma once
// Registration of ported functions for the differential tests (bridge.cpp, gbdiff.py).
//
//   BRIDGE(heading_vector) { r.ax = heading_vector(u8(r.ax)); }
//   BRIDGE(dos_seek)       { r.ax = u16(dos_seek(s16(a[0]), a[1], a[2])); }
//
// The name is the function's symbols.csv name; r is the register file (in and out), a[] the stack
// arguments of a C function as pushed words, first argument first (a far pointer is two words:
// offset, segment).
#include "types.hpp"

namespace gb {

struct Regs {
    u16 ax, bx, cx, dx, si, di, bp, es;
};

using BridgeCall = void (*)(Regs &r, const u16 *a);

struct BridgeEntry {
    BridgeEntry(const char *name, BridgeCall call);
};

} // namespace gb

#define GB_BRIDGE_CAT2(a, b) a##b
#define GB_BRIDGE_CAT(a, b) GB_BRIDGE_CAT2(a, b)
#define BRIDGE(name)                                                                                   \
    static void GB_BRIDGE_CAT(bridge_fn_, name)([[maybe_unused]] gb::Regs & r,                      \
                                               [[maybe_unused]] const gb::u16 *a);                   \
    static const gb::BridgeEntry GB_BRIDGE_CAT(bridge_entry_, name)(#name, GB_BRIDGE_CAT(bridge_fn_, name)); \
    static void GB_BRIDGE_CAT(bridge_fn_, name)([[maybe_unused]] gb::Regs & r, [[maybe_unused]] const gb::u16 *a)
