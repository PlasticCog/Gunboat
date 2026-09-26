#include "original_controls.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace gb {
namespace {
constexpr uint16_t bit(Station s) { return uint16_t(1u << unsigned(s)); }
constexpr uint16_t pilot = bit(Station::Pilot);
constexpr uint16_t gunners = bit(Station::Bow) | bit(Station::Midship) | bit(Station::Stern);
constexpr uint16_t mission = pilot | gunners;
constexpr uint16_t panels = bit(Station::Map) | bit(Station::Assignment) | bit(Station::Damage);
constexpr uint16_t menus = panels | bit(Station::Menu);
constexpr uint16_t everywhere = mission | menus;
struct KeyName { uint16_t key; const char *name; };
constexpr KeyName names[] = {
    {0,"Unbound"},{1,"Esc"},{2,"1"},{3,"2"},{4,"3"},{5,"4"},{6,"5"},
    {7,"6"},{8,"7"},{9,"8"},{10,"9"},{11,"0"},{12,"Minus"},{13,"Equals"},
    {14,"Backspace"},{15,"Tab"},{16,"Q"},{17,"W"},{18,"E"},{19,"R"},
    {20,"T"},{21,"Y"},{22,"U"},{23,"I"},{24,"O"},{25,"P"},
    {26,"LeftBracket"},{27,"RightBracket"},{28,"Enter"},{29,"LeftCtrl"},
    {30,"A"},{31,"S"},{32,"D"},{33,"F"},{34,"G"},{35,"H"},{36,"J"},
    {37,"K"},{38,"L"},{39,"Semicolon"},{40,"Apostrophe"},{41,"Grave"},
    {42,"LeftShift"},{43,"Backslash"},{44,"Z"},{45,"X"},{46,"C"},{47,"V"},
    {48,"B"},{49,"N"},{50,"M"},{51,"Comma"},{52,"Period"},{53,"Slash"},
    {54,"RightShift"},{55,"KP_Multiply"},{56,"LeftAlt"},{57,"Space"},
    {58,"CapsLock"},{59,"F1"},{60,"F2"},{61,"F3"},{62,"F4"},{63,"F5"},
    {64,"F6"},{65,"F7"},{66,"F8"},{67,"F9"},{68,"F10"},{69,"NumLock"},
    {70,"ScrollLock"},{71,"KP_7"},{72,"KP_8"},{73,"KP_9"},{74,"KP_Minus"},
    {75,"KP_4"},{76,"KP_5"},{77,"KP_6"},{78,"KP_Plus"},{79,"KP_1"},
    {80,"KP_2"},{81,"KP_3"},{82,"KP_0"},{83,"KP_Period"},
    {87,"F11"},{88,"F12"},{0x11c,"KP_Enter"},{0x11d,"RightCtrl"},
    {0x135,"KP_Divide"},{0x137,"PrintScreen"},{0x138,"RightAlt"},
    {0x147,"Home"},{0x148,"Up"},{0x149,"PageUp"},{0x14b,"Left"},
    {0x14d,"Right"},{0x14f,"End"},{0x150,"Down"},{0x151,"PageDown"},
    {0x152,"Insert"},{0x153,"Delete"},{0x15b,"LeftWin"},{0x15c,"RightWin"},
    {0x15d,"Application"},{0x1ff,"Pause"}
};
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool modifier_key(uint16_t k) {
    return k == 0x1d || k == 0x11d || k == 0x2a || k == 0x36 ||
           k == 0x38 || k == 0x138;
}
bool known_key(uint16_t k) {
    return std::any_of(std::begin(names), std::end(names), [=](const KeyName &n) { return n.key == k; });
}
uint8_t legacy_direction(uint16_t actual) {
    const auto scan = actual & 0xff;
    if (actual == 0x29) return 1; // Original DOS keyboard alternatives.
    if (actual == 0x2b) return 4;
    if (actual == 0x4a) return 2;
    if (actual == 0x4e) return 8;
    switch (scan) {
    case 0x47: return 5;
    case 0x48: return 1;
    case 0x49: return 9;
    case 0x4b: return 4;
    case 0x4d: return 8;
    case 0x4f: return 6;
    case 0x50: return 2;
    case 0x51: return 10;
    default: return 0;
    }
}
uint8_t cardinal(uint16_t key) {
    switch (key & 0xff) {
    case 0x48: return 1;
    case 0x50: return 2;
    case 0x4b: return 4;
    case 0x4d: return 8;
    default: return 0;
    }
}
// DOS diagonal keys intentionally drive two direction actions simultaneously.
bool key_matches(Chord chord, uint16_t actual) {
    if (chord.key == actual) return true;
    if (!chord.keypadEquivalent) return false;
    if (cardinal(chord.key)) return (legacy_direction(actual) & cardinal(chord.key)) != 0;
    if ((chord.key & 0xff) == 0x1c) return (actual & 0xff) == 0x1c;
    return false;
}
bool mods_match(Chord c, uint8_t mods) {
    const uint8_t mask = c.ignoreShift ? uint8_t(Ctrl | Alt) : uint8_t(Shift | Ctrl | Alt);
    return (mods & mask) == (c.modifiers & mask);
}
bool overlaps(Chord a, Chord b) {
    if (!a.key || !b.key) return false;
    bool key = false;
    for (const auto &n : names) key = key || (key_matches(a, n.key) && key_matches(b, n.key));
    if (!key) return false;
    for (uint8_t m = 0; m < 8; ++m)
        if (mods_match(a, m) && mods_match(b, m)) return true;
    return false;
}
Chord original(uint16_t key, bool keypad = false) { return {key, 0, true, keypad}; }
} // namespace

