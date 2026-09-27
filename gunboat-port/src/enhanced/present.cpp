// The enhanced presentation (present.hpp).
//
// Each present: the 320x200 frame the game drew becomes a texture with the player's filter, placed in
// the window with the player's aspect. While a 3D station runs with the enhancements on, the pixels of
// that frame that show the 3D view (the capture's map, still holding the captured view: anything
// the game drew over them since stays) are left transparent and the view is drawn under them again at
// the window's resolution: the parts of page 1's view window each run of them shows, and in a
// window wider than the picture, the world beside it (as seen through the outermost part of the
// cockpit's opening). Smooth motion draws the view for the moment between the last two captured
// frames, one frame behind the game. With neither the high-resolution view nor smooth motion, the
// view in the cockpit stays the original's pixels (only the world beside the picture is drawn).
#include "enhanced/present.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "enhanced/capture.hpp"
#include "enhanced/controller.hpp"
#include "enhanced/debris.hpp"
#include "enhanced/gameplay.hpp"
#include "enhanced/icon.hpp"
#include "enhanced/sfx_adlib.hpp"
#include "enhanced/view3d.hpp"
#include "enhanced/widen.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "platform/card.hpp"
#include "platform/vga.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

Settings cfg;
bool enhanced_on = true;  // F11
constexpr int FAR_RADIUS = 5;  // the extended draw distance: 11 x 11 cells around the boat's
std::unique_ptr<Scene> scene_store[2];
Scene *cur, *prev;

SDL_Texture *cockpit_tex;
int cockpit_w;
std::vector<u32> cockpit_px;
bool hole[320 * 200];
u32 pal[256];

struct ViewTex {
    SDL_Texture *tex = nullptr;
    int w = 0, h = 0;
    std::vector<u8> idx;
};
ViewTex win_tex, left_tex, right_tex;

// The enhancements draw the VGA game (mode 13h: the capture reads its pages); the other video cards
// show their own pictures as they are.
bool view_wanted() { return enhanced_on && cfg.any_enhancement() && card_machine() == Machine::Vga; }

SDL_ScaleMode filter_mode()
{
    switch (cfg.filter) {
    case Filter::Nearest: return SDL_SCALEMODE_NEAREST;
    case Filter::Smooth: return SDL_SCALEMODE_LINEAR;
    default: return SDL_SCALEMODE_PIXELART;
    }
}

void read_palette()
{
    for (int i = 0; i < 256; i++) {
        u8 r, g, b;
        vga_dac_read(u8(i), &r, &g, &b);
        pal[i] = 0xFF000000u | u32(r << 2 | r >> 4) << 16 | u32(g << 2 | g >> 4) << 8 | u32(b << 2 | b >> 4);
    }
}

// ---- developer check (GB_VIEW_CHECK): the view drawn again at 1x against the original's pixels
struct Check {
    bool on = false;
    const char *dir = nullptr;
    unsigned long long compared = 0, equal = 0;
    unsigned frames = 0, saved = 0, memory_changed = 0;
    double worst = 1.0;
} check;

