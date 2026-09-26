#pragma once
#include "assets.hpp"
namespace gb {
struct Options {
    std::filesystem::path directory;
    int world = 1, scale = 3;
    bool fullscreen = false, practice = false;
    double seconds = 0;
    std::string stateFile, screenshot;
};
int play(const Archive &, const Options &);
void self_test(const Archive &);
void dump_assets(const Archive &, const std::filesystem::path &);
} // namespace gb
