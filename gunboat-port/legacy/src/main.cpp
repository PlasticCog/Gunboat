#define SDL_MAIN_HANDLED
#include "game.hpp"
#include "original_renderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <set>

int main(int argc, char **argv) {
    try {
        gb::Options options;
        options.directory = "Original DOS version";
        bool check = false, test = false, explicitDirectory = false;
        std::string dump,rendererInput,rendererOutput;
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            auto value = [&]() {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing value for " + a);
                return std::string(argv[++i]);
            };
            if (a == "--game-dir") {
                options.directory = value();
                explicitDirectory = true;
            } else if (a == "--world")
                options.world = std::stoi(value());
            else if (a == "--scale")
                options.scale = std::stoi(value());
            else if (a == "--fullscreen")
                options.fullscreen = true;
            else if (a == "--practice")
                options.practice = true;
            else if (a == "--seconds")
                options.seconds = std::stod(value());
            else if (a == "--state")
                options.stateFile = value();
            else if (a == "--screenshot")
                options.screenshot = value();
            else if (a == "--check")
                check = true;
            else if (a == "--self-test")
                test = true;
            else if (a == "--dump-assets")
                dump = value();
            else if (a == "--renderer-probe") {rendererInput=value();rendererOutput=value();}
            else if (a == "--help") {
                std::cout << "Gunboat native SDL3 port\n--game-dir DIR --world 1..4 --scale 1..6 "
                             "--fullscreen --practice\n--check --self-test --dump-assets DIR "
                             "--seconds N --state FILE --screenshot FILE.bmp\n";
                return 0;
            } else
                throw std::runtime_error("Unknown option: " + a);
        }
        if (options.world < 1 || options.world > 4 || options.scale < 1 || options.scale > 6 ||
            options.seconds < 0)
            throw std::runtime_error("Invalid world, window scale or duration");
        if (!explicitDirectory && !std::filesystem::exists(options.directory)) {
            const auto base = std::filesystem::absolute(argv[0]).parent_path();
            for (const auto &candidate : {base / "Game", base / "../../Original DOS version",
                                          base / "../../../Original DOS version"})
                if (std::filesystem::exists(candidate)) {
                    options.directory = candidate;
                    break;
                }
        }
        const gb::Archive archive(options.directory);
        if(!rendererInput.empty()){gb::original::renderer_probe(archive.exe,rendererInput,rendererOutput);return 0;}
        if (check) {
            for (int n = 1; n <= 4; n++) {
                gb::World world(archive, n);
                std::set<int> kinds;
                for (auto o : world.objects)
                    if (o.kind != 57)
                        kinds.insert(o.kind);
                for (int kind : kinds)
                    for (int angle = 0; angle < 256; angle += 32)
                        world.sprites.decode(kind, angle);
                std::cout << "World " << n << ": " << world.triangles.size() << " triangles, "
                          << world.objects.size() << " source objects, " << kinds.size()
                          << " sprite kinds OK\n";
            }
            std::cout << "84 archive records; GB.EXE unpacked natively to " << archive.exe.size()
                      << " bytes\n";
            return 0;
        }
        if (test) {
            gb::self_test(archive);
            return 0;
        }
        if (!dump.empty()) {
            gb::dump_assets(archive, dump);
            return 0;
        }
        SDL_SetMainReady();
        return gb::play(archive, options);
    } catch (const std::exception &e) {
        std::cerr << "Gunboat: " << e.what() << '\n';
        return 1;
    }
}