void save_indexed(const char *path, const u8 *px, int w, int h, int scale)
{
    SDL_Surface *s = SDL_CreateSurface(w * scale, h * scale, SDL_PIXELFORMAT_XRGB8888);
    if (!s) return;
    for (int y = 0; y < h * scale; y++) {
        u32 *row = reinterpret_cast<u32 *>(static_cast<u8 *>(s->pixels) + size_t(y) * s->pitch);
        for (int x = 0; x < w * scale; x++) row[x] = pal[px[(y / scale) * w + x / scale]];
    }
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

void check_frame(const Scene &sc)
{
    static std::vector<u8> px(VIEW_W * VIEW_H);
    ViewTarget t;
    t.px = px.data();
    t.w = VIEW_W;
    t.h = VIEW_H;
    t.ox = VIEW_X;
    t.oy = VIEW_Y;
    t.far = false;  // the original has no far cells
    view3d_render(sc, nullptr, 1.0, t);
    unsigned long long n = 0, same = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        if (sc.overlay[i]) continue;
        n++;
        same += px[i] == sc.window[i];
    }
    check.compared += n;
    check.equal += same;
    check.frames++;
    if (n) check.worst = std::min(check.worst, double(same) / double(n));
    if (check.frames % 60 == 0) enhanced_report();
    if (check.dir && check.frames % 15 == 1 && check.saved < 40) {
        read_palette();
        char path[512];
        std::snprintf(path, sizeof path, "%s/view%03u_orig.bmp", check.dir, check.saved);
        save_indexed(path, sc.window, VIEW_W, VIEW_H, 4);
        std::snprintf(path, sizeof path, "%s/view%03u_1x.bmp", check.dir, check.saved);
        save_indexed(path, px.data(), VIEW_W, VIEW_H, 4);
        static std::vector<u8> big(VIEW_W * 4 * VIEW_H * 4);
        t.px = big.data();
        t.w = VIEW_W * 4;
        t.h = VIEW_H * 4;
        t.sx = t.sy = 4;
        view3d_render(sc, nullptr, 1.0, t);
        std::snprintf(path, sizeof path, "%s/view%03u_4x.bmp", check.dir, check.saved);
        save_indexed(path, big.data(), VIEW_W * 4, VIEW_H * 4, 1);
        if (sc.far) {  // the same frame with the extended draw distance
            t.far = true;
            view3d_render(sc, nullptr, 1.0, t);
            std::snprintf(path, sizeof path, "%s/view%03u_4x_far.bmp", check.dir, check.saved);
            save_indexed(path, big.data(), VIEW_W * 4, VIEW_H * 4, 1);
        }
        check.saved++;
    }
}

// ---- the frame hook: a 3D station's frame is complete
unsigned captures;  // for the present statistics
Uint64 capture_ns;

void frame_hook()
{
    captures++;
    if (!view_wanted() && !check.on) return;
    const Uint64 t0 = SDL_GetTicksNS();
    std::swap(cur, prev);
    // The check also proves that a capture leaves the game's memory as it found it.
    static std::vector<u8> before;
    if (check.on) before.assign(mem, mem + MEM_SIZE);
    scene_capture(*cur, cfg.far_view ? FAR_RADIUS : 0, prev);
    capture_ns += SDL_GetTicksNS() - t0;
    if (check.on) {
        if (std::memcmp(before.data(), mem, MEM_SIZE) != 0) check.memory_changed++;
        check_frame(*cur);
    }
}

// ---- F12: a screenshot. The next picture shown is saved in the settings folder's Screenshots:
// shot_NNNN.bmp (the window as shown), shot_NNNN_original.bmp (the game's own picture) and
// shot_NNNN.mem (the game's memory then, for looking into what the picture shows). A note on the
// picture (drawn after the shot, so not in it) and the window's title name it a few seconds.
bool shot_pending;
Uint64 title_until;
char shot_name[32];

void take_shot(SDL_Renderer *r, const u32 *frame, int w, int h)
{
    if (!shot_pending) return;
    shot_pending = false;
    char *pref = SDL_GetPrefPath("", "Gunboat");
    if (!pref) return;
    const std::string dir = std::string(pref) + "Screenshots/";
    SDL_free(pref);
    SDL_CreateDirectory(dir.c_str());
    int n = 1;
    char name[32];
    for (;; n++) {
        std::snprintf(name, sizeof name, "shot_%04d", n);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo((dir + name + ".bmp").c_str(), &info)) break;
    }
    const std::string base = dir + name;
    std::snprintf(shot_name, sizeof shot_name, "%s", name);
    title_until = SDL_GetTicksNS() + 3 * SDL_NS_PER_SECOND;
    if (SDL_Surface *shown = SDL_RenderReadPixels(r, nullptr)) {
        SDL_SaveBMP(shown, (base + ".bmp").c_str());
        SDL_DestroySurface(shown);
    }
    if (SDL_Surface *orig = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, const_cast<u32 *>(frame), w * 4)) {
        SDL_SaveBMP(orig, (base + "_original.bmp").c_str());
        SDL_DestroySurface(orig);
    }
    if (std::FILE *f = std::fopen((base + ".mem").c_str(), "wb")) {
        std::fwrite(mem, 1, MEM_SIZE, f);
        std::fclose(f);
    }
    if (SDL_Window *win = host_window())
        SDL_SetWindowTitle(win, (std::string("Gunboat - saved ") + name + " in " + dir).c_str());
}

// While the note is up, and once more when it goes (the picture without it): the picture is drawn
// again even when the game's frame has not changed.
bool title_back()
{
    if (!title_until) return false;
    if (SDL_GetTicksNS() > title_until) {
        title_until = 0;
        if (SDL_Window *win = host_window()) SDL_SetWindowTitle(win, "Gunboat");
    }
    return true;
}

