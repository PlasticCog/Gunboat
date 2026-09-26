// The renderer's Tandy (mode 9) routines (render/modes.hpp). Not ported yet: each stops the
// program with render_parked until it is.
#include "render/modes.hpp"

#include "render/render.hpp"

namespace gb {

// 0919:4f96 blit_rows_tandy: not ported yet.
void blit_rows_tandy(u8, u16)
{
    render_parked("blit_rows_tandy (0919:4f96)");
}

// 0919:5494 spotlight_beam_tandy: not ported yet.
void spotlight_beam_tandy(u16, u16, u16)
{
    render_parked("spotlight_beam_tandy (0919:5494)");
}

// 0919:5529 sky_water_tandy: not ported yet.
u16 sky_water_tandy(u16, u16, u8, u8)
{
    render_parked("sky_water_tandy (0919:5529)");
}

// 0919:55da water_marks_tandy: not ported yet.
void water_marks_tandy(u16, u16, u16, u16, u16, u16)
{
    render_parked("water_marks_tandy (0919:55da)");
}

// 0919:5693 span_tandy_a: not ported yet.
void span_tandy_a(u16)
{
    render_parked("span_tandy_a (0919:5693)");
}

// 0919:5756 span_tandy_b: not ported yet.
void span_tandy_b(u16)
{
    render_parked("span_tandy_b (0919:5756)");
}

} // namespace gb
