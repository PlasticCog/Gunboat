#include "assets.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace gb {
static void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
uint16_t word(const Bytes &b, size_t p) {
    require(p + 2 <= b.size(), "Truncated 16-bit asset field");
    return uint16_t(b[p] | b[p + 1] << 8);
}
static uint32_t dword(const Bytes &b, size_t p) {
    return uint32_t(word(b, p)) | (uint32_t(word(b, p + 2)) << 16);
}
Bytes read_file(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    require(bool(f), "Cannot read " + path.string());
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
void write_file(const std::filesystem::path &path, const Bytes &b) {
    std::ofstream f(path, std::ios::binary);
    require(bool(f), "Cannot write " + path.string());
    f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    require(bool(f), "Write failed: " + path.string());
}
static std::filesystem::path find_file(const std::filesystem::path &dir, std::string name) {
    for (const auto &e : std::filesystem::directory_iterator(dir)) {
        auto n = e.path().filename().string();
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
        if (n == name)
            return e.path();
    }
    throw std::runtime_error("Missing original game file: " + name);
}

// Adapted from TD3's mem.c EXEPACK unpacker (MIT, Krzysztof Kania).
// Decodes data only: no DOS instructions or CPU are executed.
Bytes unpack_exe(const Bytes &file) {
    require(file.size() >= 28 && word(file, 0) == 0x5a4d, "Invalid GB.EXE MZ header");
    const size_t pages = word(file, 4), tail = word(file, 2), header = size_t(word(file, 8)) * 16;
    const size_t total = pages * 512 - (tail ? 512 - tail : 0);
    require(header < total && total <= file.size(), "Invalid MZ size");
    Bytes image(file.begin() + header, file.begin() + total);
    const size_t ep = size_t(word(file, 22)) * 16;
    std::vector<size_t> reloc;
    if (ep + 18 <= image.size() &&
        (word(image, ep + 14) == 0x4252 || word(image, ep + 16) == 0x4252)) {
        const bool newer = word(image, ep + 16) == 0x4252;
        const size_t stubsize = word(image, ep + 6), dest = size_t(word(image, ep + 12)) * 16;
        const unsigned skip = newer ? word(image, ep + 14) : 1;
        require(skip >= 1 && ep >= (skip - 1) * 16u && ep + stubsize <= image.size(),
                "Invalid EXEPACK header");
        const std::string marker = "Packed file is corrupt";
        const auto at = std::search(image.begin() + ep, image.begin() + ep + stubsize,
                                    marker.begin(), marker.end());
        require(at != image.begin() + ep + stubsize, "Missing EXEPACK relocation table");
        size_t p = size_t(at - image.begin()) + marker.size();
        for (size_t seg = 0; seg < 16; seg++) {
            unsigned count = word(image, p);
            p += 2;
            require(p + count * 2 <= ep + stubsize, "Truncated relocation table");
            for (unsigned j = 0; j < count; j++, p += 2)
                reloc.push_back(seg * 65536 + word(image, p));
        }
        const size_t packed = ep - (skip - 1) * 16u;
        require(dest > 0 && dest <= 0x40000, "Unexpected unpacked GB.EXE size");
        Bytes out(std::max(packed, dest));
        std::copy_n(image.begin(), packed, out.begin());
        size_t src = packed, dst = dest;
        while (src && out[src - 1] == 255)
            --src;
        for (;;) {
            require(src >= 3, "Truncated EXEPACK command");
            uint8_t cmd = out[--src];
            const size_t len = word(out, src - 2);
            src -= 2;
            require(dst >= len, "EXEPACK output underflow");
            if ((cmd & 254) == 0xb0) {
                require(src > 0, "Truncated fill");
                uint8_t c = out[--src];
                dst -= len;
                std::fill_n(out.begin() + dst, len, c);
            } else if ((cmd & 254) == 0xb2) {
                require(src >= len, "Truncated literal");
                src -= len;
                dst -= len;
                std::memmove(out.data() + dst, out.data() + src, len);
            } else
                throw std::runtime_error("Unknown EXEPACK command");
            if (cmd & 1)
                break;
        }
        out.resize(dest);
        image = std::move(out);
    } else {
        size_t p = word(file, 24);
        for (unsigned i = 0; i < word(file, 6); i++, p += 4)
            reloc.push_back(size_t(word(file, p + 2)) * 16 + word(file, p));
    }
    for (size_t p : reloc) {
        require(p + 2 <= image.size(), "Relocation outside load image");
        const auto v = uint16_t(word(image, p) + 0x1000);
        image[p] = uint8_t(v);
        image[p + 1] = uint8_t(v >> 8);
    }
    require(image.size() > 0x28fe7, "GB.EXE is too small for the known sprite tables");
    const std::string signature = "GUNBOAT.CFG";
    require(std::search(image.begin(), image.end(), signature.begin(), signature.end()) !=
                image.end(),
            "Unsupported Gunboat executable");
    return image;
}

// TD3 platform/pic.c dictionary semantics, expressed with bounded C++ storage.
Bytes decode_lzw(const Bytes &input) {
    std::array<uint16_t, 4096> prefix{};
    std::array<uint8_t, 4096> suffix{};
    Bytes output;
    size_t bit = 0;
    int width = 9, next = 258, old = -1;
    uint8_t first = 0;
    auto code = [&]() {
        require(bit + width <= input.size() * 8, "Truncated LZW picture");
        unsigned v = 0;
        for (int n = 0; n < width; n++)
            v |= ((input[(bit + n) / 8] >> ((bit + n) % 8)) & 1) << n;
        bit += width;
        return int(v);
    };
    for (;;) {
        int c = code();
        if (c == 257)
            break;
        if (c == 256) {
            width = 9;
            next = 258;
            old = -1;
            continue;
        }
        if (old < 0) {
            require(c < 256, "Invalid LZW first code");
            output.push_back(uint8_t(c));
            first = uint8_t(c);
            old = c;
            continue;
        }
        const int original = c;
        Bytes stack;
        if (c == next) {
            stack.push_back(first);
            c = old;
        } else
            require(c < next, "Invalid LZW dictionary reference");
        while (c >= 256) {
            require(c < 4096 && stack.size() < 4096, "Invalid LZW chain");
            stack.push_back(suffix[c]);
            c = prefix[c];
        }
        first = uint8_t(c);
        stack.push_back(first);
        output.insert(output.end(), stack.rbegin(), stack.rend());
        require(output.size() <= 1024 * 1024, "LZW output exceeds limit");
        if (next < 4096) {
            prefix[next] = uint16_t(old);
            suffix[next] = first;
            ++next;
            if (next >= (1 << width) && width < 12)
                ++width;
        }
        old = original;
    }
    return output;
}
std::pair<uint16_t, uint16_t> filename_key(const std::string &name) {
    uint16_t h0 = 0, h1 = 0;
    for (size_t i = 0; i + 1 < name.size(); i++)
        h0 = uint16_t(h0 + i * uint8_t(name[i]));
    for (auto i = name.rbegin(); i != name.rend(); ++i)
        h1 = uint16_t(h1 * 257 + uint8_t(*i));
    return {h0, h1};
}
Archive::Archive(const std::filesystem::path &dir) {
    exe = unpack_exe(read_file(find_file(dir, "GB.EXE")));
    banks['a'] = read_file(find_file(dir, "DATAA.DAT"));
    banks['b'] = read_file(find_file(dir, "DATAB.DAT"));
    const auto table = read_file(find_file(dir, "DATAC.DAT"));
    for (size_t p = 0; p + 14 <= table.size(); p += 14) {
        if (!word(table, p) && !word(table, p + 2))
            break;
        Record r{word(table, p), word(table, p + 2), char(table[p + 4]), dword(table, p + 6),
                 dword(table, p + 10)};
        require(banks.count(r.bank) && uint64_t(r.offset) + r.length <= banks.at(r.bank).size(),
                "Asset record exceeds archive");
        records.push_back(r);
    }
    require(records.size() == 84, "Expected 84 Gunboat archive records");
}
Bytes Archive::get(const std::string &name) const {
    const auto hash = filename_key(name);
    for (const auto &r : records)
        if (r.key0 == hash.first && r.key1 == hash.second) {
            const auto &b = banks.at(r.bank);
            return Bytes(b.begin() + r.offset, b.begin() + r.offset + r.length);
        }
    throw std::runtime_error("Unknown archived file: " + name);
}
std::pair<int, int> rotate(int x, int y, int r) {
    switch (r & 3) {
    case 1:
        return {y, 128 - x};
    case 2:
        return {128 - x, 128 - y};
    case 3:
        return {128 - y, x};
    default:
        return {x, y};
    }
}
uint8_t daytime_color(uint8_t control, int vertex) {
    int c = control & 127, alt = vertex % 2 ? 0 : 31;
    if (c == 2)
        c = 10 ^ (alt & 24);
    else if (c == 6)
        c = 6 ^ ((alt & 16) ^ 4);
    return uint8_t(c & 63);
}
World::World(const Archive &archive, int n) : number(n), sprites(archive, n) {
    require(n >= 1 && n <= 4, "World must be 1..4");
    const auto raw = archive.get("DAT" + std::to_string(n) + ".DAT"),
               tile = archive.get("TILE.BIN"), pal = archive.get("TACTCOLR.BIN");
    require(raw.size() == 6468 && pal.size() >= 96, "Unexpected world/palette size");
    std::copy_n(raw.begin() + 2, 187, cells.begin());
    for (int i = 0; i < 32; i++)
        for (int c = 0; c < 3; c++)
            palette[i][c] = pal[i * 3 + c];
    for (int id = 0; id < 34; id++) {
        Tile t;
        t.offset = word(tile, id * 2);
        const size_t p = t.offset;
        require(p + 4 <= tile.size(), "Invalid tile offset");
        const int a = tile[p], b = tile[p + 1], count = a + b, oc = tile[p + 2];
        t.groups={unsigned(a),unsigned(b)};
        require(p + 4 + count * 4 + oc * 4 + tile[p + 3] * 3 <= tile.size(), "Truncated tile");
        for (int i = 0; i < count; i++)
            t.vertices.push_back({tile[p + 4 + count * 2 + i], tile[p + 4 + count + i],
                                  tile[p + 4 + count * 3 + i]});
        for (const auto group : {std::pair<int, int>{0, a}, {a, b}})
            for (int i = group.first; i < group.first + group.second; i++) {
                uint8_t c = tile[p + 4 + i];
                if (!(c & 63))
                    continue;
                int mode = c >> 6;
                if (mode == 1) {
                    require(i + 1 < group.first + group.second, "Invalid tile line");
                    t.lines.push_back({i, i + 1, daytime_color(c, i)});
                } else {
                    require(mode == 0 || mode == 2, "Unknown tile command");
                    int start = i - mode / 2;
                    require(start >= group.first && i + 2 < group.first + group.second,
                            "Invalid tile triangle");
                    t.faces.push_back({{start, i + 1, i + 2}, daytime_color(c, i)});
                }
            }
        for (int i = 0; i < oc; i++) {
            size_t q = p + 4 + count * 4 + i * 4;
            t.objects.push_back({tile[q], tile[q + 1], tile[q + 2], tile[q + 3], uint16_t(q)});
        }
        tiles.push_back(std::move(t));
    }
    auto add = [&](int kind, int heading, int index, int x, int y, bool scenery) {
        objects.push_back({kind,
                           heading,
                           index,
                           uint16_t(x),
                           uint16_t(y),
                           scenery,
                           {x / 64.f - 136, 0, 88 - y / 64.f}});
    };
    const unsigned limit = word(raw, 269);
    require(limit <= 1000, "Invalid object boundary");
    for (unsigned i = 0; i < limit; i++) {
        unsigned type = word(raw, 273 + i * 2);
        if (type & 255)
            add(type & 255, (type >> 8) & 7, int(i), word(raw, 2273 + i * 2),
                word(raw, 4273 + i * 2), false);
    }
    for (int i = 0; i < 187; i++) {
        int cell = cells[i], id = cell & 63, col = i % 17, row = i / 17;
        require(id < 34, "Invalid world tile ID");
        const auto &t = tiles[id];
        std::vector<Vec3> vertices;
        for (auto v : t.vertices) {
            auto [x, y] = rotate(v.x, v.y, cell >> 6);
            vertices.push_back(
                {(col * 128 + x - 1088) / 8.f, v.h / 32.f, (row * 128 + 128 - y - 704) / 8.f});
        }
        for (auto f : t.faces)
            triangles.push_back(
                {{vertices[f.indices[0]], vertices[f.indices[1]], vertices[f.indices[2]]},
                 f.color});
        for (auto line : t.lines)
            lines.push_back({{vertices[line[0]], vertices[line[1]]}, uint8_t(line[2])});
        for (size_t j = 0; j < t.objects.size(); j++) {
            auto o = t.objects[j];
            auto [x, y] = rotate(o.x, o.y, cell >> 6);
            add(o.kind, o.offset & 7, int(j), col * 1024 + (x & 255) * 8,
                (10 - row) * 1024 + (y & 255) * 8, true);
            objects.back().cell = i;
        }
    }
}

SpriteBank::SpriteBank(const Archive &archive, int world) {
    int pair = world % 4 + 1;
    const std::string base = "DAT" + std::to_string(pair);
    a = archive.get(base + "A.DAT");
    pixels = archive.get(base + "B.DAT");
    require(pixels.size() <= 0x1aac, "Sprite banks overlap");
    pixels.resize(0x1aac);
    pixels.insert(pixels.end(), a.begin(), a.end());
    std::copy_n(archive.exe.begin() + 0x28fd7, 16, facing.begin());
    std::copy_n(archive.exe.begin() + 0xeb3f, 20, scaleMasks.begin());
}
// Direct translations of GB image EC7B/F158/F503/F837/FA34 and EE01.
// Native decoding at cache scale 47; the runtime rasterizer handles screen size.
Sprite SpriteBank::decode(int kind, int angle) const {
    Sprite result;
    if (kind == 57)
        return result;
    require(kind >= 0 && size_t(kind * 8 + 8) <= a.size(), "Invalid sprite type");
    const int maskOffset = (kind == 58 || kind == 59) ? 10 : 0, view = ((angle + 2) & 255) >> 2;
    auto hb = [&](int i) {
        require(i >= 0, "Invalid sprite mask index");
        return (scaleMasks[maskOffset + (i / 8) % 7] >> (7 - i % 8)) & 1;
    };
    auto mb = [](int m, int i) { return (m >> (7 - (i & 7))) & 1; };
    auto part = [&](size_t p, int depth) {
        require(p + 8 <= a.size(), "Truncated sprite descriptor");
        const int w = a[p], h = a[p + 1], ptr = word(a, p + 2), lens = word(a, p + 4);
        require(h <= 24, "Sprite height exceeds row table");
        std::vector<Bytes> rows;
        for (int row = 0; row < h; row++) {
            int src = word(a, ptr + row * 2), cl = a.at(lens + row * 2),
                dl = a.at(lens + row * 2 + 1);
            Bytes out;
            auto emit = [&](uint8_t color, int i) {
                out.insert(out.end(), size_t(1 + hb(i)), color);
            };
            if (cl & 192) {
                const int len = cl & ((cl & 128) ? 127 : 63), pad = (w - len) / 2,
                          phase = (cl & 128) ? w * view / 32 : 0;
                require(pad >= 0, "Invalid sprite row length");
                if (cl & 128)
                    src += len * view / 32;
                for (int i = 0; i < pad; i++)
                    emit(0, phase + i);
                for (int i = 0; i < len; i++)
                    emit(pixels.at(src + i), phase + pad + i);
                for (int i = 0; i < pad; i++)
                    emit(0, phase + pad + len + i);
            } else {
                int turn = (view + 1) / 2, width = w, dep = depth;
                if (turn & 16)
                    src += cl + dl;
                if (turn & 8) {
                    src += cl;
                    std::swap(cl, dl);
                    std::swap(width, dep);
                }
                require(width >= cl && dep >= dl, "Invalid directional sprite row");
                const int m0 = facing[(turn & 7) * 2], m1 = facing[(turn & 7) * 2 + 1];
                int missing = 0;
                for (int i = 0; i < width - cl; i++)
                    missing += (1 + hb(i)) * mb(m0, i);
                for (int i = dl; i < dep; i++)
                    missing += (1 + hb(width + i)) * mb(m1, i);
                out.insert(out.end(), (missing + 1) / 2, 0);
                for (int i = 0; i < cl; i++)
                    if (mb(m0, width - cl + i))
                        emit(pixels.at(src + i), width - cl + i);
                for (int i = 0; i < dl; i++)
                    if (mb(m1, i))
                        emit(pixels.at(src + cl + i), width + i);
                out.insert(out.end(), missing / 2, 0);
            }
            int repeat = 1 + ((scaleMasks[maskOffset + 9 - row / 8] >> (7 - row % 8)) & 1);
            for (int i = 0; i < repeat; i++)
                rows.push_back(out);
        }
        return rows;
    };
    const size_t p = kind * 8, sub = word(a, 6) + a[p + 7] * 8;
    const auto main = part(p, a[p + 6]);
    const auto upper = a.at(sub) ? part(sub, a[p + 6]) : std::vector<Bytes>{};
    auto draw = [&](const std::vector<Bytes> &rows, int bottom, int shift) {
        if (rows.empty())
            return;
        int width = int(rows.back().size()),
            left = 2 * ((72 - ((width / 2 + shift) / 2) + 8) & 255) - 40;
        for (int y = 0; y < int(rows.size()); y++)
            for (int x = 0; x < int(rows[y].size()); x++) {
                const int yy = bottom - int(rows.size()) + y - 64, xx = left + x;
                if (xx >= 0 && xx < 256 && yy >= 0 && yy < 64 && rows[y][x])
                    result.pixels[yy * 256 + xx] = rows[y][x];
            }
    };
    draw(main, 120, 0);
    if (!upper.empty()) {
        const int ratio = (a.at(sub + 6) * 256 / a.at(sub)) & 255,
                  shift = (int(upper.back().size()) * ratio / 2) >> 8;
        draw(upper, 120 - int(main.size()), shift);
    }
    return result;
}
} // namespace gb
