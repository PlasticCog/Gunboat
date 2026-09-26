// The renderer's CGA (mode 4; Hercules draws through these too) routines (render/modes.hpp). Not ported yet: each stops the
// program with render_parked until it is.
#include "render/modes.hpp"

#include "render/render.hpp"

namespace gb {

// 0919:4056 blit_rows_cga: not ported yet.
void blit_rows_cga(u8, u16)
{
    render_parked("blit_rows_cga (0919:4056)");
}

// 0919:44f0 spotlight_beam_cga: not ported yet.
void spotlight_beam_cga(u16, u16, u16)
{
    render_parked("spotlight_beam_cga (0919:44f0)");
}

// 0919:4587 sky_water_cga: not ported yet.
u16 sky_water_cga(u16, u16, u8, u8)
{
    render_parked("sky_water_cga (0919:4587)");
}

// 0919:4639 water_marks_cga: not ported yet.
void water_marks_cga(u16, u16, u16, u16, u16, u16)
{
    render_parked("water_marks_cga (0919:4639)");
}

// 0919:46e8 span_cga_a: not ported yet.
void span_cga_a(u16)
{
    render_parked("span_cga_a (0919:46e8)");
}

// 0919:47cf span_cga_b: not ported yet.
void span_cga_b(u16)
{
    render_parked("span_cga_b (0919:47cf)");
}

} // namespace gb
