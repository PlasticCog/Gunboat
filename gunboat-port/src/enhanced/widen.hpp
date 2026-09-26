#pragma once
// The wide cockpit (src/enhanced/widen.cpp): the 320x200 frame made wider for a wide window by seam
// insertion, so that the cockpit art reaches the window's edges without being stretched. In each
// outer band of the frame (the centre stays as it is) the few paths from the top row to the bottom
// row that cross the least detail (flat colour, and the view's openings, which are free) are found,
// and beside each a block of the columns before it is repeated: black areas, plain metal and its
// dithering grow, rivets, handles, gauges and lettering keep their shape, and an opening grows,
// showing more of the world. The message line at the top is padded at its ends instead (its text
// changes).
#include <vector>

#include "types.hpp"

namespace gb {

struct Widening {
    int left = 0, right = 0;  // columns added on each side
    int width = 320;          // 320 + left + right
    std::vector<u16> source;  // 200 rows x width: the frame column each column shows
    std::vector<u8> repeated; // 320 x 200: 1 for the frame pixels that are shown more than once
    u16 at(int x, int y) const { return source[size_t(y) * width + x]; }
};

// Builds the widening of `frame` (320x200 XRGB) by `left` and `right` columns. `free_px`: pixels a
// path crosses at no cost (the view's openings); `avoid`: pixels a path should not touch (the ones
// seen changing: digits, needles, lamps); `text_rows`: the rows at the top padded at their ends.
void widen_build(Widening &w, const u32 *frame, const bool *free_px, const bool *avoid, int left, int right,
                 int text_rows);

} // namespace gb
