// The player's settings file (settings.hpp): "key = value" lines, unknown keys ignored.
#include "enhanced/settings.hpp"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace gb {

namespace {

bool truthy(const char *v) { return !std::strcmp(v, "1") || !SDL_strcasecmp(v, "yes") || !SDL_strcasecmp(v, "on"); }

void trim(char *&b)
{
    while (*b == ' ' || *b == '\t') b++;
    char *e = b + std::strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

} // namespace

const char *aspect_name(Aspect a) { return a == Aspect::Square ? "square" : "4:3"; }

const char *filter_name(Filter f)
{
    switch (f) {
    case Filter::Nearest: return "nearest";
    case Filter::Smooth: return "smooth";
    case Filter::Crt: return "crt";
    default: return "sharp";
    }
}

const char *effects_name(Effects e) { return e == Effects::Adlib ? "adlib" : "speaker"; }
const char *sound_name(Sound s) { return s == Sound::Adlib ? "adlib" : s == Sound::Speaker ? "speaker" : "auto"; }

const char *video_name(Video v)
{
    switch (v) {
    case Video::Ega: return "ega";
    case Video::Tandy: return "tandy";
    case Video::Cga: return "cga";
    case Video::Hercules: return "hercules";
    default: return "vga";
    }
}

const char *wide_name(Wide w) { return w == Wide::World ? "world" : w == Wide::Cockpit ? "cockpit" : "off"; }

namespace {

bool has_gb_exe(const std::filesystem::path &dir)
{
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string n = entry.path().filename().string();
        for (char &c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (n == "GB.EXE") return true;
    }
    return false;
}

} // namespace

std::string default_game_dir()
{
    namespace fs = std::filesystem;
    const char *base = SDL_GetBasePath();  // the folder of gunboat.exe (SDL owns the string)
    fs::path exe_dir = base ? fs::path(base) : fs::current_path();
#ifdef __APPLE__
    // A macOS app bundle: SDL's base path is Gunboat.app/Contents/Resources/; Game sits beside Gunboat.app.
    const std::string b = exe_dir.string();
    if (const auto app = b.find(".app/Contents/"); app != std::string::npos)
        exe_dir = fs::path(b.substr(0, app + 4)).parent_path();
#endif
    std::error_code ec;
    for (const fs::path &p : {exe_dir / "Game", exe_dir / ".." / "Game", exe_dir / ".." / ".." / "Game",
                              fs::current_path(ec) / "Game"})
        if (has_gb_exe(p)) return fs::weakly_canonical(p, ec).string();
    return (exe_dir / "Game").lexically_normal().string();
}

std::string game_dir_of(const Settings &s) { return s.game_dir.empty() ? default_game_dir() : s.game_dir; }

std::string settings_path()
{
    char *dir = SDL_GetPrefPath("", "Gunboat");
    std::string p = dir ? std::string(dir) + "gunboat.ini" : std::string("gunboat.ini");
    SDL_free(dir);
    return p;
}

bool settings_load(Settings &s)
{
    std::FILE *f = std::fopen(settings_path().c_str(), "r");
    if (!f) return false;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        char *eq = std::strchr(line, '=');
        if (line[0] == ';' || line[0] == '#' || !eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        trim(k);
        trim(v);
        if (!std::strcmp(k, "game_dir")) s.game_dir = v;
        else if (!std::strcmp(k, "launcher")) s.launcher = truthy(v);
        else if (!std::strcmp(k, "fullscreen")) s.fullscreen = truthy(v);
        else if (!std::strcmp(k, "window_scale")) s.window_scale = SDL_clamp(std::atoi(v), 1, 8);
        else if (!std::strcmp(k, "aspect")) s.aspect = !std::strcmp(v, "square") ? Aspect::Square : Aspect::Crt43;
        else if (!std::strcmp(k, "filter")) {
            s.filter = !std::strcmp(v, "nearest") ? Filter::Nearest
                       : !std::strcmp(v, "smooth") ? Filter::Smooth
                       : !std::strcmp(v, "crt")    ? Filter::Crt
                                                   : Filter::Sharp;
        }
        else if (!std::strcmp(k, "hires_view")) s.hires_view = truthy(v);
        else if (!std::strcmp(k, "smooth_motion")) s.smooth_motion = truthy(v);
        else if (!std::strcmp(k, "far_view")) s.far_view = truthy(v);
        else if (!std::strcmp(k, "debris")) s.debris = truthy(v);
        else if (!std::strcmp(k, "hills_stop_bullets")) s.hills_stop_bullets = truthy(v);
        else if (!std::strcmp(k, "widescreen")) {  // (the first settings files had 0 / 1)
            s.widescreen = !std::strcmp(v, "world")                    ? Wide::World
                           : !std::strcmp(v, "cockpit") || truthy(v) ? Wide::Cockpit
                                                                       : Wide::Off;
        }
        else if (!std::strcmp(k, "sound")) {
            s.sound = !std::strcmp(v, "adlib") ? Sound::Adlib : !std::strcmp(v, "speaker") ? Sound::Speaker : Sound::Auto;
        }
        else if (!std::strcmp(k, "effects")) s.effects = !std::strcmp(v, "adlib") ? Effects::Adlib : Effects::Speaker;
        else if (!std::strcmp(k, "fps")) s.fps = SDL_clamp(std::atoi(v), 0, 1000);
        else if (!std::strcmp(k, "video")) {
            s.video = !std::strcmp(v, "ega")        ? Video::Ega
                      : !std::strcmp(v, "tandy")    ? Video::Tandy
                      : !std::strcmp(v, "cga")      ? Video::Cga
                      : !std::strcmp(v, "hercules") ? Video::Hercules
                                                    : Video::Vga;
        }
    }
    std::fclose(f);
    return true;
}

bool settings_save(const Settings &s)
{
    std::FILE *f = std::fopen(settings_path().c_str(), "w");
    if (!f) return false;
    std::fprintf(f,
                 "; Gunboat port settings (written by the launcher)\n"
                 "game_dir = %s\n"
                 "launcher = %d\n"
                 "fullscreen = %d\n"
                 "window_scale = %d\n"
                 "aspect = %s\n"
                 "filter = %s\n"
                 "; enhancements (0 = as the original)\n"
                 "hires_view = %d\n"
                 "smooth_motion = %d\n"
                 "far_view = %d\n"
                 "debris = %d\n"
                 "widescreen = %s\n"
                 "sound = %s\n"
                 "effects = %s\n"
                 "video = %s\n"
                 "fps = %d\n"
                 "; gameplay changes (0 = as the original)\n"
                 "hills_stop_bullets = %d\n",
                 s.game_dir.c_str(), s.launcher, s.fullscreen, s.window_scale, aspect_name(s.aspect),
                 filter_name(s.filter), s.hires_view, s.smooth_motion, s.far_view, s.debris, wide_name(s.widescreen), sound_name(s.sound), effects_name(s.effects),
                 video_name(s.video), s.fps, s.hills_stop_bullets);
    return std::fclose(f) == 0;
}

} // namespace gb
