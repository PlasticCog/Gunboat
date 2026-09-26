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
#include "enhanced/view3d.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "platform/vga.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

Settings cfg;
bool enhanced_on = true;  // F11
std::unique_ptr<Scene> scene_store[2];
Scene *cur, *prev;

SDL_Texture *cockpit_tex;
u32 cockpit_px[320 * 200];
bool hole[320 * 200];
u32 pal[256];

struct ViewTex {
    SDL_Texture *tex = nullptr;
    int w = 0, h = 0;
    std::vector<u8> idx;
};
ViewTex win_tex, left_tex, right_tex;

bool view_wanted() { return enhanced_on && cfg.any_enhancement(); }

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
    scene_capture(*cur);
    capture_ns += SDL_GetTicksNS() - t0;
    if (check.on) {
        if (std::memcmp(before.data(), mem, MEM_SIZE) != 0) check.memory_changed++;
        check_frame(*cur);
    }
}

bool hotkey(int scancode)
{
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
    view3d_render(*cur, interpolate ? prev : nullptr, t, target);
    void *pixels;
    int pitch;
    if (!SDL_LockTexture(vt.tex, nullptr, &pixels, &pitch)) return;
    for (int y = 0; y < h; y++) {
        u32 *row = reinterpret_cast<u32 *>(static_cast<u8 *>(pixels) + size_t(y) * pitch);
        const u8 *src = vt.idx.data() + size_t(y) * w;
        for (int x = 0; x < w; x++) row[x] = pal[src[x]];
    }
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

Layout last_layout;
bool was_live;

bool present(const u32 *frame, bool changed)
{
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
    if (!cockpit_tex) {
        cockpit_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
        SDL_SetTextureBlendMode(cockpit_tex, SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureScaleMode(cockpit_tex, filter_mode());
    const bool redraw = live && (cfg.hires_view || cfg.smooth_motion);  // the view in the cockpit
    for (int i = 0; i < 64000; i++)
        cockpit_px[i] = redraw && hole[i] ? (frame[i] & 0x00FFFFFFu) : (frame[i] | 0xFF000000u);
    SDL_UpdateTexture(cockpit_tex, nullptr, cockpit_px, 320 * 4);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (live) {
        const double rsx = cfg.hires_view ? l.sx : 1.0, rsy = cfg.hires_view ? l.sy : 1.0;
        const SDL_ScaleMode mode = cfg.hires_view ? SDL_SCALEMODE_NEAREST : filter_mode();
        // The world beside the picture.
        int ltx, lty, rtx, rty;
        if (cfg.widescreen && l.pic.x >= 1 && cur->u8_at(DS_chase_view) == 0 &&
            side_translations(ltx, lty, rtx, rty)) {  // (the chase view is framed in black: no extension)
            const double side = l.pic.x / l.sx, top = -l.pic.y / l.sy, height = l.oh / l.sy;
            render_region(left_tex, -side + ltx, top + lty, side, height, rsx, rsy, t, mode);
            render_region(right_tex, 320 + rtx, top + rty, side, height, rsx, rsy, t, mode);
            const SDL_FRect ld = {0, 0, l.pic.x, float(l.oh)};
            const SDL_FRect rd = {l.pic.x + l.pic.w, 0, float(l.ow) - (l.pic.x + l.pic.w), float(l.oh)};
            const SDL_FRect src = {0, 0, float(side * rsx), float(height * rsy)};
            SDL_RenderTexture(r, left_tex.tex, &src, &ld);
            SDL_RenderTexture(r, right_tex.tex, &src, &rd);
        }
    }
    if (redraw) {
        // The view through the cockpit's openings: the holes in rectangles of one offset each (runs
        // of a row merged with the same run of the rows below), their edges on whole output pixels.
        const double rsx = cfg.hires_view ? l.sx : 1.0, rsy = cfg.hires_view ? l.sy : 1.0;
        render_region(win_tex, VIEW_X, VIEW_Y, VIEW_W, VIEW_H, rsx, rsy, t,
                      cfg.hires_view ? SDL_SCALEMODE_NEAREST : filter_mode());
        struct Rect {
            int x0, x1, y0, y1, tx, ty;
        };
        static std::vector<Rect> rects, reached;
        rects.clear();
        size_t open_from = 0;  // rects that reach the previous row start here
        for (int y = 0; y < 200; y++) {
            const size_t row_from = rects.size();
            for (int x = 0; x < 320;) {
                if (!hole[y * 320 + x]) {
                    x++;
                    continue;
                }
                int tx, ty;
                translation(x, y, tx, ty);
                int x1 = x + 1;
                for (int ax, ay; x1 < 320 && hole[y * 320 + x1]; x1++) {
                    translation(x1, y, ax, ay);
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
        for (const Rect &q : rects) {
            const float X0 = std::round(l.pic.x + float(q.x0 * l.sx)), X1 = std::round(l.pic.x + float(q.x1 * l.sx));
            const float Y0 = std::round(l.pic.y + float(q.y0 * l.sy)), Y1 = std::round(l.pic.y + float(q.y1 * l.sy));
            const SDL_FRect dst = {X0, Y0, X1 - X0, Y1 - Y0};
            const SDL_FRect src = {float(((X0 - l.pic.x) / l.sx + q.tx - VIEW_X) * rsx),
                                   float(((Y0 - l.pic.y) / l.sy + q.ty - VIEW_Y) * rsy), float((X1 - X0) / l.sx * rsx),
                                   float((Y1 - Y0) / l.sy * rsy)};
            SDL_RenderTexture(r, win_tex.tex, &src, &dst);
        }
    }
    SDL_RenderTexture(r, cockpit_tex, nullptr, &l.pic);
    if (cfg.filter == Filter::Crt && l.sy >= 2) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 96);
        for (int y = 0; y < 200; y++) {
            const SDL_FRect band = {l.pic.x, l.pic.y + float((y + 0.6) * l.sy), l.pic.w, float(0.4 * l.sy)};
            SDL_RenderFillRect(r, &band);
        }
    }
    snapshot(r);
    stats(now, live, animating);
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
        const int base_w = s.widescreen ? base_h * 16 / 9 : (s.aspect == Aspect::Square ? 320 : 320);
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
}

} // namespace gb
