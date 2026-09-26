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
void host_stub_set_pit2(u8 v);  // what IN 42h reads in the tests

// The test's timer model: host_pump() runs this once per call (the game's own host_set_timer calls
// are recorded, not run: a test decides what one tick is on both sides).
void host_stub_set_test_tick(void (*tick)());
// The PIT divisor the game last programmed (host_set_timer), for tests.
u16 host_stub_timer_divisor();

// Every host_speaker() call since the last clear: (divisor, on) in order.
struct SpeakerEvent {
    u16 divisor;
    bool on;
};
const std::vector<SpeakerEvent> &host_stub_speaker_log();
void host_stub_speaker_clear();

// Every host_set_timer() divisor since the last clear, in order.
const std::vector<u16> &host_stub_timer_log();
void host_stub_timer_clear();

} // namespace gb
