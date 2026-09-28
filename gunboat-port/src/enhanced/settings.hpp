#pragma once
// The player's settings (src/enhanced/): the game folder, the display, the sound device and the
// optional enhancements. They are saved in gunboat.ini in the user's settings folder
// (SDL_GetPrefPath: %APPDATA%\Gunboat on Windows) by the launcher, and command-line options override
// them for one run. The enhancements only change how the frames the game drew are shown; with all
// of them off the picture is the original's.
#include <string>

namespace gb {

enum class Aspect { Crt43, Square };               // 4:3 like the VGA monitor, or square pixels (16:10)
enum class Filter { Sharp, Nearest, Smooth, Crt };  // how the 320x200 picture is scaled
enum class Sound { Auto, Adlib, Speaker };          // Auto: AdLib when the game folder has ADLIB.COM
// The sound effects: on AdLib FM instruments (sfx_fm.hpp; the default), or on the PC speaker as in the
// original.
enum class Effects { Speaker, Adlib };
// In a window wider than the picture, on the 3D stations: black borders, the world drawn beside the
// picture, or the cockpit widened to the window's edges (widen.hpp).
enum class Wide { Off, World, Cockpit };
// The video card of the emulated machine: the original's graphics modes (VGA 256 colours, EGA and
// Tandy 16, CGA 4, Hercules monochrome).
enum class Video { Vga, Ega, Tandy, Cga, Hercules };

struct Settings {
    std::string game_dir;     // empty: the Game folder (default_game_dir)
    bool launcher = true;     // show the launcher at start
    bool fullscreen = false;
    int window_scale = 3;     // initial window: 320x240 (or 427x240 with widescreen) times this
    Aspect aspect = Aspect::Crt43;
    Filter filter = Filter::Sharp;
    // The enhancements (all off: the original picture).
    bool hires_view = true;     // the 3D view drawn at the window's resolution
    bool smooth_motion = true;  // 60 fps: the 3D view interpolated between the game's frames
    Wide widescreen = Wide::Cockpit;
    bool far_view = true;       // the extended draw distance: terrain and objects beyond the game's window
    bool debris = true;         // impact debris: particles where the shots hit (debris.hpp)
    Sound sound = Sound::Auto;
    Effects effects = Effects::Adlib;
    Video video = Video::Vga;
    int fps = 15;               // frame rate of the 3D stations (the mission clock; host_set_frame_rate)
    // Gameplay changes (gameplay.hpp): they change the game itself, so they are off unless the player
    // turns them on, and the Original preset turns them off.
    bool hills_stop_bullets = false;

    bool any_enhancement() const { return hires_view || smooth_motion || widescreen != Wide::Off || far_view || debris; }
    bool all_enhancements() const
    {
        return hires_view && smooth_motion && widescreen == Wide::Cockpit && far_view && debris;
    }
    void set_original()
    {
        hires_view = smooth_motion = far_view = debris = hills_stop_bullets = false;
        widescreen = Wide::Off;
    }
    void set_enhanced()
    {
        hires_view = smooth_motion = far_view = debris = true;
        widescreen = Wide::Cockpit;
    }
};

// Loads gunboat.ini (defaults for what it lacks); false when there is none yet.
bool settings_load(Settings &s);
bool settings_save(const Settings &s);
std::string settings_path();

// The Game folder, where the player puts the original files (as in the Test Drive III port): the
// first of "Game" next to gunboat.exe, one or two folders above it (a build in gunboat-port/build of
// the repository) and in the current folder that holds GB.EXE; else the one next to gunboat.exe.
std::string default_game_dir();
// The folder in use: the one chosen, or the Game folder.
std::string game_dir_of(const Settings &s);

const char *aspect_name(Aspect a);
const char *filter_name(Filter f);
const char *sound_name(Sound s);
const char *effects_name(Effects e);
const char *wide_name(Wide w);
const char *video_name(Video v);

} // namespace gb
