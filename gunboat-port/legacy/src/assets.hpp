#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gb {
using Bytes = std::vector<uint8_t>;
uint16_t word(const Bytes &, size_t);
Bytes read_file(const std::filesystem::path &);
void write_file(const std::filesystem::path &, const Bytes &);
Bytes unpack_exe(const Bytes &);
Bytes decode_lzw(const Bytes &);
std::pair<uint16_t, uint16_t> filename_key(const std::string &);
struct Record {
    uint16_t key0, key1;
    char bank;
    uint32_t offset, length;
};
class Archive {
    std::map<char, Bytes> banks;

  public:
    std::vector<Record> records;
    Bytes exe;
    explicit Archive(const std::filesystem::path &);
    Bytes get(const std::string &) const;
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Vertex {
    uint8_t x, h, y;
};
struct Face {
    std::array<int, 3> indices;
    uint8_t color;
};
struct TileObject {
    uint8_t kind, x, y, extra;
    uint16_t offset;
};
struct Tile {
    uint16_t offset;
    std::array<unsigned,2> groups{};
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    std::vector<std::array<int, 3>> lines;
    std::vector<TileObject> objects;
};
struct Object {
    int kind, heading, index;
    uint16_t x, y;
    bool scenery;
    Vec3 position;
    int cell = -1;
};
struct Triangle {
    std::array<Vec3, 3> v;
    uint8_t color;
};
struct Line {
    std::array<Vec3, 2> v;
    uint8_t color;
};
struct Sprite {
    std::array<uint8_t, 256 * 64> pixels{};
};
struct SpriteBank {
    Bytes a, pixels;
    std::array<uint8_t, 16> facing;
    std::array<uint8_t, 20> scaleMasks;
    explicit SpriteBank(const Archive &, int world);
    Sprite decode(int kind, int angle) const;
};
struct World {
    int number;
    std::array<uint8_t, 187> cells;
    std::array<std::array<uint8_t, 3>, 32> palette;
    std::vector<Tile> tiles;
    std::vector<Object> objects;
    std::vector<Triangle> triangles;
    std::vector<Line> lines;
    SpriteBank sprites;
    World(const Archive &, int number);
};
std::pair<int, int> rotate(int x, int y, int rotation);
uint8_t daytime_color(uint8_t control, int vertex);
} // namespace gb
