// Real-mode memory and the GB.EXE loader. The EXEPACK decoder follows the Test Drive III port's
// mem.c (MIT, (c) 2026 Krzysztof Kania; THIRD_PARTY.md), itself a port of tools/unexepack.py.
#include "mem.hpp"

#include "host.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace gb {

u8 mem[MEM_SIZE];

namespace {

constexpr u32 GB_IMAGE_SIZE = 0x2A110;  // unpacked load image of the shipped GB.EXE
constexpr u32 GB_RELOCATIONS = 1584;

u16 le16(const u8 *p) { return u16(p[0] | p[1] << 8); }

struct Reloc {
    u16 seg, off;
};

// Microsoft EXEPACK: decompresses the load image backwards and returns the relocation table kept in
// the unpacker stub (at CS:0000 of the packed file).
bool exepack_unpack(const std::vector<u8> &body, u16 cs, std::vector<u8> &image,
                    std::vector<Reloc> &relocs, std::string &err)
{
    const size_t ep = size_t(cs) * 16;
    if (ep + 18 > body.size()) { err = "EXEPACK header out of range"; return false; }
    const size_t sig = std::memcmp(&body[ep + 16], "RB", 2) == 0 ? ep + 16 : ep + 14;
    if (std::memcmp(&body[sig], "RB", 2) != 0) { err = "not an EXEPACK file"; return false; }
    const u16 exepack_size = le16(&body[ep + 6]);
    const u16 dest_len = le16(&body[ep + 12]);
    const u16 skip_len = sig == ep + 16 ? le16(&body[ep + 14]) : 1;

    static const char msg[] = "Packed file is corrupt";
    const size_t stub_len = std::min<size_t>(exepack_size, body.size() - ep);
    const u8 *stub = &body[ep];
    size_t p = 0;
    for (size_t i = 0; i + sizeof msg - 1 <= stub_len; i++)
        if (std::memcmp(stub + i, msg, sizeof msg - 1) == 0) { p = i + sizeof msg - 1; break; }
    if (!p) { err = "EXEPACK relocation table not found"; return false; }
    for (int seg = 0; seg < 16; seg++) {
        if (p + 2 > stub_len) { err = "EXEPACK relocation table truncated"; return false; }
        const u16 n = le16(stub + p);
        p += 2;
        for (u16 k = 0; k < n; k++, p += 2) {
            if (p + 2 > stub_len) { err = "EXEPACK relocation table truncated"; return false; }
            relocs.push_back({u16(seg * 0x1000), le16(stub + p)});
        }
    }

    const size_t packed_len = ep - size_t(skip_len - 1) * 16;
    const size_t unpacked_len = size_t(dest_len) * 16;
    std::vector<u8> buf(std::max(packed_len, unpacked_len));
    std::memcpy(buf.data(), body.data(), packed_len);
    size_t src = packed_len, dst = unpacked_len;
    while (src > 0 && buf[src - 1] == 0xFF) src--;
    for (;;) {
        if (src < 3) { err = "EXEPACK data is corrupt"; return false; }
        const u8 cmd = buf[--src];
        const u16 len = u16(buf[src - 1] << 8 | buf[src - 2]);
        src -= 2;
        if ((cmd & 0xFE) == 0xB0) {
            if (src < 1 || dst < len) { err = "EXEPACK data is corrupt"; return false; }
            const u8 fill = buf[--src];
            dst -= len;
            std::memset(&buf[dst], fill, len);
        } else if ((cmd & 0xFE) == 0xB2) {
            if (src < len || dst < len) { err = "EXEPACK data is corrupt"; return false; }
            src -= len;
            dst -= len;
            std::memmove(&buf[dst], &buf[src], len);
        } else {
            err = "EXEPACK data is corrupt";
            return false;
        }
        if (cmd & 1) break;
    }
    buf.resize(unpacked_len);
    image = std::move(buf);
    return true;
}

} // namespace

bool mem_load_exe(const std::string &path, ExeInfo &info, std::string &err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot read " + path; return false; }
    const std::vector<u8> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < 0x1C || le16(&file[0]) != 0x5A4D) { err = path + " is not a DOS executable"; return false; }
    const u16 cblp = le16(&file[2]), cp = le16(&file[4]), crlc = le16(&file[6]), cparhdr = le16(&file[8]);
    const u16 cs = le16(&file[0x16]), lfarlc = le16(&file[0x18]);
    size_t mz_len = size_t(cp) * 512 - (cblp ? 512 - cblp : 0);
    const size_t hdr_len = size_t(cparhdr) * 16;
    mz_len = std::min(mz_len, file.size());
    if (hdr_len >= mz_len) { err = path + ": bad MZ header"; return false; }
    const std::vector<u8> body(file.begin() + hdr_len, file.begin() + mz_len);

    std::vector<u8> image;
    std::vector<Reloc> relocs;
    const size_t ep = size_t(cs) * 16;
    info.packed = ep + 18 <= body.size() &&
                  (std::memcmp(&body[ep + 14], "RB", 2) == 0 || std::memcmp(&body[ep + 16], "RB", 2) == 0);
    if (info.packed) {
        if (!exepack_unpack(body, cs, image, relocs, err)) { err = path + ": " + err; return false; }
    } else {
        image = body;
        for (u16 i = 0; i < crlc && lfarlc + 4u * i + 4 <= file.size(); i++)
            relocs.push_back({le16(&file[lfarlc + 4 * i + 2]), le16(&file[lfarlc + 4 * i])});
    }
    if (image.size() != GB_IMAGE_SIZE || relocs.size() != GB_RELOCATIONS) {
        err = path + ": unexpected load image (" + std::to_string(image.size()) + " bytes, " +
              std::to_string(relocs.size()) + " relocations); expected GB.EXE of Gunboat (Accolade, 1990)";
        return false;
    }

    std::memset(mem, 0, sizeof mem);
    std::memcpy(mp(LOAD_SEG, 0), image.data(), image.size());
    const u32 base = lin(LOAD_SEG, 0);
    for (const Reloc &r : relocs) {
        const u32 at = base + lin(r.seg, r.off);
        if (at + 2 > base + image.size()) continue;
        const u16 v = u16(mem[at] | mem[at + 1] << 8) + LOAD_SEG;
        mem[at] = u8(v);
        mem[at + 1] = u8(v >> 8);
    }

    // Identify the build by strings at fixed DGROUP offsets (game_flow.md §1).
    if (std::memcmp(mp(DGROUP, 0x0123), "GUNBOAT.CFG", 12) != 0 ||
        std::memcmp(mp(DGROUP, 0x0A0E), "GBROSTER.DAT", 13) != 0 ||
        std::memcmp(mp(DGROUP, 0xE72E), "R6003", 5) != 0) {
        err = path + " is not the expected GB.EXE (Gunboat, Accolade 1990)";
        return false;
    }
    info.image_size = u32(image.size());
    info.relocations = u32(relocs.size());
    return true;
}

void div_error()
{
    // MSC 5.1 runtime INT 0 handler: prints R6003 (DS:E72E) and exits.
    host_fatal("run-time error R6003\n- integer divide by 0");
}

} // namespace gb
