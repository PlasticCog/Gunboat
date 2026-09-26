#pragma once
// Host layer for the differential tests: no SDL, no window, deterministic. host_pump() runs exactly
// one timer interrupt, host_fatal() throws HostFatal back to the bridge.
#include <stdexcept>

namespace gb {

struct HostFatal : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void host_stub_set_game_dir(const char *dir);

} // namespace gb
