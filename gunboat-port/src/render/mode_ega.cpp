// The renderer's EGA (mode 0Dh) routines (render/modes.hpp). Not ported yet: each stops the
// program with render_parked until it is.
#include "render/modes.hpp"

#include "render/render.hpp"

namespace gb {

// 0919:48da blit_rows_ega: not ported yet.
void blit_rows_ega(u8, u16)
{
    render_parked("blit_rows_ega (0919:48da)");
}

// 0919:4989 ega_gc_setup: not ported yet.
void ega_gc_setup()
{
    render_parked("ega_gc_setup (0919:4989)");
}

// 0919:4c18 spotlight_beam_ega: not ported yet.
void spotlight_beam_ega(u16, u16, u16)
{
    render_parked("spotlight_beam_ega (0919:4c18)");
}

// 0919:4ce2 sky_water_ega: not ported yet.
u16 sky_water_ega(u16, u16, u8, u8)
{
    render_parked("sky_water_ega (0919:4ce2)");
}

// 0919:4d64 water_marks_ega: not ported yet.
void water_marks_ega(u16, u16, u16, u16, u16, u16)
{
    render_parked("water_marks_ega (0919:4d64)");
}

// 0919:4dfa span_ega_a: not ported yet.
void span_ega_a(u16)
{
    render_parked("span_ega_a (0919:4dfa)");
}

// 0919:4eb6 span_ega_b: not ported yet.
void span_ega_b(u16)
{
    render_parked("span_ega_b (0919:4eb6)");
}

} // namespace gb
