// LZW pictures (platform.md §6): the dictionary block and the decoder of segment 08e1, Test Drive
// III's decoder (the TD3 port's platform/pic.c is its model).
#include "platform/platform.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

// 08e1:018e lzw_alloc (platform.md §6): DOS 48h for 300h paragraphs. AX goes to lzw_dict_seg
// before the carry is tested, so a failure leaves the DOS error code there. Returns 1, or 0.
s16 lzw_alloc()
{
    u16 err;
    const u16 seg = dos_alloc(0x300, &err);
    ds_u16(DS_lzw_dict_seg) = seg ? seg : err;
    return seg ? 1 : 0;
}

// 08e1:01aa lzw_free (platform.md §6): DOS 49h on lzw_dict_seg.
void lzw_free() { dos_free(ds_u16(DS_lzw_dict_seg)); }

} // namespace gb