// The note in the picture's top right corner, in SDL's debug font at about the game's text size.
void shot_note(SDL_Renderer *r, int ow, int oh)
{
    if (!title_until) return;
    char text[64];
    std::snprintf(text, sizeof text, "Screenshot saved: %s", shot_name);
    const float s = float(std::max(1, oh / 400));
    const float cw = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    const float tw = std::strlen(text) * cw, pad = 4;
    const float x = ow / s - tw - 2 * pad - 8, y = 8;
    SDL_SetRenderScale(r, s, s);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 192);
    const SDL_FRect box = {x, y, tw + 2 * pad, cw + 2 * pad};
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, x + pad, y + pad, text);
    SDL_SetRenderScale(r, 1, 1);
}

bool hotkey(int scancode)
{
    if (scancode == SDL_SCANCODE_F12) {
        shot_pending = true;  // the host redraws after a hotkey: the shot is taken there
        return true;
    }
    if (scancode != SDL_SCANCODE_F11) return false;
    enhanced_on = !enhanced_on;
    cur->valid = prev->valid = false;
    return true;
}

// ---- layout
struct Layout {
    int ow = 0, oh = 0;
    SDL_FRect pic{};
    double sx = 1, sy = 1;  // output pixels per page pixel
};

Layout layout(SDL_Renderer *r)
{
    Layout l;
    SDL_GetRenderOutputSize(r, &l.ow, &l.oh);
    const double aspect = cfg.aspect == Aspect::Square ? 1.6 : 4.0 / 3.0;
    double w = l.oh * aspect, h = l.oh;
    if (w > l.ow) {
        w = l.ow;
        h = w / aspect;
    }
    w = std::floor(w + 0.5);
    h = std::floor(h + 0.5);
    l.pic = {float(std::floor((l.ow - w) / 2)), float(std::floor((l.oh - h) / 2)), float(w), float(h)};
    l.sx = w / 320.0;
    l.sy = h / 200.0;
    return l;
}

// The horizon line of the enhanced view as haze: the original's two rows of colour 8 (grey, and the
// explosion flash's colour) drawn as a band that fades from the sky into a light haze at its middle
// and on into the water, the haze reaching a few rows up into the sky. The haze is between the
// water and the sky, a little paler and bluer by day. (Only where the enhanced view is drawn; the
// original picture keeps its line.)
u32 mix(u32 a, u32 b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    u32 out = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        const double x = double(a >> s & 0xFF) + (double(b >> s & 0xFF) - double(a >> s & 0xFF)) * t;
        out |= u32(std::lround(x)) << s;
    }
    return out;
}

