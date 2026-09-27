#pragma once
// Host services: the layer between the game and SDL3 (host.cpp; the presentation layer in
// src/enhanced/ uses SDL too, never the game). Window and presentation, the PIT
// timer interrupt, VGA vertical retrace, keyboard (XT byte stream for the game's own INT 9
// handler), joystick, audio (OPL2 through Nuked-OPL3, PC speaker), game files and fatal errors.
// No game logic lives here. Converted to C++ from the Test Drive III port's host.h/host.c (MIT,
// (c) 2026 Krzysztof Kania; THIRD_PARTY.md). The differential tests link a stub instead
// (tests/difftest/host_stub.cpp).
#include "types.hpp"

struct SDL_Window;    // SDL3's opaque types, for the presentation layer (src/enhanced/)
struct SDL_Renderer;

namespace gb {

constexpr u32 PIT_HZ = 1193182;
// PIT channel 0 divisors used by GB.EXE (platform.md §1). 0 means 65536.
constexpr u16 PIT_DIV_BIOS = 0;         // 18.2065 Hz, the BIOS rate
constexpr u16 PIT_DIV_MENU = 0x3400;    // 89.63 Hz, timer_install 121b:0c9a (menus, music)
constexpr u16 PIT_DIV_MISSION = 0x13B1; // 236.695 Hz, sfx_install 12ed:08c0 (missions)

bool host_init(const char *game_dir, int window_scale, bool fullscreen);
void host_shutdown();

// PIT channel 0 and the INT 8 handler: the host calls handler (the port's version of the game's
// timer interrupt body) once per timer interrupt at PIT_HZ / divisor, from host_pump(). A change of
// divisor takes effect from the last tick on, as reprogramming the PIT does. handler may be null.
void host_set_timer(u16 divisor, void (*handler)());

// Source of the displayed image: fills an XRGB8888 frame, sets its size (*w x *h: 320x200, or up to
// 720x348 for a Hercules card) and returns true if it changed since the last call. Installed by the
// video model (platform/vga.cpp). Shown with 4:3 aspect.
constexpr int HOST_FRAME_MAX_W = 720;
constexpr int HOST_FRAME_MAX_H = 348;
void host_set_frame_source(bool (*compose)(u32 *xrgb, int *w, int *h), int w, int h);

// Runs the timer ticks that are due (and their audio), handles window events and presents the
// screen when it changed. Every busy-wait loop of the original (tick waits, key polls, delays)
// calls this once per iteration. Sleeps briefly when nothing was due.
void host_pump();

// Frame pacing of the 3D stations (PORT: simulation.md §1.1). The original simulates once per
// drawn frame with no frame limiter, as fast as the PC could draw; the port waits (pumping) until
// at least 1/fps s have passed since the previous call. fps = 0: no wait (the original's rule).
// The mission loop calls it once per pass on the 3D stations; the test stub does nothing.
void host_set_frame_rate(int fps);
void host_frame_pace();

// Waits for the start of the next vertical retrace of the emulated VGA (mode 13h: 70.086 Hz),
// pumping meanwhile. Replaces the port 3DAh polls.
void host_wait_vretrace();

// Keyboard: the game's INT 9 handler (kbd_isr 121b:0a9c, platform.md §2) receives the XT byte
// sequence the keyboard would send, in event order: normal keys sc / sc|80h, grey keys E0 sc /
// E0 sc|80h, Pause E1 1D 45 E1 9D C5, Print Screen E0 2A E0 37 / E0 B7 E0 AA. Key repeats feed the
// make code again.
void host_set_kbd_handler(void (*handler)(u8 byte));
// Called when the window loses keyboard focus (keys released outside it never send their break).
void host_set_focus_lost_handler(void (*handler)());

// Joystick (port 201h replacement, platform.md §3): the first connected gamepad. Axes
// -32768..32767, buttons bit 0 = A, bit 1 = B. False when there is none.
bool host_joy_read(s16 *x, s16 *y, u8 *buttons);

// Audio. Sound code writes OPL2 registers and the PC speaker as the original does; writes take
// effect from the current tick onward.
void host_opl_write(u8 reg, u8 value);
void host_speaker(u16 divisor, bool on);  // PIT channel 2 divisor (0 = 65536) and the port 61h gate
// PORT: the timer interrupt dispatch says whether the effects driver's handler (12ed:00a3) is the one
// running: its speaker changes are the sound effects, which the presentation layer can play on AdLib
// instead (host_set_speaker_filter). Returns the previous state.
bool host_speaker_effects(bool effects);
// PORT: events of the game for the presentation layer (the AdLib effects, the impact debris), each
// passed to every observer added (host_add_*_observer); nothing in mem[] changes.
// sfx_play: an effect's program starts (its DGROUP address).
void host_sfx_play(u16 program);
// projectile_impact: a shot of weapon `weapon` lands at (x, y) map units.
void host_shot_landed(u16 x, u16 y, u8 weapon);
// hit_objects: the shot hit object `obj` (its offset in the object arrays), of kind `kind` (before the
// damage; wrecks 30h/31h let the shot on).
void host_object_hit(u8 kind, u16 obj);
// hit_objects: the shot destroyed an object of kind `old` into its wreck `wreck`.
void host_target_destroyed(u8 old, u8 wreck);
// The low byte of PIT channel 2's counter (IN 42h): it counts down at 1.19 MHz, so it depends on the
// moment it is read.
u8 host_pit2_low();

// Game files: case-insensitive lookup in the game folder. Returns a path to free with host_free,
// or null if the file does not exist; with create = true, the path for a new file.
char *host_game_path(const char *name, bool create);
void host_free(void *p);

// Shows a message box, shuts down and exits with code 3.
[[noreturn]] void host_fatal(const char *fmt, ...);
// Shuts down and ends the program with this exit code (the runtime's exit()).
[[noreturn]] void host_exit(int code);
// Shows a message (stderr and a message box) and returns: the text a DOS program prints on its way
// out, which the port has no text screen for.
void host_error_box(const char *text);

// ---- The presentation layer (src/enhanced/: the launcher and the optional enhancements). It only
// shows what the game drew; the game never calls it. Without it the host shows the 320x200 frame
// in a 4:3 area as the VGA monitor did.
SDL_Window *host_window();
SDL_Renderer *host_renderer();
void host_set_game_dir(const char *dir);
// Replaces the host's own picture: at each present slot of host_pump (at most every 8 ms, VSync
// pacing it further) the presenter gets the composed frame (w x h) and whether it changed since the
// last slot (or the window needs a redraw), draws with host_renderer() and returns true if it
// presented.
void host_set_presenter(bool (*present)(const u32 *xrgb, int w, int h, bool changed));
// Called by host_frame_pace on entry: a 3D station's frame is complete in memory.
void host_set_frame_hook(void (*hook)());
// Called for each key press (SDL scancode) before the game gets it; true = the key is the
// presentation layer's (its press and release never reach the game).
void host_set_hotkey_handler(bool (*handler)(int scancode));
void host_set_fullscreen(bool on);
bool host_fullscreen();
// Sound effects on AdLib (sfx_adlib.cpp): every speaker change goes to the filter first with whether
// the effects driver's handler made it; when the filter returns true the change is its own and the
// speaker does not change, else the speaker takes it (with the gate the filter may have closed). The
// filter plays the effects on a second OPL2 chip (host_sfx_opl_write), mixed with the first. The tick
// observer is called after the handlers of each timer tick.
void host_set_speaker_filter(bool (*filter)(u16 divisor, bool &on, bool effects));
void host_sfx_opl_write(u8 reg, u8 value);
void host_set_sfx_gain(int gain);  // the second chip's output is mixed times this
void host_set_tick_observer(void (*observer)());
void host_add_sfx_play_observer(void (*observer)(u16 program));
void host_add_shot_landed_observer(void (*observer)(u16 x, u16 y, u8 weapon));
void host_add_object_hit_observer(void (*observer)(u8 kind, u16 obj));
void host_add_target_destroyed_observer(void (*observer)(u8 old, u8 wreck));
// Restarts the timer clock from now (the game starts after the launcher, not at host_init).
void host_reset_clock();

// Developer aids (environment variables):
//   GB_SNAPSHOT_DIR=dir   the screen is saved every 2 s as snapNNNN.bmp, changing or not (works
//                         with SDL_VIDEO_DRIVER=dummy)
//   GB_KEYS="<seconds>:<xt>[+<xt>...],..."  presses (in order) and releases (in reverse) the XT
//                         keys at that many seconds after start-up; "p" after the codes only
//                         presses (held), "r" only releases: "9:48p,20:48r" holds Up for 11 s.
//                         Grey keys are written with their E0 prefix, as e048.

} // namespace gb
