// The wide cockpit (widen.hpp): seam insertion in the outer bands of the frame.
#include "enhanced/widen.hpp"

#include <algorithm>
#include <cstdlib>

namespace gb {

namespace {

constexpr int W = 320, H = 200;
constexpr int BAND = 120;          // the outer columns on each side where the paths may run
constexpr float OUTWARD = 40.0f;   // extra cost of a pixel a whole band in from the edge
constexpr int PATHS = 16;          // paths per side at most: few, through the clearest places
constexpr float AVOID = 3000.0f;   // the cost of a pixel seen changing
constexpr float STEP = 100.0f;     // the cost of a sideways step (it shears what the path crosses)

int diff(u32 a, u32 b)
{
    return std::abs(int(a >> 16 & 0xFF) - int(b >> 16 & 0xFF)) + std::abs(int(a >> 8 & 0xFF) - int(b >> 8 & 0xFF)) +
           std::abs(int(a & 0xFF) - int(b & 0xFF));
}

struct Path {
    std::vector<u16> column;  // the frame column of each row from the first path row
    float cost = 0;
};

// The n cheapest paths through columns [x0, x1) of rows y0..199, one pixel per row, each row's pixel
// next to or below the one above; every path found is taken out before the next is searched, so they
// are all different.
std::vector<Path> find_paths(const std::vector<float> &cost, int x0, int x1, int y0, int n)
{
    const int rows = H - y0;
    std::vector<std::vector<u16>> left(static_cast<size_t>(rows));
    for (auto &r : left)
        for (int x = x0; x < x1; x++) r.push_back(u16(x));
    std::vector<Path> paths;
    std::vector<float> sum;
    std::vector<signed char> step;
    for (int k = 0; k < n; k++) {
        const int w = int(left[0].size());
        if (w == 0) break;
        sum.assign(size_t(rows) * w, 0);
        step.assign(size_t(rows) * w, 0);
        for (int i = 0; i < w; i++) sum[size_t(i)] = cost[size_t(y0) * W + left[0][size_t(i)]];
        for (int r = 1; r < rows; r++) {
            const float *above = &sum[size_t(r - 1) * w];
            for (int i = 0; i < w; i++) {
                float best = above[i];
                signed char d = 0;
                if (i > 0 && above[i - 1] + STEP < best) {
                    best = above[i - 1] + STEP;
                    d = -1;
                }
                if (i + 1 < w && above[i + 1] + STEP < best) {
                    best = above[i + 1] + STEP;
                    d = 1;
                }
                sum[size_t(r) * w + i] = best + cost[size_t(y0 + r) * W + left[size_t(r)][size_t(i)]];
                step[size_t(r) * w + i] = d;
            }
        }
        int i = 0;
        const float *last = &sum[size_t(rows - 1) * w];
        for (int j = 1; j < w; j++)
            if (last[j] < last[i]) i = j;
        Path p;
        p.cost = last[i];
        p.column.resize(size_t(rows));
        for (int r = rows - 1; r >= 0; r--) {
            p.column[size_t(r)] = left[size_t(r)][size_t(i)];
            left[size_t(r)].erase(left[size_t(r)].begin() + i);
            if (r > 0) i += step[size_t(r) * w + i];
        }
        paths.push_back(std::move(p));
    }
    return paths;
}

// One insertion: at row y after frame column x, the `count` columns ending at x again (or starting
// at x at the frame's left edge), so that a texture continues instead of one column smeared.
struct Insert {
    u16 x, count;
};

// Spreads `extra` columns over the paths of one band, the cheapest paths first, into each row's
// insertions.
void spread(std::vector<std::vector<Insert>> &rows, std::vector<Path> paths, int extra, int y0)
{
    if (paths.empty() || extra <= 0) return;
    std::sort(paths.begin(), paths.end(), [](const Path &a, const Path &b) { return a.cost < b.cost; });
    const int n = int(paths.size());
    for (int k = 0; k < n; k++) {
        const int d = extra / n + (k < extra % n ? 1 : 0);
        if (d == 0) continue;
        const std::vector<u16> &column = paths[size_t(k)].column;
        for (int r = 0; r < int(column.size()); r++) rows[size_t(y0 + r)].push_back({column[size_t(r)], u16(d)});
    }
}

} // namespace

void widen_build(Widening &w, const u32 *frame, const bool *free_px, const bool *avoid, int left, int right,
                 int text_rows)
{
    left = std::clamp(left, 0, 4 * BAND);
    right = std::clamp(right, 0, 4 * BAND);
    text_rows = std::clamp(text_rows, 0, H - 1);
    w.left = left;
    w.right = right;
    w.width = W + left + right;
    w.source.assign(size_t(H) * w.width, 0);
    w.repeated.assign(size_t(W) * H, 0);

    // The cost of repeating a pixel: how much its neighbours differ (the ones two apart, so that a
    // one-pixel dither costs little), strong edges (lettering, digits, lamps, outlines) much more, and
    // near them too (three columns and a row around: a path does not slip between letters, which the
    // game may redraw); nothing in the view's openings; a little more the further in from the edge.
    std::vector<float> edge(size_t(W) * H, 0.0f);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const size_t p = size_t(y) * W + x;
            if (avoid[p]) {
                edge[p] = AVOID;
                continue;
            }
            if (free_px[p]) continue;
            const u32 l = frame[p - (x > 0)], r = frame[p + (x < W - 1)];
            const u32 u = frame[p - (y > 0 ? W : 0)], d = frame[p + (y < H - 1 ? W : 0)];
            const float c = float(diff(l, r) + diff(u, d));
            edge[p] = c + 4.0f * std::max(0.0f, c - 120.0f);
        }
    std::vector<float> cost(size_t(W) * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float c = 0;
            for (int dy = -1; dy <= 1; dy++) {
                const int yy = std::clamp(y + dy, 0, H - 1);
                for (int dx = -3; dx <= 3; dx++) c = std::max(c, edge[size_t(yy) * W + std::clamp(x + dx, 0, W - 1)]);
            }
            const int in = std::min(x, W - 1 - x);
            cost[size_t(y) * W + x] = c + OUTWARD * float(in) / BAND;
        }

    std::vector<std::vector<Insert>> inserts(H);
    spread(inserts, find_paths(cost, 0, BAND, text_rows, std::min(left, PATHS)), left, text_rows);
    spread(inserts, find_paths(cost, W - BAND, W, text_rows, std::min(right, PATHS)), right, text_rows);

    for (int y = 0; y < H; y++) {
        u16 *row = &w.source[size_t(y) * w.width];
        int o = 0;
        if (y < text_rows) {  // the message line: padded at its ends
            for (int k = 0; k < left; k++) row[o++] = 0;
            for (int x = 0; x < W; x++) row[o++] = u16(x);
            for (int k = 0; k < right; k++) row[o++] = W - 1;
            continue;
        }
        std::vector<Insert> &at = inserts[size_t(y)];
        std::sort(at.begin(), at.end(), [](const Insert &a, const Insert &b) { return a.x < b.x; });
        size_t k = 0;
        for (int x = 0; x < W; x++) {
            if (o < w.width) row[o++] = u16(x);
            for (; k < at.size() && at[k].x == x; k++) {
                const int first = std::max(0, x - at[k].count + 1);
                for (int c = 0; c < at[k].count && o < w.width; c++) {
                    const int col = std::min(first + c, W - 1);
                    row[o++] = u16(col);
                    w.repeated[size_t(y) * W + col] = 1;
                }
            }
        }
        while (o < w.width) row[o++] = W - 1;  // (never: the paths add exactly left + right)
    }
}

} // namespace gb