double smooth(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

class Haze {
public:
    static constexpr double ABOVE = 3.0;  // page rows of sky the haze reaches into

    // The haze: halfway between the sky and the water, a little paler by day (blue-white), and never
    // darker than the water (under a dark sky it would be a dark stripe).
    explicit Haze(const ViewHorizon &h) : y_(h.y), sky_(pal[h.sky]), water_(pal[h.water])
    {
        haze_ = mix(sky_, water_, 0.5);
        const double light = lum(sky_) / 160.0;
        haze_ = mix(haze_, 0xFFA0C8EBu, 0.25 * std::min(1.0, light));  // pale by day
        if (lum(haze_) < lum(water_)) haze_ = mix(haze_, water_, 1.0);
    }

    // The colours of page row y: `band` for its colour 8 pixels, `above` for its sky pixels (0 for
    // none); false when the row is outside the haze.
    bool row(double y, u32 &band, u32 &above) const
    {
        band = above = 0;
        if (y < y_ - ABOVE || y >= y_ + 2) return false;
        const double mid = y_ + 1;
        const u32 c = y < mid ? mix(sky_, haze_, smooth((y - (y_ - ABOVE)) / (ABOVE + 1)))
                              : mix(haze_, water_, smooth(y - mid));
        if (y >= y_) band = c;
        else above = c;
        return true;
    }

private:
    static double lum(u32 c)
    {
        return 0.3 * double(c >> 16 & 0xFF) + 0.59 * double(c >> 8 & 0xFF) + 0.11 * double(c & 0xFF);
    }
    double y_;
    u32 sky_, water_, haze_;
};

// Renders the view rectangle of page coordinates (ox, oy, w x h page pixels) into vt at the scale.
void render_region(ViewTex &vt, double ox, double oy, double pw, double ph, double sx, double sy, double t,
                   SDL_ScaleMode mode)
{
    SDL_Renderer *r = host_renderer();
    const int w = std::max(1, int(std::ceil(pw * sx))), h = std::max(1, int(std::ceil(ph * sy)));
    if (!vt.tex || vt.w != w || vt.h != h) {
        if (vt.tex) SDL_DestroyTexture(vt.tex);
        vt.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        vt.w = w;
        vt.h = h;
        vt.idx.assign(size_t(w) * h, 0);
    }
    if (!vt.tex) return;
    SDL_SetTextureScaleMode(vt.tex, mode);
    ViewTarget target;
    target.px = vt.idx.data();
    target.w = w;
    target.h = h;
    target.ox = ox;
    target.oy = oy;
    target.sx = sx;
    target.sy = sy;
    const bool interpolate = cfg.smooth_motion && prev && prev->valid;
    const ViewHorizon hz = view3d_render(*cur, interpolate ? prev : nullptr, t, target);
    void *pixels;
    int pitch;
    if (!SDL_LockTexture(vt.tex, nullptr, &pixels, &pitch)) return;
    const Haze haze(hz);
    for (int y = 0; y < h; y++) {
        u32 *row = reinterpret_cast<u32 *>(static_cast<u8 *>(pixels) + size_t(y) * pitch);
        const u8 *src = vt.idx.data() + size_t(y) * w;
        u32 band = 0, above = 0;  // the haze colours of this row for colour 8 and for the sky
        const bool hazy = haze.row(oy + (y + 0.5) / sy, band, above);
        for (int x = 0; x < w; x++) {
            const u8 c = src[x];
            row[x] = pal[c];
            if (hazy) {
                if (c == 8 && band) row[x] = band;
                else if (c == hz.sky && above) row[x] = above;
            }
        }
    }
    if (cfg.debris)
        debris_draw(view3d_projection(*cur, interpolate ? prev : nullptr, t), target, static_cast<u32 *>(pixels), pitch,
                    pal, hz.water);
    SDL_UnlockTexture(vt.tex);
}

// Where the frame shows the captured view: hole[] for the displayed cells; returns the number of
// cells mapped to the view and of those still showing it.
void find_holes(int &mapped, int &shown)
{
    mapped = shown = 0;
    const u8 *vram = mp(VRAM_SEG, 0);
    const u16 start = vga_start();
    for (int i = 0; i < 64000; i++) {
        const u16 p = u16(start + i);
        hole[i] = false;
        if (p >= 64000) continue;
        const u16 m = cur->map[p];
        if (m == NOT_VIEW) continue;
        mapped++;
        if (vram[p] == cur->window[(m >> 8) * VIEW_W + (m & 0xFF)]) {
            hole[i] = true;
            shown++;
        }
    }
}

// The page 1 position minus the displayed position of the cell (x, y), a hole.
void translation(int x, int y, int &tx, int &ty)
{
    const u16 m = cur->map[u16(vga_start() + y * 320 + x)];
    tx = VIEW_X + (m & 0xFF) - x;
    ty = VIEW_Y + (m >> 8) - y;
}

// The translations of the outermost view cells on each side, if the view's openings show it at no
// more than two offsets (the gun stations, the pilot looking ahead, the chase view; the pilot's side
// windows are sheared and get no extension).
bool side_translations(int &ltx, int &lty, int &rtx, int &rty)
{
    int kinds = 0, seen[3][2];
    int lx = 1 << 30, rx = -1;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            if (!hole[y * 320 + x]) continue;
            int tx, ty;
            translation(x, y, tx, ty);
            bool known = false;
            for (int k = 0; k < kinds; k++) known |= seen[k][0] == tx && seen[k][1] == ty;
            if (!known) {
                if (kinds == 2) return false;
                seen[kinds][0] = tx;
                seen[kinds][1] = ty;
                kinds++;
            }
            if (x < lx) {
                lx = x;
                ltx = tx;
                lty = ty;
            }
            if (x > rx) {
                rx = x;
                rtx = tx;
                rty = ty;
            }
        }
    return rx >= 0;
}

struct Snapshot {
    const char *dir = nullptr;
    bool checked = false;
    Uint64 last = 0;
    int n = 0;
} snap;

