#include "../src/original_controls.hpp"
#include <iostream>
#include <sstream>
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--trace") {
        std::string line;
        while (std::getline(std::cin,line)) {
            gb::Controls controls;
            std::istringstream stream(line);
            unsigned byte;
            bool first = true;
            while (stream >> std::hex >> byte) {
                controls.feed_xt(uint8_t(byte));
                if (!first) std::cout << ',';
                first = false;
                std::cout << unsigned(controls.helm_mask());
            }
            std::cout << '\n';
        }
        return 0;
    }
    try { gb::controls_self_test(); std::cout << "Original controls tests passed.\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
