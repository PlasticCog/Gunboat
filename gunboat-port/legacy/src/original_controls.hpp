#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gb {
// Values are the original DOS DS:0086 station/screen numbers.
enum class Station { Pilot = 1, Bow = 2, Midship = 3, Stern = 4, Map = 5,
                     Assignment = 7, Damage = 8, Menu = 9 };
enum class Action {
    Pilot, PilotLeft, PilotRight, Bow, Midship, Stern,
    ThrottleForward, ThrottleBack, TurnLeft, TurnRight, Neutral,
    AimUp, AimDown, AimLeft, AimRight, Fire,
    Pause, ReturnBase, Map, Assignment, Damage, Identify, TimeCycle, TimeExtra,
    ReverseCourse, BranchLeft, BranchRight, Slower, Faster, CrewFire,
    Detail, Chase, SystemF1, SystemF2, SystemF3, ControlRate, Effects, Music, Quit,
    MenuAccept, MenuBack, MenuUp, MenuDown, MenuLeft, MenuRight,
    Settings, EnhancedCamera, Restart, World1, World2, World3, World4, Count
};
enum KeyModifier : uint8_t { Shift = 1, Ctrl = 2, Alt = 4 };
struct Chord {
    // XT set-1 make code; add 0x100 for E0. Zero means unbound.
    uint16_t key = 0;
    uint8_t modifiers = 0;
    // Original DOS bindings ignore letter case and accept both arrow key blocks.
    // A captured/reconfigured chord is exact unless explicitly given these flags.
    bool ignoreShift = false;
    bool keypadEquivalent = false;
};
struct Binding {
    Action action;
    const char *id;
    const char *label;
    uint16_t stations;
    bool continuous;
    bool enhancement;
    Chord chord;
};
struct ControlLoadResult {
    bool loaded = false;
    std::vector<std::string> errors;
};

const char *station_name(Station);
std::string chord_name(Chord);
std::optional<Chord> parse_chord(const std::string &);
bool action_active(const Binding &, Station);

class Controls {
  public:
    Controls();
    void defaults();
    void feed_xt(uint8_t);
    void clear();
    // Discard this frame's unconsumed press events and key-capture event.
    void end_frame();
    bool held(Action, Station) const;
    bool take(Action, Station);
    uint8_t helm_mask() const;
    const std::vector<Binding> &bindings() const { return bindings_; }
    const Binding &binding(Action) const;
    std::string binding_label(Action) const;
    std::vector<Action> conflicts(Action, Chord) const;
    // Refuses overlapping active actions; returns false and does not change state.
    bool rebind(Action, Chord, std::string *error = nullptr);
    ControlLoadResult load(const std::filesystem::path &);
    void save(const std::filesystem::path &) const;
    // Last new, non-modifier physical key press. Calling consumes the capture only.
    std::optional<Chord> capture();

  private:
    std::vector<Binding> bindings_;
    std::array<bool, 512> keys_{};
    std::array<bool, static_cast<size_t>(Action::Count)> pressed_{};
    std::optional<Chord> capture_;
    bool extended_ = false;
    unsigned pauseBytes_ = 0;
    uint8_t modifiers() const;
    bool down(Chord) const;
};

void controls_self_test();
} // namespace gb