const char *station_name(Station s) {
    switch (s) {
    case Station::Pilot: return "Pilot";
    case Station::Bow: return "Bow gunner";
    case Station::Midship: return "Midship gunner";
    case Station::Stern: return "Stern gunner";
    case Station::Map: return "Mission map";
    case Station::Assignment: return "Assignment";
    case Station::Damage: return "Damage report";
    case Station::Menu: return "Menu";
    }
    return "Unknown";
}
std::string chord_name(Chord c) {
    if (!c.key) return "Unbound";
    std::string s;
    if (c.modifiers & Ctrl) s += "Ctrl+";
    if (c.modifiers & Alt) s += "Alt+";
    if (c.modifiers & Shift) s += "Shift+";
    for (const auto &n : names) if (n.key == c.key) return s + n.name;
    return s + "Unknown";
}
std::optional<Chord> parse_chord(const std::string &text) {
    Chord c;
    auto s = trim(text);
    auto flags = s.find(' ');
    std::string suffix;
    if (flags != std::string::npos) { suffix = s.substr(flags); s.resize(flags); }
    std::istringstream parts(s);
    std::string part;
    bool found = false;
    while (std::getline(parts, part, '+')) {
        part = lower(trim(part));
        if (part == "ctrl" || part == "alt" || part == "shift") {
            const uint8_t flag = part == "ctrl" ? Ctrl : part == "alt" ? Alt : Shift;
            if (found || (c.modifiers & flag)) return {};
            c.modifiers |= flag;
        } else {
            if (found) return {};
            auto n = std::find_if(std::begin(names), std::end(names), [&](const KeyName &k) {
                return lower(k.name) == part;
            });
            if (n == std::end(names)) return {};
            c.key = n->key;
            found = true;
        }
    }
    if (!found || (!s.empty() && s.back() == '+') || (!c.key && c.modifiers) || modifier_key(c.key)) return {};
    std::istringstream options(suffix);
    while (options >> part) {
        if (part == "case-insensitive" && !(c.modifiers & Shift) && !c.ignoreShift) c.ignoreShift = true;
        else if (part == "keypad-equivalent" && !c.keypadEquivalent) c.keypadEquivalent = true;
        else return {};
    }
    return c;
}
bool action_active(const Binding &b, Station s) { return (b.stations & bit(s)) != 0; }