void snapshot(SDL_Renderer *r)
{
    if (!snap.checked) {
        snap.dir = SDL_getenv("GB_SNAPSHOT_DIR");
        snap.checked = true;
    }
    if (!snap.dir) return;
    const Uint64 now = SDL_GetTicksNS();
    if (snap.n && now - snap.last < 2 * SDL_NS_PER_SECOND) return;
    snap.last = now;
    SDL_Surface *s = SDL_RenderReadPixels(r, nullptr);
    if (!s) return;
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/hd%04d.bmp", snap.dir, snap.n++);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

// Developer aid (GB_PRESENT_STATS=1): presents per second and the time spent drawing them.
void stats(Uint64 start, bool live, bool animating)
{
    static int on = -1;
    static Uint64 since, busy;
    static int n, n_live, n_anim;
    if (on < 0) {
        const char *e = SDL_getenv("GB_PRESENT_STATS");
        on = e && *e && *e != '0';
        since = start;
    }
    if (!on) return;
    busy += SDL_GetTicksNS() - start;
    n++;
    n_live += live;
    n_anim += animating;
    if (start - since >= 5 * SDL_NS_PER_SECOND) {
        int w, h;
        SDL_GetRenderOutputSize(host_renderer(), &w, &h);
        std::printf("present: %dx%d, %.1f/s (%d with the view, %d between frames), %.2f ms each; game %.1f frames/s, "
                    "capture %.2f ms each\n",
                    w, h, n * 1e9 / double(start - since), n_live, n_anim, busy / 1e6 / (n ? n : 1),
                    captures * 1e9 / double(start - since), capture_ns / 1e6 / (captures ? captures : 1));
        captures = 0;
        capture_ns = 0;
        std::fflush(stdout);
        since = start;
        busy = 0;
        n = n_live = n_anim = 0;
    }
}

// ---- the wide cockpit
constexpr int MAX_WIDEN = 28;  // columns added on a side at most (enough for 16:9 with square pixels)
Widening widening;
struct WideKey {  // what the widening was made for
    int ow = 0, oh = 0, side = 0;
    u16 station = 0, look = 0;
    u8 chase = 0, bow = 0, midship = 0, stern = 0;
    bool operator==(const WideKey &o) const
    {
        return ow == o.ow && oh == o.oh && side == o.side && station == o.station && look == o.look &&
               chase == o.chase && bow == o.bow && midship == o.midship && stern == o.stern;
    }
};
WideKey wide_key;
bool wide_made, wide_settled;

// The key without the window's part.
WideKey station_key_only(WideKey k)
{
    k.ow = k.oh = k.side = 0;
    return k;
}

WideKey station_key(const Scene &s)
{
    WideKey k;
    k.station = s.u16_at(DS_station);
    k.look = s.u16_at(DS_look_direction);
    k.chase = s.u8_at(DS_chase_view);
    k.bow = s.u8_at(DS_bow_weapon);
    k.midship = s.u8_at(DS_midship_weapon);
    k.stern = s.u8_at(DS_stern_weapon);
    return k;
}

// The widening is made again when the window or the station's cockpit changes. A new station's
// cockpit is drawn in the pass after the capture that first shows it, so a widening made before two
// captures agree is made again once they do. On a station, every pixel that has shown the view is
// free to widen (the gun frames slide over it), and the pixels seen changing outside it (digits,
// needles, lamps) are avoided: when one changes on a repeated column, the widening is made again
// around it (at most four times a second), so it settles in the first seconds on a station.
bool ever_view[64000], changing[64000];
std::vector<u32> last_frame(64000);
Uint64 wide_made_ns;

void update_widening(const Layout &l, const u32 *frame, int side)
{
    WideKey k = station_key(*cur);
    k.ow = l.ow;
    k.oh = l.oh;
    k.side = side;
    const bool settled = prev && prev->valid && station_key(*prev) == station_key(*cur);
    const bool window = !wide_made || k.ow != wide_key.ow || k.oh != wide_key.oh || k.side != wide_key.side;
    const bool station = settled && (!(station_key(*cur) == station_key_only(wide_key)) || !wide_settled);
    if (station) {  // a new cockpit: nothing learnt about it yet
        std::fill(std::begin(ever_view), std::end(ever_view), false);
        std::fill(std::begin(changing), std::end(changing), false);
        std::copy(frame, frame + 64000, last_frame.begin());
    }
    const u16 start = vga_start();
    bool hit = false;
    for (int i = 0; i < 64000; i++) {
        const u16 p = u16(start + i);
        if (p < 64000 && cur->map[p] != NOT_VIEW) ever_view[i] = true;
        if (frame[i] != last_frame[size_t(i)] && !ever_view[i] && !changing[i]) {
            changing[i] = true;
            hit |= wide_made && widening.repeated[size_t(i)];
        }
        last_frame[size_t(i)] = frame[i];
    }
    const Uint64 now = SDL_GetTicksNS();
    if (!window && !station && !(hit && now - wide_made_ns > 250 * SDL_NS_PER_MS)) return;
    widen_build(widening, frame, ever_view, changing, side, side, 12);  // rows 0-11: the message line
    wide_key = k;
    wide_made = true;
    wide_settled = settled;
    wide_made_ns = now;
}

Layout last_layout;
bool was_live;

// The frame of another video card (EGA, CGA, Tandy, Hercules) as it is, in a 4:3 picture with the
// player's filter.
SDL_Texture *plain_tex;
int plain_w, plain_h;

bool present_plain(const u32 *frame, int w, int h, bool changed)
{
    SDL_Renderer *r = host_renderer();
    int ow, oh;
    SDL_GetRenderOutputSize(r, &ow, &oh);
    const bool resized = ow != last_layout.ow || oh != last_layout.oh;
    last_layout.ow = ow;
    last_layout.oh = oh;
    if (!changed && !resized) return false;
    if (!plain_tex || plain_w != w || plain_h != h) {
        if (plain_tex) SDL_DestroyTexture(plain_tex);
        plain_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        plain_w = w;
        plain_h = h;
    }
    if (!plain_tex) return false;
    SDL_SetTextureScaleMode(plain_tex, filter_mode());
    SDL_UpdateTexture(plain_tex, nullptr, frame, w * 4);
    double pw = oh * 4.0 / 3.0, ph = oh;
    if (pw > ow) {
        pw = ow;
        ph = pw * 3.0 / 4.0;
    }
    const SDL_FRect pic = {float(std::floor((ow - pw) / 2)), float(std::floor((oh - ph) / 2)), float(std::floor(pw)),
                           float(std::floor(ph))};
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderTexture(r, plain_tex, nullptr, &pic);
    const double sy = pic.h / h;
    if (cfg.filter == Filter::Crt && sy >= 2) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 96);
        for (int y = 0; y < h; y++) {
            const SDL_FRect band = {pic.x, pic.y + float((y + 0.6) * sy), pic.w, float(0.4 * sy)};
            SDL_RenderFillRect(r, &band);
        }
    }
    take_shot(r, frame, w, h);
    shot_note(r, ow, oh);
    snapshot(r);
    SDL_RenderPresent(r);
    return true;
}

