// SDL3 host (host.hpp). Converted to C++ from the Test Drive III port's host.c (MIT, (c) 2026
// Krzysztof Kania; THIRD_PARTY.md). Changes for Gunboat: the timer rate follows the PIT divisor
// the game programs (three rates instead of TD3's fixed one), audio is generated in chunks so
// that long BIOS-rate ticks work, no mouse (GB.EXE has no INT 33h), no race frame pacing.
#include "host.hpp"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "opl3.h"

namespace gb {

namespace {

constexpr int AUDIO_RATE = 44100;
constexpr int SPEAKER_AMPLITUDE = 5000;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
SDL_AudioStream *audio;
SDL_Gamepad *gamepad;
char *game_dir;

void (*tick_handler)();
void (*kbd_handler)(u8);
void (*focus_lost_handler)();
bool (*frame_source)(u32 *, int *, int *);
bool (*presenter)(const u32 *, int, int, bool);
void (*frame_hook)();
bool (*speaker_filter)(u16, bool &, bool);
void (*tick_observer)();
void (*gamepad_handler)(const SDL_Event &);
std::vector<void (*)(u16)> sfx_play_observers;
std::vector<void (*)(u16, u16, u8)> shot_landed_observers;
std::vector<void (*)(u8, u16)> object_hit_observers;
std::vector<void (*)(u8, u8)> target_destroyed_observers;
std::vector<void (*)(u8)> shell_fired_observers;
std::vector<void (*)()> title_shown_observers;
bool (*shot_blocked_handler)(u16);
bool (*shell_stopped_handler)(u8, u16 &, u16 &);
bool speaker_effects;  // the effects driver's timer handler is running (host_speaker_effects)
bool (*hotkey_handler)(int);
bool consumed_keys[SDL_SCANCODE_COUNT];  // presses the hotkey handler took: their releases too
bool ctrl_held[2];  // the left and right Ctrl, held on the keyboard or by the controller
bool redraw = true;                      // the window needs a new picture (resized, exposed)
u32 frame[HOST_FRAME_MAX_W * HOST_FRAME_MAX_H];
int frame_w = 320, frame_h = 200;
int view_w(int w) { return w; }  // logical presentation: frame width x 3/4 of it (4:3)
int view_h(int w) { return w * 3 / 4; }

// Timer clock: tick n after the last rate change is due at base + n * divisor / PIT_HZ seconds
// (exact integer arithmetic).
Uint64 init_ns;
Uint64 clock_base_ns;
Uint64 ticks_run;
u32 pit_divisor = 65536;

// Audio: the OPL2 (as one OPL3 in OPL2 mode) and the speaker's square wave, mixed to stereo; the
// sound effects' own OPL2 (host_sfx_opl_write) once it is used.
opl3_chip opl;
opl3_chip opl_sfx;
bool opl_sfx_used;
int sfx_gain = 1;
u16 spk_div;
bool spk_on;
double spk_phase;
double samples_frac;

Uint64 last_present_ns;

void process_events();

Uint64 tick_due_ns(Uint64 n)
{
    const Uint64 pit_counts = n * pit_divisor;  // split so that long sessions do not overflow
    return clock_base_ns + pit_counts / PIT_HZ * SDL_NS_PER_SECOND + pit_counts % PIT_HZ * SDL_NS_PER_SECOND / PIT_HZ;
}

void audio_for_one_tick()
{
    if (!audio) return;
    samples_frac += double(AUDIO_RATE) * pit_divisor / PIT_HZ;
    int n = int(samples_frac);
    samples_frac -= n;
    // Drop output if the device is far behind (e.g. after a stall) instead of building latency.
    if (SDL_GetAudioStreamQueued(audio) > AUDIO_RATE / 4 * 2 * int(sizeof(s16))) return;
    const double step = double(PIT_HZ) / (spk_div ? spk_div : 65536) / AUDIO_RATE;
    s16 buf[2 * 512];
    while (n > 0) {
        const int chunk = SDL_min(n, 512);
        OPL3_GenerateStream(&opl, buf, uint32_t(chunk));
        if (opl_sfx_used) {
            s16 fx[2 * 512];
            OPL3_GenerateStream(&opl_sfx, fx, uint32_t(chunk));
            for (int i = 0; i < 2 * chunk; i++) buf[i] = s16(SDL_clamp(buf[i] + fx[i] * sfx_gain, -32768, 32767));
        }
        for (int i = 0; i < chunk; i++) {
            int s = 0;
            if (spk_on) {
                s = spk_phase < 0.5 ? SPEAKER_AMPLITUDE : -SPEAKER_AMPLITUDE;
                spk_phase += step;
                spk_phase -= int(spk_phase);
            }
            for (int c = 0; c < 2; c++) buf[2 * i + c] = s16(SDL_clamp(buf[2 * i + c] + s, -32768, 32767));
        }
        SDL_PutAudioStreamData(audio, buf, chunk * 2 * int(sizeof(s16)));
        n -= chunk;
    }
}

// Developer aid: GB_SNAPSHOT_DIR, see host.hpp.
void snapshot()
{
    static const char *dir;
    static bool checked;
    static Uint64 last_ns;
    static int n;
    if (!checked) { dir = SDL_getenv("GB_SNAPSHOT_DIR"); checked = true; }
    if (!dir || !frame_source) return;
    const Uint64 now = SDL_GetTicksNS();
    if (n && now - last_ns < 2 * SDL_NS_PER_SECOND) return;
    last_ns = now;
    SDL_Surface *s = SDL_CreateSurfaceFrom(frame_w, frame_h, SDL_PIXELFORMAT_XRGB8888, frame, frame_w * 4);
    if (!s) return;
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/snap%04d.bmp", dir, n++);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

void present()
{
    if (!texture) return;
    SDL_UpdateTexture(texture, nullptr, frame, frame_w * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    const SDL_FRect dst = {0, 0, float(view_w(frame_w)), float(view_h(frame_w))};
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_RenderPresent(renderer);
}

// Developer aid: GB_KEYS, see host.hpp.
void scripted_keys()
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = SDL_getenv("GB_KEYS"); checked = true; }
    if (!spec || !*spec || !kbd_handler) return;
    char *end;
    const double at = SDL_strtod(spec, &end);
    if (end == spec || *end != ':') { spec = nullptr; return; }
    if (double(SDL_GetTicksNS() - init_ns) / 1e9 < at) return;
    const char *p = end + 1;
    u16 keys[8];
    int n = 0;
    bool press = true, release = true;
    while (n < 8) {
        keys[n++] = u16(SDL_strtoul(p, &end, 16));
        p = end;
        if (*p == 'p') { release = false; p++; }       // held down
        else if (*p == 'r') { press = false; p++; }     // let go
        if (*p != '+') break;
        p++;
    }
    // F11, F12 and (with Ctrl, 1Dh, held) H go to the presentation layer's hotkeys first, as when
    // pressed (a taken key's release goes nowhere).
    bool taken[8] = {};
    for (int i = 0; i < n; i++)  // releases alone: F11 and F12 were taken when pressed
        taken[i] = !press && hotkey_handler && (keys[i] == 0x57 || keys[i] == 0x58);
    if (press)
        for (int i = 0; i < n; i++) {
            if (keys[i] == 0x1D) ctrl_held[0] = true;
            const SDL_Scancode sc = keys[i] == 0x57   ? SDL_SCANCODE_F11
                                    : keys[i] == 0x58 ? SDL_SCANCODE_F12
                                    : keys[i] == 0x23 ? SDL_SCANCODE_H
                                                      : SDL_SCANCODE_UNKNOWN;
            taken[i] = sc != SDL_SCANCODE_UNKNOWN && hotkey_handler && hotkey_handler(sc);
            if (taken[i]) continue;
            if (keys[i] >> 8) kbd_handler(u8(keys[i] >> 8));
            kbd_handler(u8(keys[i]));
        }
    if (release)
        for (int i = n - 1; i >= 0; i--) {
            if (keys[i] == 0x1D) ctrl_held[0] = false;
            if (taken[i]) continue;
            if (keys[i] >> 8) kbd_handler(u8(keys[i] >> 8));
            kbd_handler(u8(keys[i] | 0x80));
        }
    spec = *p == ',' ? p + 1 : nullptr;
}

// XT set-1 make code of an SDL key; GREY set = grey key sent with an E0 prefix. 0 = not reported.
// (Keypad digits send their plain codes, the grey block its E0 codes.)
constexpr u16 GREY = 0x100;
u16 xt_scan(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const u8 letter_scan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                           0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                           0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return letter_scan[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return u16(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return u16(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN: return 0x1C;
    case SDL_SCANCODE_KP_ENTER: return GREY | 0x1C;
    case SDL_SCANCODE_LCTRL: return 0x1D;
    case SDL_SCANCODE_RCTRL: return GREY | 0x1D;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2A;
    case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_KP_DIVIDE: return GREY | 0x35;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_RALT: return GREY | 0x38;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_KP_4: return 0x4B;
    case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_HOME: return GREY | 0x47;
    case SDL_SCANCODE_UP: return GREY | 0x48;
    case SDL_SCANCODE_PAGEUP: return GREY | 0x49;
    case SDL_SCANCODE_LEFT: return GREY | 0x4B;
    case SDL_SCANCODE_RIGHT: return GREY | 0x4D;
    case SDL_SCANCODE_END: return GREY | 0x4F;
    case SDL_SCANCODE_DOWN: return GREY | 0x50;
    case SDL_SCANCODE_PAGEDOWN: return GREY | 0x51;
    case SDL_SCANCODE_INSERT: return GREY | 0x52;
    case SDL_SCANCODE_DELETE: return GREY | 0x53;
    default: return 0;
    }
}

void feed(const u8 *bytes, int n)
{
    if (!kbd_handler) return;
    for (int i = 0; i < n; i++) kbd_handler(bytes[i]);
}

void key_event(SDL_Scancode sc, bool down)
{
    if (sc == SDL_SCANCODE_PAUSE) {  // make only, no break sequence
        static const u8 pause[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
        if (down) feed(pause, sizeof pause);
        return;
    }
    if (sc == SDL_SCANCODE_PRINTSCREEN) {
        static const u8 make[] = {0xE0, 0x2A, 0xE0, 0x37}, brk[] = {0xE0, 0xB7, 0xE0, 0xAA};
        if (down) feed(make, sizeof make);
        else feed(brk, sizeof brk);
        return;
    }
    const u16 x = xt_scan(sc);
    if (!x) return;
    u8 seq[2];
    int n = 0;
    if (x & GREY) seq[n++] = 0xE0;
    seq[n++] = u8(down ? (x & 0x7F) : ((x & 0x7F) | 0x80));
    feed(seq, n);
}

// A key pressed or let go: Alt+Enter aside, the presentation layer's hotkeys first (their presses and
// releases are theirs), else the game.
void key_input(SDL_Scancode sc, bool down, bool repeat)
{
    if (sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL) ctrl_held[sc == SDL_SCANCODE_RCTRL] = down;
    // PORT: Ctrl+Q quits wherever it is pressed, as closing the window does. The original quits on it
    // in a mission only (input_read_key -> quit_to_dos, which writes no file) and ignores it elsewhere.
    if (down && sc == SDL_SCANCODE_Q && (ctrl_held[0] || ctrl_held[1])) {
        host_shutdown();
        std::exit(0);
    }
    if (down) {
        if (sc < SDL_SCANCODE_COUNT && consumed_keys[sc]) return;  // its repeats
        if (!repeat && hotkey_handler && hotkey_handler(sc)) {
            if (sc < SDL_SCANCODE_COUNT) consumed_keys[sc] = true;
            redraw = true;
            return;
        }
        key_event(sc, true);
    } else {
        if (sc < SDL_SCANCODE_COUNT && consumed_keys[sc]) {
            consumed_keys[sc] = false;
            return;
        }
        key_event(sc, false);
    }
}

// The first controller, opened when none is (its connection's event may have gone to the launcher's
// own loop): checked twice a second.
void ensure_gamepad()
{
    static Uint64 next;
    const Uint64 now = SDL_GetTicksNS();
    if (gamepad || now < next) return;
    next = now + 500 * SDL_NS_PER_MS;
    if (!SDL_HasGamepad()) return;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids && n > 0) gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

void process_events()
{
    ensure_gamepad();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            host_shutdown();
            std::exit(0);
        case SDL_EVENT_KEY_DOWN:
            // Alt+Enter toggles fullscreen; the presentation layer's hotkeys; everything else goes to
            // the game, repeats included.
            if (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                if (!ev.key.repeat) host_set_fullscreen(!host_fullscreen());
                break;
            }
            key_input(ev.key.scancode, true, false);
            break;
        case SDL_EVENT_KEY_UP:
            key_input(ev.key.scancode, false, false);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (gamepad_handler) gamepad_handler(ev);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
            redraw = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (focus_lost_handler) focus_lost_handler();
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) gamepad = SDL_OpenGamepad(ev.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad && SDL_GetGamepadID(gamepad) == ev.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                if (gamepad_handler) gamepad_handler(ev);
            }
            break;
        default:
            break;
        }
    }
}

} // namespace

bool host_init(const char *dir, int window_scale, bool fullscreen)
{
    SDL_SetMainReady();  // the port has its own main
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    game_dir = SDL_strdup(dir);
    if (window_scale < 1) window_scale = 3;
    if (!SDL_CreateWindowAndRenderer("Gunboat", 320 * window_scale, 240 * window_scale, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        std::fprintf(stderr, "window/renderer failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (fullscreen) SDL_SetWindowFullscreen(window, true);
    host_set_frame_source(nullptr, 320, 200);

    OPL3_Reset(&opl, AUDIO_RATE);
    OPL3_Reset(&opl_sfx, AUDIO_RATE);
    const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) {
        SDL_ResumeAudioStreamDevice(audio);
        static s16 silence[AUDIO_RATE / 20 * 2];  // 50 ms of lead-in against underruns
        SDL_PutAudioStreamData(audio, silence, sizeof silence);
    } else {
        std::fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    }

    init_ns = clock_base_ns = SDL_GetTicksNS();
    ticks_run = 0;
    pit_divisor = 65536;
    return true;
}

void host_shutdown()
{
    if (gamepad) SDL_CloseGamepad(gamepad);
    if (audio) SDL_DestroyAudioStream(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    gamepad = nullptr;
    audio = nullptr;
    texture = nullptr;
    renderer = nullptr;
    window = nullptr;
    SDL_free(game_dir);
    game_dir = nullptr;
    SDL_Quit();
}

void host_set_timer(u16 divisor, void (*handler)())
{
    const u32 d = divisor ? divisor : 65536;
    if (d != pit_divisor) {
        clock_base_ns = tick_due_ns(ticks_run);
        ticks_run = 0;
        pit_divisor = d;
    }
    tick_handler = handler;
}

void host_set_kbd_handler(void (*handler)(u8)) { kbd_handler = handler; }
void host_set_gamepad_handler(void (*handler)(const SDL_Event &)) { gamepad_handler = handler; }
void host_key(int scancode, bool down)
{
    if (scancode > 0 && scancode < SDL_SCANCODE_COUNT) key_input(SDL_Scancode(scancode), down, false);
}
void host_set_focus_lost_handler(void (*handler)()) { focus_lost_handler = handler; }

void host_set_frame_source(bool (*compose)(u32 *, int *, int *), int w, int h)
{
    frame_source = compose;
    frame_w = SDL_clamp(w, 1, HOST_FRAME_MAX_W);
    frame_h = SDL_clamp(h, 1, HOST_FRAME_MAX_H);
    // The 200-line picture fills a 4:3 area, as it did on a VGA monitor (a presenter draws in window
    // pixels instead).
    if (presenter) SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    else SDL_SetRenderLogicalPresentation(renderer, view_w(frame_w), view_h(frame_w), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    redraw = true;
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, frame_w, frame_h);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
}

void host_opl_write(u8 reg, u8 value) { OPL3_WriteReg(&opl, reg, value); }

u8 host_pit2_low()
{
    const Uint64 ns = SDL_GetTicksNS();
    const Uint64 counts = ns / SDL_NS_PER_SECOND * PIT_HZ + ns % SDL_NS_PER_SECOND * PIT_HZ / SDL_NS_PER_SECOND;
    return u8(0 - counts);
}

void host_speaker(u16 divisor, bool on)
{
    if (speaker_filter && speaker_filter(divisor, on, speaker_effects)) return;  // the filter's own
    spk_div = divisor;
    spk_on = on;
}

bool host_speaker_effects(bool effects)
{
    const bool was = speaker_effects;
    speaker_effects = effects;
    return was;
}

void host_set_sfx_gain(int gain) { sfx_gain = gain; }

void host_sfx_opl_write(u8 reg, u8 value)
{
    opl_sfx_used = true;
    OPL3_WriteReg(&opl_sfx, reg, value);
}

void host_set_speaker_filter(bool (*filter)(u16 divisor, bool &on, bool effects)) { speaker_filter = filter; }
void host_set_tick_observer(void (*observer)()) { tick_observer = observer; }
void host_add_sfx_play_observer(void (*observer)(u16)) { sfx_play_observers.push_back(observer); }
void host_add_shot_landed_observer(void (*observer)(u16, u16, u8)) { shot_landed_observers.push_back(observer); }
void host_add_object_hit_observer(void (*observer)(u8, u16)) { object_hit_observers.push_back(observer); }
void host_add_target_destroyed_observer(void (*observer)(u8, u8)) { target_destroyed_observers.push_back(observer); }
void host_add_shell_fired_observer(void (*observer)(u8)) { shell_fired_observers.push_back(observer); }
void host_sfx_play(u16 program)
{
    for (auto f : sfx_play_observers) f(program);
}
void host_shot_landed(u16 x, u16 y, u8 weapon)
{
    for (auto f : shot_landed_observers) f(x, y, weapon);
}
void host_object_hit(u8 kind, u16 obj)
{
    for (auto f : object_hit_observers) f(kind, obj);
}
void host_target_destroyed(u8 old, u8 wreck)
{
    for (auto f : target_destroyed_observers) f(old, wreck);
}
void host_shell_fired(u8 weapon)
{
    for (auto f : shell_fired_observers) f(weapon);
}
void host_add_title_shown_observer(void (*observer)()) { title_shown_observers.push_back(observer); }
void host_set_shot_blocked_handler(bool (*handler)(u16)) { shot_blocked_handler = handler; }
bool host_shot_blocked(u16 obj) { return shot_blocked_handler && shot_blocked_handler(obj); }
void host_set_shell_stopped_handler(bool (*handler)(u8, u16 &, u16 &)) { shell_stopped_handler = handler; }
bool host_shell_stopped(u8 weapon, u16 &x, u16 &y) { return shell_stopped_handler && shell_stopped_handler(weapon, x, y); }
void host_title_shown()
{
    for (auto f : title_shown_observers) f();
}

void host_pump()
{
    process_events();
    scripted_keys();

    bool worked = false;
    Uint64 now = SDL_GetTicksNS();
    int budget = int(PIT_HZ / pit_divisor / 2) + 1;  // at most 0.5 s of catch-up per call
    while (tick_due_ns(ticks_run + 1) <= now && budget-- > 0) {
        ticks_run++;
        if (tick_handler) tick_handler();
        if (tick_observer) tick_observer();
        audio_for_one_tick();
        worked = true;
    }
    if (budget < 0) {  // fell too far behind: resynchronise the clock
        clock_base_ns = now - (tick_due_ns(ticks_run) - clock_base_ns);
    }

    snapshot();  // the screen as shown now, also while it does not change

    // Present at most once per ~8 ms; VSync paces it further.
    if (frame_source && now - last_present_ns >= 8 * SDL_NS_PER_MS) {
        int w = frame_w, h = frame_h;
        bool changed = frame_source(frame, &w, &h) || redraw;
        if (w != frame_w || h != frame_h) {  // another mode: the texture follows the frame's size
            host_set_frame_source(frame_source, w, h);
            changed = true;
        }
        bool shown = false;
        if (presenter) {
            shown = presenter(frame, frame_w, frame_h, changed);
        } else if (changed) {
            present();
            shown = true;
        }
        if (shown) {
            redraw = false;
            last_present_ns = SDL_GetTicksNS();
            worked = true;
        }
    }
    if (!worked) {
        const Uint64 next = tick_due_ns(ticks_run + 1);
        now = SDL_GetTicksNS();
        if (next > now) SDL_DelayNS(SDL_min(next - now, SDL_NS_PER_MS));
    }
}

namespace {
Uint64 frame_period_ns = SDL_NS_PER_SECOND / 15;
Uint64 last_frame_ns;
} // namespace

void host_set_frame_rate(int fps) { frame_period_ns = fps > 0 ? SDL_NS_PER_SECOND / Uint64(fps) : 0; }

void host_frame_drawn()
{
    if (frame_hook) frame_hook();
}

void host_frame_pace()
{
    if (frame_period_ns == 0) return;
    const Uint64 due = last_frame_ns + frame_period_ns;
    while (SDL_GetTicksNS() < due) host_pump();
    const Uint64 now = SDL_GetTicksNS();
    // keep the rhythm, but do not try to catch up after a stall (a full-screen station, a drag)
    last_frame_ns = now - due < frame_period_ns ? due : now;
}

// VGA 320x200 (mode 13h) refresh: 25.175 MHz / (800 x 449) = 70.086 Hz.
void host_wait_vretrace()
{
    constexpr Uint64 num = 800ull * 449ull * SDL_NS_PER_SECOND, den = 25175000ull;
    const Uint64 since = SDL_GetTicksNS() - init_ns;
    const Uint64 next = (since * den / num + 1) * num / den;
    while (SDL_GetTicksNS() - init_ns < next) host_pump();
}

bool host_joy_read(s16 *x, s16 *y, u8 *buttons)
{
    if (!gamepad && SDL_HasGamepad()) {  // one connected before its event reached the host (the launcher)
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        if (ids && n > 0) gamepad = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
    if (!gamepad) return false;
    s16 ax = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 ay = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) ax = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) ax = 32767;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) ay = -32768;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) ay = 32767;
    u8 b = 0;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 1;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)) b |= 2;
    if (x) *x = ax;
    if (y) *y = ay;
    if (buttons) *buttons = b;
    return true;
}

