// Files and memory of segment 0000 (game_flow.md §1, platform.md §4-§5, ORIGINAL_WORLD_FORMAT.md):
// the archive name hash, archive_open, the whole-file loaders and the far buffers.
#include "game/flow.hpp"

#include <cstring>

#include "mem.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

// 0000:0e62 name_hash_h1: from the last character to the first, h = h * mult + char (chars
// sign-extended, 16-bit wrap). The caller passes mult = 101h.
u16 name_hash_h1(u16 name_ds, u16 mult)
{
    const char *s = ds_str(name_ds);
    u16 h = 0;
    for (s16 i = s16(u16(std::strlen(s)) - 1); i > -1; i--) h = u16(u16(mult * h) + s16(s8(s[i])));
    return h;
}

// 0000:0eac name_hash_h2: the sum of char * index over all characters but the last (chars sign-
// extended, 16-bit wrap). The second argument (10Fh) is not used.
u16 name_hash_h2(u16 name_ds, u16 /*unused*/)
{
    const char *s = ds_str(name_ds);
    u16 h = 0;
    const s16 n = s16(u16(std::strlen(s)) - 1);
    for (s16 i = 0; i < n; i++) h = u16(h + s16(s8(s[i])) * i);
    return h;
}

// 0000:0ef2 name_hash: DX:AX = h1:h2. (The original does not pop the arguments of the second call;
// its frame is discarded on return.)
u32 name_hash(u16 name_ds)
{
    const u16 h1 = name_hash_h1(name_ds, 0x101);
    const u16 h2 = name_hash_h2(name_ds, 0x10F);
    return u32(h1) << 16 | h2;
}

// 0000:0d74 archive_open (game_flow.md §1): looks the name up in archive_directory (14-byte
// records: +0 hash, +4 bank letter, +6 offset, +10 size; a zero hash ends the table), remembers the
// size and the bank letter, opens "dataX.dat" and seeks to the entry. Returns the handle, or 0 if
// the name is not in the archive.
s16 archive_open(u16 name_ds)
{
    const u32 hash = name_hash(name_ds);
    u16 i = 0;
    for (;;) {
        const u16 rec = u16(DS_archive_directory + 14 * i);
        if (ds_u16(rec) == 0 && ds_u16(u16(rec + 2)) == 0) break;
        if (ds_u16(rec) == u16(hash) && ds_u16(u16(rec + 2)) == u16(hash >> 16)) {
            ds_u16(DS_archive_entry_size) = ds_u16(u16(rec + 10));
            ds_u16(u16(DS_archive_entry_size + 2)) = ds_u16(u16(rec + 12));
            ds_u8(DS_data_file_bank) = ds_u8(u16(rec + 4));
            break;
        }
        i++;
    }
    const u16 rec = u16(DS_archive_directory + 14 * i);
    if (ds_u16(rec) == 0 && ds_u16(u16(rec + 2)) == 0) return 0;
    const s16 fh = dos_open_read(DS_data_file_name);
    // PORT: the original prints "Insert Disk %c into Drive A", waits for a key and retries; the
    // port has all files in one folder, so a missing data file is the fatal "file open failed".
    if (fh == -1) fatal_exit(2);
    dos_seek(fh, ds_u16(u16(rec + 6)), ds_u16(u16(rec + 8)));
    return fh;
}

// 0000:0648 file_load_near (game_flow.md §1): a whole archive entry (or a plain file, if the name
// is not in the archive) into DGROUP. An archive entry is read one byte short of its size.
void file_load_near(u16 name_ds, u16 dst_ds)
{
    s16 fh = archive_open(name_ds);
    u16 size = 0;
    if (fh == 0) {
        fh = dos_open_read(name_ds);
        if (fh == -1) fatal_exit(2);
        size = dos_file_size(fh);
    } else {
        size = u16(ds_u16(DS_archive_entry_size) - 1);
    }
    dos_read({dst_ds, DGROUP}, size, fh);
    dos_close(fh);
}