bool present(const u32 *frame, int w, int h, bool changed)
{
    icon_frame(frame, w, h);
    if (title_back() || shot_pending) changed = true;
    if (w != 320 || h != 200 || card_machine() != Machine::Vga) return present_plain(frame, w, h, changed);
    SDL_Renderer *r = host_renderer();
    const Layout l = layout(r);
    const bool resized = l.ow != last_layout.ow || l.oh != last_layout.oh;
    last_layout = l;
    const Uint64 now = SDL_GetTicksNS();

    // Is the captured view on screen?
    bool live = false;
    if (view_wanted() && cur && cur->valid) {
        int mapped, shown;
        find_holes(mapped, shown);
        live = shown > 0 && (now - cur->time_ns < 300 * SDL_NS_PER_MS || shown * 10 >= mapped * 9);
    }
    double t = 1.0;
    bool animating = false;
    if (live && cfg.smooth_motion && prev && prev->valid && view3d_compatible(*prev, *cur)) {
        const double period = std::clamp(double(cur->time_ns - prev->time_ns), 20e6, 200e6);
        t = std::min(1.0, double(now - cur->time_ns) / period);
        animating = t < 1.0;
    }
    if (!changed && !animating && !resized && live == was_live) return false;
    was_live = live;

    read_palette();
    // The wide cockpit: on the 3D stations the frame widened to the window's edges.
    const bool wide = live && cfg.widescreen == Wide::Cockpit && l.pic.x >= 1;
    // Columns added on each side: as many as the window needs, at most MAX_WIDEN (then the widened
    // frame is drawn a little wider, all of it alike, the view too).
    const int side = wide ? std::min(int(std::ceil(l.pic.x / l.sx)), MAX_WIDEN) : 0;
    if (wide) update_widening(l, frame, side);
    const int fw = wide ? widening.width : 320;  // the columns of the frame as shown
    auto source = [&](int x, int y) { return wide ? int(widening.at(x, y)) : x; };
    const double xs = wide ? double(l.ow) / fw : l.sx;  // output pixels per shown column
    const SDL_FRect pic = wide ? SDL_FRect{0, l.pic.y, float(l.ow), l.pic.h} : l.pic;
    // The view drawn again: for the high-resolution view, smooth motion, and the widened openings.
    const bool redraw = live && (cfg.hires_view || cfg.smooth_motion || wide);

    if (!cockpit_tex || cockpit_w != fw) {
        if (cockpit_tex) SDL_DestroyTexture(cockpit_tex);
        cockpit_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fw, 200);
        SDL_SetTextureBlendMode(cockpit_tex, SDL_BLENDMODE_BLEND);
        cockpit_w = fw;
        cockpit_px.assign(size_t(fw) * 200, 0);
    }
    SDL_SetTextureScaleMode(cockpit_tex, filter_mode());
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < fw; x++) {
            const int p = y * 320 + source(x, y);
            cockpit_px[size_t(y) * fw + x] = redraw && hole[p] ? (frame[p] & 0x00FFFFFFu) : (frame[p] | 0xFF000000u);
        }
    SDL_UpdateTexture(cockpit_tex, nullptr, cockpit_px.data(), fw * 4);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    const double rsx = cfg.hires_view ? xs : 1.0, rsy = cfg.hires_view ? l.sy : 1.0;
    const SDL_ScaleMode mode = cfg.hires_view ? SDL_SCALEMODE_NEAREST : filter_mode();
    // The world beside the picture.
    int ltx, lty, rtx, rty;
    if (live && cfg.widescreen == Wide::World && l.pic.x >= 1 && cur->u8_at(DS_chase_view) == 0 &&
        side_translations(ltx, lty, rtx, rty)) {  // (the chase view is framed in black: no extension)
        const double w = l.pic.x / l.sx, top = -l.pic.y / l.sy, height = l.oh / l.sy;
        render_region(left_tex, -w + ltx, top + lty, w, height, rsx, rsy, t, mode);
        render_region(right_tex, 320 + rtx, top + rty, w, height, rsx, rsy, t, mode);
        const SDL_FRect ld = {0, 0, l.pic.x, float(l.oh)};
        const SDL_FRect rd = {l.pic.x + l.pic.w, 0, float(l.ow) - (l.pic.x + l.pic.w), float(l.oh)};
        const SDL_FRect src = {0, 0, float(w * rsx), float(height * rsy)};
        SDL_RenderTexture(r, left_tex.tex, &src, &ld);
        SDL_RenderTexture(r, right_tex.tex, &src, &rd);
    }
    if (redraw) {
        // The view through the cockpit's openings: the holes in rectangles of one offset each (runs
        // of a row merged with the same run of the rows below), their edges on whole output pixels.
        // A shown column x is page 1's column x - side + the offset of the frame column it shows, so
        // a widened opening shows the world beyond the original one.
        struct Rect {
            int x0, x1, y0, y1, tx, ty;
        };
        static std::vector<Rect> rects, reached;
        rects.clear();
        size_t open_from = 0;  // rects that reach the previous row start here
        for (int y = 0; y < 200; y++) {
            const size_t row_from = rects.size();
            for (int x = 0; x < fw;) {
                if (!hole[y * 320 + source(x, y)]) {
                    x++;
                    continue;
                }
                int tx, ty;
                translation(source(x, y), y, tx, ty);
                int x1 = x + 1;
                for (int ax, ay; x1 < fw && hole[y * 320 + source(x1, y)]; x1++) {
                    translation(source(x1, y), y, ax, ay);
                    if (ax != tx || ay != ty) break;
                }
                bool merged = false;
                for (size_t k = open_from; k < row_from && !merged; k++) {
                    Rect &q = rects[k];
                    if (q.y1 == y && q.x0 == x && q.x1 == x1 && q.tx == tx && q.ty == ty) {
                        q.y1 = y + 1;
                        merged = true;
                    }
                }
                if (!merged) rects.push_back({x, x1, y, y + 1, tx, ty});
                x = x1;
            }
            // keep the rects that reached this row together at the end for the next row's search
            size_t w = open_from;
            reached.clear();
            for (size_t k = open_from; k < rects.size(); k++)
                if (rects[k].y1 == y + 1) reached.push_back(rects[k]);
                else rects[w++] = rects[k];
            rects.resize(w);
            open_from = rects.size();
            rects.insert(rects.end(), reached.begin(), reached.end());
        }
        if (!rects.empty()) {
            // The part of page 1 the rectangles show, drawn again.
            int px0 = 1 << 30, px1 = -(1 << 30), py0 = 1 << 30, py1 = -(1 << 30);
            for (const Rect &q : rects) {
                px0 = std::min(px0, q.x0 - side + q.tx);
                px1 = std::max(px1, q.x1 - side + q.tx);
                py0 = std::min(py0, q.y0 + q.ty);
                py1 = std::max(py1, q.y1 + q.ty);
            }
            render_region(win_tex, px0, py0, px1 - px0, py1 - py0, rsx, rsy, t, mode);
            for (const Rect &q : rects) {
                const float X0 = std::round(pic.x + float(q.x0 * xs)), X1 = std::round(pic.x + float(q.x1 * xs));
                const float Y0 = std::round(pic.y + float(q.y0 * l.sy)), Y1 = std::round(pic.y + float(q.y1 * l.sy));
                const SDL_FRect dst = {X0, Y0, X1 - X0, Y1 - Y0};
                const SDL_FRect src = {float(((X0 - pic.x) / xs - side + q.tx - px0) * rsx),
                                       float(((Y0 - pic.y) / l.sy + q.ty - py0) * rsy), float((X1 - X0) / xs * rsx),
                                       float((Y1 - Y0) / l.sy * rsy)};
                SDL_RenderTexture(r, win_tex.tex, &src, &dst);
            }
        }
    }
    SDL_RenderTexture(r, cockpit_tex, nullptr, &pic);
    if (cfg.filter == Filter::Crt && l.sy >= 2) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 96);
        for (int y = 0; y < 200; y++) {
            const SDL_FRect band = {pic.x, pic.y + float((y + 0.6) * l.sy), pic.w, float(0.4 * l.sy)};
            SDL_RenderFillRect(r, &band);
        }
    }
    stats(now, live, animating);
    take_shot(r, frame, w, h);
    shot_note(r, l.ow, l.oh);
    snapshot(r);
    SDL_RenderPresent(r);
    return true;
}

} // namespace

