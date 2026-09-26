// LZW pictures (platform.md §6): the dictionary block and the decoder of segment 08e1, Test Drive
// III's decoder (the TD3 port's platform/pic.c is its model). All decoder state is at its DGROUP
// address; the input window is lzw_window (DS:0C74, 400h bytes); the dictionary is the DOS block
// lzw_dict_seg, 3 bytes per code (u16 prefix, u8 character).
#include "platform/platform.hpp"

#include <vector>

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// 08e1:02b1 lzw_fill_input: 400h source bytes into the window; the source offset advances.
void lzw_fill_input()
{
    const FarPtr dst = ds_far(DS_lzw_window_far);
    const FarPtr src = ds_far(DS_lzw_src);
    for (u16 i = 0; i < 0x400; i++) mem_u8(dst.seg, u16(dst.off + i)) = mem_u8(src.seg, u16(src.off + i));
    ds_u16(DS_lzw_src) = u16(src.off + 0x400);
}

// 08e1:02cd lzw_get_code: the next nbits-bit code, LSB first. Near the end of the window (byte
// >= 3FDh) the tail moves to the start and new source bytes follow it. The tail is copied to ES:
// es is the decoder's ES register, which is DGROUP at every refill of a real picture (the first
// ordinary code sets it), but the destination segment before that.
u16 lzw_get_code(u16 es)
{
    const u16 pos = ds_u16(DS_lzw_bitpos);
    ds_u16(DS_lzw_bitpos) = u16(pos + ds_u16(DS_lzw_nbits));
    u16 byte = pos >> 3;
    const u16 bit = pos & 7;
    if (s16(byte) >= 0x3FD) {
        ds_u16(DS_lzw_bitpos) = u16(bit + ds_u16(DS_lzw_nbits));
        u16 di = DS_lzw_window, si = u16(DS_lzw_window + byte);
        for (u16 n = u16(0x400 - byte); n; n--) mem_u8(es, di++) = ds_u8(si++);
        const u16 window_seg = ds_u16(u16(DS_lzw_window_far + 2));
        const FarPtr src = ds_far(DS_lzw_src);
        u16 s = src.off;
        for (u16 n = byte; n; n--) mem_u8(window_seg, di++) = mem_u8(src.seg, s++);
        ds_u16(DS_lzw_src) = s;
        byte = 0;
    }
    const u16 si = u16(DS_lzw_window + byte);
    u16 bx = ds_u16(si);
    u8 al = ds_u8(u16(si + 2));
    for (u16 n = bit; n; n--) {  // SHR AL,1 / RCR BX,1
        bx = u16(bx >> 1 | (al & 1) << 15);
        al >>= 1;
    }
    return bx & ds_u16(u16(DS_lzw_masks + 2 * (ds_u16(DS_lzw_nbits) - 9)));
}

// 08e1:033b lzw_reset_width
void lzw_reset_width()
{
    ds_u16(DS_lzw_nbits) = 9;
    ds_u16(DS_lzw_maxcode) = 0x200;
    ds_u16(DS_lzw_next) = 0x102;
}

// 08e1:034e lzw_out: one byte to the output far pointer; only its offset advances.
void lzw_out(u8 al)
{
    const FarPtr d = ds_far(DS_lzw_dst);
    mem_u8(d.seg, d.off) = al;
    ds_u16(DS_lzw_dst) = u16(d.off + 1);
}

// 08e1:035a lzw_idx3
u16 lzw_idx3(u16 bx) { return u16(bx * 3); }

// 08e1:0361 lzw_add_entry: dict[next] = {old, firstchar}; next++. At 12 bits `next` goes on
// growing and the entries land behind the 3000h-byte block, in whatever memory follows it, as in
// the original; a valid stream sends a clear code first.
void lzw_add_entry()
{
    const u16 bx = lzw_idx3(ds_u16(DS_lzw_next));
    const u16 seg = ds_u16(DS_lzw_dict_seg);
    mem_u8(seg, u16(bx + 2)) = ds_u8(DS_lzw_firstchar);
    mem_u16(seg, bx) = ds_u16(DS_lzw_old);
    ds_u16(DS_lzw_next)++;
}

} // namespace

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

// 08e1:01db lzw_decode_body (platform.md §6): decodes src into dst until the end code 101h.
// The expanded characters go on the CPU stack (count lzw_stack_n) and come out reversed.
void lzw_decode_body(FarPtr src, FarPtr dst)
{
    ds_far_set(DS_lzw_src, src);
    ds_far_set(DS_lzw_dst, dst);
    u16 es = dst.seg;
    std::vector<u8> stack;
    lzw_fill_input();
    for (;;) {
        u16 code = lzw_get_code(es);
        if (code == 0x101) return;
        if (code == 0x100) {
            lzw_reset_width();
            code = lzw_get_code(es);
            ds_u16(DS_lzw_cur) = code;
            ds_u16(DS_lzw_old) = code;
            ds_u8(DS_lzw_firstchar) = u8(code);
            ds_u8(DS_lzw_finchar) = u8(code);
            lzw_out(ds_u8(DS_lzw_firstchar));
            continue;
        }
        ds_u16(DS_lzw_cur) = code;
        ds_u16(DS_lzw_incode) = code;
        es = ds_u16(DS_lzw_dict_seg);
        if (s16(code) >= s16(ds_u16(DS_lzw_next))) {  // KwKwK
            ds_u16(DS_lzw_cur) = ds_u16(DS_lzw_old);
            stack.push_back(ds_u8(DS_lzw_finchar));
            ds_u16(DS_lzw_stack_n)++;
        }
        while (s16(ds_u16(DS_lzw_cur)) > 0xFF) {
            // PORT: a corrupt stream with a prefix loop would run the original's stack into
            // DGROUP; the port stops.
            if (stack.size() > 0xFFFF) return;
            const u16 bx = lzw_idx3(ds_u16(DS_lzw_cur));
            stack.push_back(mem_u8(es, u16(bx + 2)));
            ds_u16(DS_lzw_stack_n)++;
            ds_u16(DS_lzw_cur) = mem_u16(es, bx);
        }
        es = DGROUP;
        const u8 c = u8(ds_u16(DS_lzw_cur));
        ds_u8(DS_lzw_finchar) = c;
        ds_u8(DS_lzw_firstchar) = c;
        stack.push_back(c);
        ds_u16(DS_lzw_stack_n)++;
        for (u16 n = ds_u16(DS_lzw_stack_n); n && !stack.empty(); n--) {
            lzw_out(stack.back());
            stack.pop_back();
        }
        ds_u16(DS_lzw_stack_n) = 0;
        lzw_add_entry();
        ds_u16(DS_lzw_old) = ds_u16(DS_lzw_incode);
        if (s16(ds_u16(DS_lzw_next)) >= s16(ds_u16(DS_lzw_maxcode)) && ds_u16(DS_lzw_nbits) != 12) {
            ds_u16(DS_lzw_nbits)++;
            ds_u16(DS_lzw_maxcode) = u16(ds_u16(DS_lzw_maxcode) << 1);
        }
    }
}

// 08e1:01bd lzw_decode_picture (platform.md §6): resets the decoder and falls into the body.
void lzw_decode_picture(FarPtr src, FarPtr dst)
{
    ds_u16(DS_lzw_next) = 0x102;
    ds_u16(DS_lzw_stack_n) = 0;
    ds_u16(DS_lzw_nbits) = 9;
    ds_u16(DS_lzw_maxcode) = 0x200;
    ds_u16(DS_lzw_bitpos) = 0;
    lzw_decode_body(src, dst);
}

} // namespace gb