// 0000:06b4 file_load_far (game_flow.md §1): as file_load_near into a far buffer; the number of
// bytes read is left in archive_entry_size.
void file_load_far(u16 name_ds, FarPtr dst)
{
    s16 fh = archive_open(name_ds);
    u16 size = 0;
    if (fh == 0) {
        fh = dos_open_read(name_ds);
        if (fh == -1) fatal_exit(2);
        size = dos_file_size(fh);
    } else {
        size = u16(ds_u16(DS_archive_entry_size) - 1);
    }
    ds_u16(DS_archive_entry_size) = size;
    ds_u16(u16(DS_archive_entry_size + 2)) = 0;
    dos_read(dst, size, fh);
    dos_close(fh);
}

// 0000:072c far_to_near_copy (world.md §3.3): count bytes from a far buffer into DGROUP. The loop
// counter is the global flow_scratch (DS:F13A), read for every byte and left equal to the count; a
// copy that writes over it changes the loop, as in the original.
void far_to_near_copy(FarPtr src, u16 dst_ds, u16 count)
{
    for (ds_u16(DS_flow_scratch) = 0; count > ds_u16(DS_flow_scratch); ds_u16(DS_flow_scratch)++) {
        const u16 si = ds_u16(DS_flow_scratch);
        const u8 al = mem_u8(src.seg, u16(src.off + si));
        ds_u8(u16(dst_ds + si)) = al;
    }
}

// 0000:0756 near_to_far_copy (world.md §4): the other way, with the same global counter.
void near_to_far_copy(u16 src_ds, FarPtr dst, u16 count)
{
    for (ds_u16(DS_flow_scratch) = 0; count > ds_u16(DS_flow_scratch); ds_u16(DS_flow_scratch)++) {
        const u16 si = ds_u16(DS_flow_scratch);
        const u8 al = ds_u8(u16(src_ds + si));
        mem_u8(dst.seg, u16(dst.off + si)) = al;
    }
}

// 0000:0a4e mem_alloc_all (platform.md §5): the far buffers, each stored as a far pointer; any
// failure is fatal error 1 ("Insufficient memory").
void mem_alloc_all()
{
    const auto alloc = [](u16 size, u16 ptr_ds) {
        const FarPtr p = crt_fmalloc(size);
        ds_far_set(ptr_ds, p);
        if (far_is_null(p)) fatal_exit(1);
    };
    alloc(0xF410, DS_sprite_cache_a_far);
    alloc(0xD010, DS_sprite_cache_b_far);
    alloc(0x3A34, DS_tile_bin_offset);
    alloc(0x244A, DS_pictures_far);
    alloc(0x21F2, DS_bow_art2_far);
    alloc(0x2382, DS_midship_art2_far);
    alloc(0x251C, DS_bow_art1_far);
    alloc(0x1AEA, DS_stern_art2_far);
    alloc(0x1A7C, DS_world_b_far);
    alloc(0x2AD0, DS_clip_far);
    ds_far_set(DS_clip_1838_far, far_add(ds_far(DS_clip_far), 0x1838));
    ds_far_set(DS_clip_0c1c_far, far_add(ds_far(DS_clip_far), 0x0C1C));
    alloc(0x1310, DS_map_sheet_b_far);
    alloc(0x0D0C, DS_bd4_far);
    alloc(0x1784, DS_bd1_far);
    ds_far_set(DS_bd5_far, far_add(ds_far(DS_bd1_far), 0x0AD2));
    alloc(0x0CC6, DS_bd2_far);
    if (!lzw_alloc()) fatal_exit(1);
}

// 0000:0c7a mem_free_all (platform.md §5). It also frees clip_1838_far, a pointer into the middle
// of the clip block (PORT: the runtime then sets a flag byte inside that block; the port frees
// the block's DOS memory a second time, which does nothing; this only runs on the way out).
void mem_free_all()
{
    for (const u16 p : {DS_sprite_cache_a_far, DS_sprite_cache_b_far, DS_tile_bin_offset, DS_pictures_far,
                        DS_bow_art2_far, DS_midship_art2_far, DS_bow_art1_far, DS_stern_art2_far, DS_world_b_far,
                        DS_clip_far, DS_clip_1838_far, DS_map_sheet_b_far, DS_bd4_far, DS_bd1_far, DS_bd2_far})
        crt_ffree(ds_far(p));
    lzw_free();
}

} // namespace gb