void enhanced_report()
{
    if (!check.on) return;
    std::printf("view check: %u frames, %llu pixels, %.3f%% equal (worst frame %.3f%%), memory changed by %u captures\n",
                check.frames, check.compared,
                check.compared ? 100.0 * double(check.equal) / double(check.compared) : 0.0, 100.0 * check.worst,
                check.memory_changed);
    std::fflush(stdout);
}

void enhanced_install(const Settings &s)
{
    cfg = s;
    for (auto &p : scene_store) p.reset(new Scene());
    cur = scene_store[0].get();
    prev = scene_store[1].get();
    const char *c = SDL_getenv("GB_VIEW_CHECK");
    check.on = c && *c && *c != '0';
    check.dir = SDL_getenv("GB_VIEW_CHECK_DIR");
    if (check.on) std::atexit(enhanced_report);
    SDL_Window *w = host_window();
    if (w && !s.fullscreen && !host_fullscreen()) {
        const int base_h = s.aspect == Aspect::Square ? 200 : 240;
        const int base_w = s.widescreen != Wide::Off ? base_h * 16 / 9 : 320;
        int ww = base_w * s.window_scale, wh = base_h * s.window_scale;
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(w), &usable)) {
            while (s.window_scale > 1 && (ww > usable.w || wh > usable.h - 40) && wh > base_h) {
                ww -= base_w;
                wh -= base_h;
            }
        }
        SDL_SetWindowSize(w, ww, wh);
        SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    host_set_fullscreen(s.fullscreen);
    host_set_frame_hook(frame_hook);
    host_set_hotkey_handler(hotkey);
    host_set_presenter(present);
    debris_install();
    gameplay_install(s);
    controller_install();
    if (s.effects == Effects::Adlib) sfx_adlib_install();
}

} // namespace gb
