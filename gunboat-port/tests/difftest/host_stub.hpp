#pragma once
// Host layer for the differential tests: no SDL, no window, deterministic. host_pump() runs exactly
// one timer interrupt, host_fatal() throws HostFatal back to the bridge.
#include <stdexcept>
#include <string>
#include <vector>

#include "types.hpp"

namespace gb {

struct HostFatal : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// host_exit() in a test: the program would end here.
struct HostExit : std::runtime_error {
    int code;
    explicit HostExit(int c) : std::runtime_error("exit(" + std::to_string(c) + ")"), code(c) {}
};

void host_stub_set_game_dir(const char *dir);

// Every host_speaker() call since the last clear: (divisor, on) in order.
struct SpeakerEvent {
    u16 divisor;
    bool on;
};
const std::vector<SpeakerEvent> &host_stub_speaker_log();
void host_stub_speaker_clear();

} // namespace gb