char *host_game_path(const char *name, bool create)
{
    char *direct = nullptr;
    SDL_asprintf(&direct, "%s/%s", game_dir, name);
    if (SDL_GetPathInfo(direct, nullptr)) return direct;

    int count = 0;
    char **entries = SDL_GlobDirectory(game_dir, nullptr, 0, &count);
    char *found = nullptr;
    for (int i = 0; entries && i < count; i++) {
        if (SDL_strcasecmp(entries[i], name) == 0) {
            SDL_asprintf(&found, "%s/%s", game_dir, entries[i]);
            break;
        }
    }
    SDL_free(entries);
    if (found) {
        SDL_free(direct);
        return found;
    }
    if (create) return direct;
    SDL_free(direct);
    return nullptr;
}

void host_free(void *p) { SDL_free(p); }

SDL_Window *host_window() { return window; }
SDL_Renderer *host_renderer() { return renderer; }

void host_set_game_dir(const char *dir)
{
    SDL_free(game_dir);
    game_dir = SDL_strdup(dir);
}

void host_set_presenter(bool (*present)(const u32 *, int, int, bool))
{
    presenter = present;
    host_set_frame_source(frame_source, frame_w, frame_h);  // the logical presentation for it
}

void host_set_frame_hook(void (*hook)()) { frame_hook = hook; }
void host_set_hotkey_handler(bool (*handler)(int)) { hotkey_handler = handler; }
bool host_ctrl_held() { return ctrl_held[0] || ctrl_held[1]; }

void host_set_fullscreen(bool on)
{
    if (window) SDL_SetWindowFullscreen(window, on);
    redraw = true;
}

bool host_fullscreen() { return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN); }

void host_reset_clock()
{
    init_ns = clock_base_ns = SDL_GetTicksNS();
    ticks_run = 0;
    last_frame_ns = 0;
    redraw = true;
}

void host_fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "fatal: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gunboat", msg, window);
    host_shutdown();
    std::exit(3);
}

void host_error_box(const char *text)
{
    std::fprintf(stderr, "%s\n", text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gunboat", text, window);
}

void host_exit(int code)
{
    host_shutdown();
    std::exit(code);
}

} // namespace gb