Controls::Controls() { defaults(); }
void Controls::defaults() {
    bindings_.clear();
    auto add = [&](Action a, const char *id, const char *label, uint16_t stations, bool continuous,
                   Chord chord, bool enhancement = false) {
        bindings_.push_back({a,id,label,stations,continuous,enhancement,chord});
    };
    // DOS dispatch table, image 0x941e. See CONTROL_FIDELITY.md for evidence.
    add(Action::Pilot,"pilot","Pilot: center panel",mission|panels,false,original(0x2d));
    add(Action::PilotLeft,"pilot_left","Pilot: left panel",mission|panels,false,original(0x2c));
    add(Action::PilotRight,"pilot_right","Pilot: right panel",mission|panels,false,original(0x2e));
    add(Action::Bow,"bow","Bow gunner",mission|panels,false,original(0x2f));
    add(Action::Midship,"midship","Midship gunner",mission|panels,false,original(0x31));
    add(Action::Stern,"stern","Stern gunner",mission|panels,false,original(0x30));
    add(Action::ThrottleForward,"throttle_forward","Throttle forward",pilot,true,original(0x148,true));
    add(Action::ThrottleBack,"throttle_back","Throttle back / reverse",pilot,true,original(0x150,true));
    add(Action::TurnLeft,"turn_left","Water jets left",pilot,true,original(0x14b,true));
    add(Action::TurnRight,"turn_right","Water jets right",pilot,true,original(0x14d,true));
    add(Action::Neutral,"neutral","Throttle toward neutral",pilot,true,original(0x1c,true));
    add(Action::AimUp,"aim_up","Aim up",gunners,true,original(0x148,true));
    add(Action::AimDown,"aim_down","Aim down",gunners,true,original(0x150,true));
    add(Action::AimLeft,"aim_left","Aim left",gunners,true,original(0x14b,true));
    add(Action::AimRight,"aim_right","Aim right",gunners,true,original(0x14d,true));
    add(Action::Fire,"fire","Fire station weapon",gunners,true,original(0x1c,true));
    add(Action::Pause,"pause","Pause / resume",mission,false,original(0x01));
    add(Action::ReturnBase,"return_base","Return to base",mission,false,original(0x0f));
    add(Action::Map,"map","Mission map",mission,false,original(0x32));
    add(Action::Assignment,"assignment","Assignment review",mission,false,original(0x35));
    add(Action::Damage,"damage","Damage report",mission,false,original(0x34));
    add(Action::Identify,"identify","Identify target",mission,false,original(0x43));
    add(Action::TimeCycle,"time_cycle","Time compression off / on / high",mission,false,original(0x0d));
    add(Action::TimeExtra,"time_extra","Extra time compression",mission,true,original(0x0e));
    add(Action::ReverseCourse,"reverse_course","Pilot: reverse course",gunners,false,original(0x3e));
    add(Action::BranchLeft,"branch_left","Pilot: branch left",gunners,false,original(0x3f));
    add(Action::BranchRight,"branch_right","Pilot: branch right",gunners,false,original(0x40));
    add(Action::Slower,"slower","Pilot: slower",gunners,false,original(0x41));
    add(Action::Faster,"faster","Pilot: faster",gunners,false,original(0x42));
    add(Action::CrewFire,"crew_fire","Crew: open / cease fire",mission,false,original(0x44));
    add(Action::Detail,"detail","Original detail level",mission,false,original(0x20));
    add(Action::Chase,"chase","Original chase boat view",mission,false,original(0x33));
    add(Action::SystemF1,"system_f1","Station system function 1",mission,false,original(0x3b));
    add(Action::SystemF2,"system_f2","Station system function 2",mission,false,original(0x3c));
    add(Action::SystemF3,"system_f3","Station system function 3",mission,false,original(0x3d));
    add(Action::ControlRate,"control_rate","Control response: low / medium / high",mission,false,original(0x0c));
    add(Action::Effects,"effects","Sound effects toggle",everywhere,false,original(0x12));
    add(Action::Music,"music","Music toggle",everywhere,false,original(0x1f));
    add(Action::Quit,"quit","Quit game",everywhere,false,{0x10,Ctrl,true,false});
    add(Action::MenuAccept,"menu_accept","Confirm selection",menus,false,original(0x1c,true));
    add(Action::MenuBack,"menu_back","Back",menus,false,original(0x01));
    add(Action::MenuUp,"menu_up","Menu up",menus,false,original(0x148,true));
    add(Action::MenuDown,"menu_down","Menu down",menus,false,original(0x150,true));
    add(Action::MenuLeft,"menu_left","Menu left",menus,false,original(0x14b,true));
    add(Action::MenuRight,"menu_right","Menu right",menus,false,original(0x14d,true));
    // New host options have their own namespace and never occupy DOS function keys.
    add(Action::Settings,"settings","Controls and port options",everywhere,false,{0x58,0,false,false},true);
    add(Action::EnhancedCamera,"enhanced_camera","Enhanced external camera",mission,false,{0x57,0,false,false},true);
    add(Action::Restart,"restart","Restart prototype session",mission,false,{0x13,Ctrl,false,false},true);
    add(Action::World1,"world_1","Prototype world 1",everywhere,false,{0x3b,Ctrl,false,false},true);
    add(Action::World2,"world_2","Prototype world 2",everywhere,false,{0x3c,Ctrl,false,false},true);
    add(Action::World3,"world_3","Prototype world 3",everywhere,false,{0x3d,Ctrl,false,false},true);
    add(Action::World4,"world_4","Prototype world 4",everywhere,false,{0x3e,Ctrl,false,false},true);
    clear();
}
uint8_t Controls::modifiers() const {
    return uint8_t(((keys_[0x2a] || keys_[0x36]) ? Shift : 0) |
                   ((keys_[0x1d] || keys_[0x11d]) ? Ctrl : 0) |
                   ((keys_[0x38] || keys_[0x138]) ? Alt : 0));
}
void Controls::feed_xt(uint8_t value) {
    if (pauseBytes_) { --pauseBytes_; return; }
    if (value == 0xe1) {
        pauseBytes_ = 5;
        capture_ = Chord{0x1ff,modifiers(),false,false};
        for (const auto &b : bindings_)
            if (b.chord.key == 0x1ff && mods_match(b.chord,modifiers())) pressed_[size_t(b.action)] = true;
        return;
    }
    if (value == 0xe0) { extended_ = true; return; }
    const uint16_t key = uint16_t((value & 0x7f) | (extended_ ? 0x100 : 0));
    extended_ = false;
    // Ignore fake Shift prefixes in the XT PrintScreen sequence.
    if (key == 0x12a || key == 0x136) return;
    const bool downNow = !(value & 0x80), wasDown = keys_[key];
    keys_[key] = downNow;
    if (!downNow || wasDown || modifier_key(key) || !known_key(key)) return;
    capture_ = Chord{key,modifiers(),false,false};
    for (const auto &b : bindings_)
        if (b.chord.key && key_matches(b.chord,key) && mods_match(b.chord,modifiers()))
            pressed_[size_t(b.action)] = true;
}
void Controls::clear() {
    keys_.fill(false);
    pressed_.fill(false);
    capture_.reset();
    extended_ = false;
    pauseBytes_ = 0;
}
void Controls::end_frame() { pressed_.fill(false); capture_.reset(); }
bool Controls::down(Chord c) const {
    if (!c.key || !mods_match(c,modifiers())) return false;
    if (keys_[c.key]) return true;
    if (c.keypadEquivalent)
        for (const auto &n : names) if (keys_[n.key] && key_matches(c,n.key)) return true;
    return false;
}
const Binding &Controls::binding(Action a) const {
    if (size_t(a) >= bindings_.size()) throw std::out_of_range("Unknown input action");
    return bindings_[size_t(a)];
}
std::string Controls::binding_label(Action a) const { return chord_name(binding(a).chord); }
bool Controls::held(Action a, Station s) const {
    const auto &b = binding(a);
    return action_active(b,s) && down(b.chord);
}
bool Controls::take(Action a, Station s) {
    if (!action_active(binding(a),s)) return false;
    bool value = pressed_[size_t(a)];
    pressed_[size_t(a)] = false;
    return value;
}
uint8_t Controls::helm_mask() const {
    return uint8_t((held(Action::ThrottleForward,Station::Pilot) ? 1 : 0) |
                   (held(Action::ThrottleBack,Station::Pilot) ? 2 : 0) |
                   (held(Action::TurnLeft,Station::Pilot) ? 4 : 0) |
                   (held(Action::TurnRight,Station::Pilot) ? 8 : 0) |
                   (held(Action::Neutral,Station::Pilot) ? 16 : 0));
}
std::vector<Action> Controls::conflicts(Action a, Chord c) const {
    std::vector<Action> result;
    const auto &target = binding(a);
    for (const auto &b : bindings_) {
        if (a == b.action || !(target.stations & b.stations)) continue;
        // Defaults share Home/PageUp/End/PageDown only to provide diagonal input.
        const auto cb = cardinal(c.key), bb = cardinal(b.chord.key);
        if (c.keypadEquivalent && b.chord.keypadEquivalent && c.ignoreShift &&
            b.chord.ignoreShift && cb && bb && cb != bb) continue;
        if (overlaps(c,b.chord)) result.push_back(b.action);
    }
    return result;
}
bool Controls::rebind(Action a, Chord c, std::string *error) {
    binding(a);
    if (!known_key(c.key) || modifier_key(c.key) || (c.modifiers & ~7) ||
        (!c.key && c.modifiers) || (c.ignoreShift && (c.modifiers & Shift)) ||
        (c.key == 0x1ff && binding(a).continuous)) {
        if (error) *error = "This key cannot be used for that action.";
        return false;
    }
    auto collisions = conflicts(a,c);
    if (!collisions.empty()) {
        if (error) *error = std::string("Already assigned to ") + binding(collisions.front()).label;
        return false;
    }
    bindings_[size_t(a)].chord = c;
    clear();
    return true;
}
ControlLoadResult Controls::load(const std::filesystem::path &path) {
    ControlLoadResult result;
    std::ifstream in(path);
    if (!in) {
        if (std::filesystem::exists(path)) result.errors.push_back("Cannot read controls file: " + path.string());
        return result;
    }
    // Transactional: malformed files never partially replace working bindings.
    Controls candidate = *this;
    std::set<Action> seen;
    std::string line;
    size_t n = 0;
    while (std::getline(in,line)) {
        ++n;
        if (n == 1 && line.compare(0,3,"\xef\xbb\xbf") == 0) line.erase(0,3);
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        line = trim(line);
        if (line.empty()) continue;
        auto fail = [&](const std::string &why) { result.errors.push_back("Line " + std::to_string(n) + ": " + why); };
        const auto eq = line.find('=');
        if (eq == std::string::npos) { fail("Expected action=key."); continue; }
        const std::string id = trim(line.substr(0,eq));
        auto target = std::find_if(candidate.bindings_.begin(),candidate.bindings_.end(),[&](const Binding &b) { return id == b.id; });
        if (target == candidate.bindings_.end()) { fail("Unknown action: " + id); continue; }
        if (!seen.insert(target->action).second) { fail("Duplicate action: " + id); continue; }
        auto c = parse_chord(trim(line.substr(eq+1)));
        if (!c || (c->key == 0x1ff && target->continuous)) { fail("Invalid key binding."); continue; }
        target->chord = *c;
    }
    if (in.bad()) result.errors.push_back("Error reading controls file.");
    for (const auto &b : candidate.bindings_) {
        for (auto other : candidate.conflicts(b.action,b.chord)) {
            if (size_t(other) > size_t(b.action))
                result.errors.push_back(std::string("Conflicting bindings: ") + b.id + " and " + candidate.binding(other).id);
        }
    }
    if (result.errors.empty()) { bindings_ = std::move(candidate.bindings_); clear(); result.loaded = true; }
    return result;
}
void Controls::save(const std::filesystem::path &path) const {
    std::ofstream out(path,std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write controls file: " + path.string());
    out << "# Gunboat native controls v1. DOS defaults are documented in CONTROL_FIDELITY.md.\n"
           "# Physical key names; modifiers: Ctrl+, Alt+, Shift+. Unbound disables an action.\n"
           "# Options preserve DOS uppercase/keypad aliases. New bindings can omit both.\n";
    for (const auto &b : bindings_) {
        out << "\n# " << b.label << (b.enhancement ? " (port option)" : "") << '\n';
        out << b.id << '=' << chord_name(b.chord);
        if (b.chord.ignoreShift) out << " case-insensitive";
        if (b.chord.keypadEquivalent) out << " keypad-equivalent";
        out << '\n';
    }
    out.flush();
    if (!out) throw std::runtime_error("Cannot finish writing controls file: " + path.string());
}
std::optional<Chord> Controls::capture() { auto c = capture_; capture_.reset(); return c; }

void controls_self_test() {
    auto require = [](bool ok, const char *why) {
        if (!ok) throw std::runtime_error(std::string("Controls test failed: ") + why);
    };
    Controls c;
    for (const auto &b : c.bindings()) {
        require(c.conflicts(b.action,b.chord).empty(),"default binding conflict");
        require(c.binding(b.action).action == b.action,"action enumeration/order");
    }
    c.feed_xt(0xe0); c.feed_xt(0x48);
    require(c.helm_mask() == 1,"extended arrow drives helm");
    require(c.held(Action::AimUp,Station::Bow),"arrow aims at gunner station");
    require(!c.held(Action::ThrottleForward,Station::Bow),"gunner cannot manually throttle");
    c.feed_xt(0x48); // Keypad up pressed simultaneously.
    c.feed_xt(0xe0); c.feed_xt(0xc8);
    require(c.helm_mask() == 1,"releasing one arrow block preserves the other");
    c.feed_xt(0xc8);
    require(c.helm_mask() == 0,"both arrow blocks release");
    c.feed_xt(0xe0); c.feed_xt(0x47);
    require(c.helm_mask() == 5,"original Home diagonal");
    c.clear(); c.feed_xt(0x51);
    require(c.helm_mask() == 10,"original keypad diagonal");
    c.clear(); c.feed_xt(0x29);
    require(c.helm_mask() == 1,"original Grave alternate up");
    c.clear(); c.feed_xt(0x2b);
    require(c.helm_mask() == 4,"original Backslash alternate left");
    c.clear();
    c.feed_xt(0x1c);
    require(c.held(Action::Neutral,Station::Pilot),"DOS Enter means neutral at helm");
    require(!c.held(Action::Fire,Station::Pilot),"helm Enter never fires");
    require(c.held(Action::Fire,Station::Stern),"DOS Enter fires gunner weapon");
    c.clear(); c.feed_xt(0x39);
    require(c.helm_mask() == 0 && !c.held(Action::Fire,Station::Bow),"Space is not DOS fire/neutral");
    c.clear(); c.feed_xt(0x2a); c.feed_xt(0x2d);
    require(c.take(Action::Pilot,Station::Bow),"uppercase station shortcut");
    require(!c.take(Action::Pilot,Station::Bow),"press action is consumed once");
    c.feed_xt(0x2d);
    require(!c.take(Action::Pilot,Station::Bow),"OS typematic does not retrigger");
    c.clear(); c.feed_xt(0x0e);
    require(c.held(Action::TimeExtra,Station::Pilot),"Backspace extra compression");
    c.clear(); c.feed_xt(0x1d); c.feed_xt(0x3b);
    require(c.take(Action::World1,Station::Pilot),"modified enhancement shortcut");
    require(!c.take(Action::SystemF1,Station::Pilot),"enhancement never toggles DOS system");
    auto captured = c.capture();
    require(captured && captured->key == 0x3b && captured->modifiers == Ctrl,"key capture includes modifiers");
    require(!c.capture(),"key capture consumption");
    c.clear();
    const uint8_t pause[] = {0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
    for (uint8_t b : pause) c.feed_xt(b);
    captured = c.capture();
    require(captured && captured->key == 0x1ff,"Pause XT sequence captured atomically");
    c.feed_xt(0x3b);
    require(c.take(Action::SystemF1,Station::Pilot),"Pause prefix never leaves fake Ctrl held");
    c.clear(); c.feed_xt(0xe0); c.feed_xt(0x2a); c.feed_xt(0xe0); c.feed_xt(0x37);
    captured = c.capture();
    require(captured && captured->key == 0x137 && captured->modifiers == 0,"PrintScreen fake Shift ignored");
    c.clear(); c.feed_xt(0x58); c.end_frame();
    require(!c.take(Action::Settings,Station::Pilot),"frame clears unconsumed edges");
    require(c.held(Action::Settings,Station::Pilot),"frame end preserves physically held key");
    c.clear();
    require(!c.held(Action::Settings,Station::Pilot),"focus loss clears held key");
    std::string error;
    require(!c.rebind(Action::Fire,{0x12,0,false,false},&error),"reject E which toggles effects globally");
    require(!error.empty(),"conflict has readable reason");
    require(c.rebind(Action::Fire,{0x39,0,false,false}),"rebind fire to Space");
    require(c.rebind(Action::Neutral,{0x39,0,false,false}),"same physical key allowed in separate stations");
    c.feed_xt(0x39);
    require(c.held(Action::Fire,Station::Bow) && c.helm_mask() == 16,"remapped context-sensitive action works");
    require(c.rebind(Action::Fire,{0,0,false,false}),"unbind action");
    require(!c.rebind(Action::Fire,{0x1ff,0,false,false}),"continuous action cannot use no-break Pause");
    require(!c.rebind(Action::Fire,{0x7f,0,false,false}),"unknown key rejected");
    require(!parse_chord("Ctrl+Ctrl+F1") && !parse_chord("Ctrl+F1+") && !parse_chord("Ctrl+Unbound"),"malformed chord rejection");
    auto parsed = parse_chord("Ctrl+Shift+F1");
    require(parsed && parsed->modifiers == (Ctrl|Shift) && parsed->key == 0x3b,"modifier parser");

    const auto temporary = std::filesystem::temp_directory_path() /
        ("gunboat-controls-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".cfg");
    struct RemoveTemporary { std::filesystem::path p; ~RemoveTemporary() { std::error_code e; std::filesystem::remove(p,e); } } cleanup{temporary};
    c.defaults(); c.rebind(Action::Fire,{0x39,0,false,false});
    c.save(temporary);
    Controls restored;
    auto loaded = restored.load(temporary);
    require(loaded.loaded && loaded.errors.empty(),"saved bindings reload");
    require(restored.binding(Action::Fire).chord.key == 0x39,"remap persists");
    require(restored.binding(Action::AimUp).chord.keypadEquivalent,"DOS alias semantics persist");
    restored.feed_xt(0xe0); restored.feed_xt(0x48);
    require(restored.helm_mask() == 1,"reloaded arrows work");
    auto write = [&](const char *s) { std::ofstream file(temporary); file << s; };
    write("fire=Space\nneutral=NotAKey\n");
    auto failed = restored.load(temporary);
    require(!failed.loaded && !failed.errors.empty(),"bad key rejected");
    require(restored.binding(Action::Neutral).chord.key == 0x1c,"failed load is transactional");
    write("fire=Space\nfire=Enter\n");
    failed = restored.load(temporary);
    require(!failed.loaded && !failed.errors.empty(),"duplicate action rejected");
    write("fire=E\n");
    failed = restored.load(temporary);
    require(!failed.loaded && !failed.errors.empty(),"file conflicts rejected");
    write("unknown=F1\n");
    failed = restored.load(temporary);
    require(!failed.loaded && !failed.errors.empty(),"unknown action rejected");
    // A full-file swap must work even though applying each individual binding
    // against the old layout would transiently create a conflict.
    c.defaults();
    write("bow=B\nstern=V\n");
    loaded = c.load(temporary);
    require(loaded.loaded && c.binding(Action::Bow).chord.key == 0x30 &&
            c.binding(Action::Stern).chord.key == 0x2f,"transactional binding swap");
}
} // namespace gb
