// The title music (sound.md §3, §4): device detection (1ace:00e8), the .MUS sequencer music_tick
// (1af5:0006, from the menu timer) and its PC speaker back end (1af5:033f/0394/03b8, which start
// scripts of the speaker music driver, speaker_music.cpp).
//
// PORT: the MT-32 (MPU-401), AdLib (ADLIB.COM through INT 65h) and Game Blaster (CMS.DRV) back ends
// are parked: the modelled machine has none of these devices (hw.cpp), so sound_detect picks the
// speaker. Reaching one of their routines is fatal (sound_parked).
#include "sound/sound.hpp"

#include "mem.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 MUSIC_SEG = 0x1AF5;
constexpr u16 MPU_DATA = 0x330, MPU_STATUS = 0x331;

void set_back_end(u16 note_on, u16 note_off, u16 control)
{
    ds_far_set(DS_music_note_on, {note_on, seg_of(MUSIC_SEG)});
    ds_far_set(DS_music_note_off, {note_off, seg_of(MUSIC_SEG)});
    ds_far_set(DS_music_control, {control, seg_of(MUSIC_SEG)});
}

// CALL FAR [ptr] with up to three stacked words: the routine the far pointer designates.
void call_back_end(u16 ptr_ds, u16 a0, u16 a1, u16 a2)
{
    const FarPtr p = ds_far(ptr_ds);
    if (p.seg == seg_of(MUSIC_SEG)) {
        switch (p.off) {
        case 0x033F: speaker_note_on(a0, a1, a2); return;
        case 0x0394: speaker_note_off(a0); return;
        case 0x03B8: speaker_program(a0, a1); return;
        case 0x0125: sound_parked("mt32_program (1af5:0125)");
        case 0x013F: sound_parked("mt32_note_on (1af5:013f)");
        case 0x0163: sound_parked("mt32_note_off (1af5:0163)");
        case 0x01E5: sound_parked("adlib_note_on (1af5:01e5)");
        case 0x01FE: sound_parked("adlib_note_off (1af5:01fe)");
        case 0x0213: sound_parked("adlib_program (1af5:0213)");
        case 0x02CE: sound_parked("cms_note_on (1af5:02ce)");
        case 0x02F0: sound_parked("cms_note_off (1af5:02f0)");
        case 0x030C: sound_parked("cms_program (1af5:030c)");
        default: break;
        }
    }
    sound_parked("a music back end at an unknown address");
}

} // namespace

// 1ace:000e music_play (sound.md §4.2): starts the stream at seg:off + 1 (the byte at off is skipped)
// on the next tick. PORT: the C stack check (15ee:1c48) is not needed.
void music_play(FarPtr stream, u16 loop)
{
    music_silence();
    ds_u16(u16(DS_music_loop_point + 2)) = 0;
    ds_u16(DS_music_loop_point) = 0;
    if (ds_u16(DS_sound_device) == 8) mpu_command(0x3F);  // MPU-401 UART mode
    const u16 start = u16(stream.off + 1);
    ds_u16(DS_music_pos) = start;
    ds_u16(u16(DS_music_pos + 2)) = stream.seg;
    ds_u16(DS_music_start_pos) = start;
    ds_u16(DS_music_wait) = 1;
    ds_u16(DS_music_loop) = loop;
    ds_u8(DS_music_playing) = 1;
}

// 1ace:005c music_silence (sound.md §4.2): stops the sequencer and sends a note off to every voice
// (music_voices is read again on each iteration); then the CMS driver is re-initialised or the
// MPU-401 reset.
void music_silence()
{
    ds_u8(DS_music_playing) = 0;
    for (s16 v = 0; v < s16(ds_u16(DS_music_voices)); v++) call_back_end(DS_music_note_off, u16(v), 0, 0);
    const u16 device = ds_u16(DS_sound_device);
    if (device == 2)
        cms_driver_init();
    else if (device == 8)
        mpu_command(0xFF);
}

// 1ace:00b4 music_resume (sound.md §4.2, unreached): restarts a stopped stream where it stopped when
// music_loop is set.
void music_resume()
{
    if (ds_u16(DS_music_loop) == 0 || ds_u8(DS_music_playing) != 0) return;
    if (ds_u16(DS_sound_device) == 8) mpu_command(0x3F);
    ds_u16(DS_music_wait) = 1;
    ds_u8(DS_music_playing) = 1;
}

// 1ace:00e8 sound_detect (sound.md §3): the first device of mask (8 MT-32, 4 AdLib, 2 Game Blaster,
// 1 Tandy) that answers, else the PC speaker; sets sound_device, music_voices and the back end.
void sound_detect(u16 mask, FarPtr cms_buf, FarPtr adlib_buf)
{
    ds_u8(DS_music_playing) = 0;
    speaker_music_reset();
    if ((mask & 8) && mpu_reset() != 0) {
        ds_u16(DS_sound_device) = 8;
        ds_u16(DS_music_voices) = 0x10;
        set_back_end(0x013F, 0x0163, 0x0125);
        return;
    }
    if ((mask & 4) && adlib_driver_present() != 0) {
        adlib_driver_init();
        ds_u16(DS_sound_device) = 4;
        ds_u16(DS_music_voices) = 9;
        set_back_end(0x01E5, 0x01FE, 0x0213);
        adlib_load_bin(adlib_buf);
        return;
    }
    if ((mask & 2) && cms_detect() != 0) {
        const s16 fh = s16(mus_open(DS_cms_drv_name));
        if (fh > 0) {
            ds_far_set(DS_cms_driver_entry, cms_buf);
            mus_read(u16(fh), cms_buf, 0x12F7);
            mus_close(u16(fh));
            cms_driver_init();
            ds_u16(DS_sound_device) = 2;
            ds_u16(DS_music_voices) = 8;
            set_back_end(0x02CE, 0x02F0, 0x030C);
            return;
        }
    }
    if ((mask & 1) && mem_u8(0xFC00, 0) == 0x21) {  // Tandy ROM (F000:C000 = '!')
        ds_u16(DS_sound_device) = 1;
        ds_u16(DS_music_voices) = 3;
    } else {
        ds_u16(DS_sound_device) = 0;
        ds_u16(DS_music_voices) = 1;
    }
    set_back_end(0x033F, 0x0394, 0x03B8);
}

// 1af5:0006 music_tick (sound.md §4.3): when music_wait runs out, the events up to the next non-zero
// delta go to the back end. Status bytes (bit 7) are kept as running status in music_status.
void music_tick()
{
    bool mark = false;  // [bp-2]: a D0 loop mark was read
    if (ds_u8(DS_music_playing) == 0) return;
    if (ds_u8(DS_sound_device) == 2) sound_parked("the CMS driver tick (CALL FAR [cms_driver_tick])");
    if (--ds_u16(DS_music_wait) != 0) return;
    u16 di = ds_u16(DS_music_pos);
    u16 es = ds_u16(u16(DS_music_pos + 2));
    for (;;) {
        u8 al = mem_u8(es, di);
        if (al & 0x80) {
            ds_u8(DS_music_status) = al;
            di++;
            al = mem_u8(es, di);
        }
        u8 ah = ds_u8(DS_music_status);
        if (ah == 0xFC) {  // end of the stream: back to the loop point, or stop
            const FarPtr lp = ds_far(DS_music_loop_point);
            if (lp.off == 0 && lp.seg == 0) {
                music_silence();
                return;
            }
            ds_u16(DS_music_pos) = lp.off;
            ds_u16(u16(DS_music_pos + 2)) = lp.seg;
            di = lp.off;
            es = lp.seg;
            continue;
        }
        const u16 ch = u16(s16(s8(u8(ds_u8(DS_music_status) & 0x0F))));
        ah &= 0xF0;
        bool note_off = ah == 0x80;  // 8x: note off, no data bytes
        if (ah == 0x90) {            // 9x note velocity; velocity 0 = note off
            const u16 vel = mem_u8(es, u16(di + 1)) & 0x7F;
            di = u16(di + 2);
            if (vel != 0) {
                const u16 note = u16(s16(s8(al)));
                if (s16(ch) < s16(ds_u16(DS_music_voices))) call_back_end(DS_music_note_on, ch, note, vel);
            } else {
                note_off = true;
            }
        } else if (ah == 0xC0) {  // Cx program
            const u16 program = al & 0x7F;
            di++;
            if (s16(ch) < s16(ds_u16(DS_music_voices))) call_back_end(DS_music_control, ch, program, 0);
        } else if (ah == 0xD0) {  // Dx byte: the loop point is the position after this event's delta
            di++;
            mark = true;
        }
        if (note_off && s16(ch) < s16(ds_u16(DS_music_voices))) call_back_end(DS_music_note_off, ch, 0, 0);
        // the delta: one byte, or two when the first has bit 7. Quirk: SHL AL / SHR AH / ROR AL
        // (not RCR) drops bit 0 of the first byte: delta = ((b1 & 7Fh) >> 1) << 8 | (b2 & 7Fh).
        const u8 b1 = mem_u8(es, di);
        di++;
        u16 delta = u16(s16(s8(b1)));
        if (b1 & 0x80) {
            u8 hi = b1 & 0x7F;
            u8 lo = mem_u8(es, di);
            di++;
            lo = u8(lo << 1);
            hi = u8(hi >> 1);
            lo = u8(lo >> 1 | lo << 7);  // ROR AL,1
            delta = u16(hi << 8 | lo);
        }
        ds_u16(DS_music_wait) = delta;
        if (delta == 0) continue;
        ds_u16(DS_music_pos) = di;
        if (mark) {
            ds_u16(DS_music_loop_point) = di;
            ds_u16(u16(DS_music_loop_point + 2)) = es;
        }
        return;
    }
}

// 1af5:0192 mpu_command (sound.md §3.1): waits (20000 reads) until the MPU-401 accepts a command,
// writes it, waits for the answer and returns AX = xxFEh for the acknowledge FEh, else 0.
// PORT: no MPU-401: the status port reads FFh, so the first wait times out (no host pump in this
// bounded probe). The acknowledge's AH is the caller's AH in the original; only non-zero matters.
u16 mpu_command(u16 cmd)
{
    u16 cx = 20000;
    while (no_device_in(MPU_STATUS) & 0x40)  // bit 6: not ready for a command
        if (--cx == 0) return 0;
    no_device_out(MPU_STATUS, u8(cmd));
    cx = 20000;
    while (no_device_in(MPU_STATUS) & 0x80)  // bit 7: no data
        if (--cx == 0) return 0;
    return no_device_in(MPU_DATA) == 0xFE ? 0x00FE : 0;
}

// 1af5:01be mpu_reset (sound.md §3.1): the reset command FFh twice; the second answer.
u16 mpu_reset()
{
    mpu_command(0xFF);
    return mpu_command(0xFF);
}

// 1af5:0238 adlib_load_bin (sound.md §3.2): ADLIB.BIN: 80h bytes to music_program_map, 340h bytes
// of timbres to the far buffer. Quirks: an open error (FFFFh) is not caught (the reads fail), only
// a handle 0 returns FFFFh; the file is never closed. Returns the last read's AX.
u16 adlib_load_bin(FarPtr timbres)
{
    const u16 fh = mus_open(DS_adlib_bin_name);
    if (fh == 0) return 0xFFFF;
    ds_far_set(DS_adlib_timbres, timbres);
    mus_read(fh, {DS_music_program_map, DGROUP}, 0x80);
    return mus_read(fh, ds_far(DS_adlib_timbres), 0x340);
}

// 1af5:0290 adlib_driver_present (sound.md §3.2): the 19-byte signature "SOUND-DRIVER-AD-LIB" 16h
// bytes before the INT 65h handler (DOS 3565h: the vector in mem[]); returns the word before the
// signature (0 when absent, and when that word is 0).
u16 adlib_driver_present()
{
    const u16 off = mem_u16(0, 0x65 * 4), seg = mem_u16(0, 0x65 * 4 + 2);
    const u16 di = u16(off - 0x16);
    const u16 ax = mem_u16(seg, u16(di - 2));
    for (u16 i = 0; i < 0x13; i++)  // REPE CMPSB
        if (ds_u8(u16(DS_adlib_signature + i)) != mem_u8(seg, u16(di + i))) return 0;
    return ax;
}

// 1af5:02b0 adlib_driver_init: INT 65h with SI = 0. PORT: parked (no AdLib driver).
void adlib_driver_init() { sound_parked("adlib_driver_init (INT 65h)"); }

// 1af5:02b9 adlib_call: INT 65h with SI = 15h, ES:BX = the three stacked words. PORT: parked.
void adlib_call(u16, u16, u16) { sound_parked("adlib_call (INT 65h)"); }

// 1af5:0311 cms_driver_init: CMS.DRV functions 2, 5 (its tick routine to cms_driver_tick), 6, 9.
// PORT: parked (no Game Blaster).
void cms_driver_init() { sound_parked("cms_driver_init (CMS.DRV)"); }

// 1af5:033f speaker_note_on (sound.md §4.4): the voice's note script (speaker_voice_scripts[voice],
// voice = ch on the Tandy, else 0) gets the divisor of the note and the volume vel >> 3, then starts
// with priority C8h. The note: octave = the number of 12s in note - 18h (8-bit), divisor =
// speaker_note_divisors[semitone] >> octave.
void speaker_note_on(u16 ch, u16 note, u16 vel)
{
    u16 ax = note;
    u8 al = u8(u8(ax) - 0x18);
    u8 cl = 0;
    for (;;) {
        al = u8(al - 0x0C);
        if (al & 0x80) break;
        cl++;
    }
    do al = u8(al + 0x0C);
    while (al & 0x80);
    ax = u16((ax & 0xFF00) | al);
    u16 dx = ch;
    if (ds_u8(DS_sound_device) != 1) dx = 0;
    u16 divisor = ds_u16(u16(DS_speaker_note_divisors + u16(ax << 1)));
    cl &= 0x1F;  // SHR r16, CL (386: the count masked to 5 bits)
    divisor = cl >= 16 ? 0 : u16(divisor >> cl);
    const u16 bx = ds_u16(u16(DS_speaker_voice_scripts + u16(dx << 1)));
    ds_u16(u16(bx + 1)) = divisor;
    ds_u16(u16(bx + 4)) = u16(vel >> 3);
    speaker_voice_start(bx, 0xC8, dx);
}

// 1af5:0394 speaker_note_off (sound.md §4.4): the voice starts the release script (op 3: the voice
// ends) with priority C8h.
void speaker_note_off(u16 ch)
{
    u16 ax = ch;
    if (ds_u8(DS_sound_device) != 1) ax = 0;
    speaker_voice_start(DS_speaker_release_script, 0xC8, ax);
}

// 1af5:03b8 speaker_program: programs are ignored.
void speaker_program(u16, u16) {}

// 1af5:03bd mus_open: INT 21h 3D00h (read); FFFFh on an error.
u16 mus_open(u16 name_ds)
{
    const s16 fh = dos_open(name_ds, "rb", false);
    return fh < 0 ? 0xFFFF : u16(fh);
}

// 1af5:03d0 mus_close: INT 21h 3Eh. (AX is not a result.)
void mus_close(u16 fh) { dos_close_handle(s16(fh)); }

// 1af5:03dc mus_read: INT 21h 3Fh; the bytes read, or 0 on an error.
u16 mus_read(u16 fh, FarPtr buf, u16 n)
{
    bool ok = false;
    const u16 ax = dos_read_handle(s16(fh), buf, n, &ok);
    return ok ? ax : 0;
}

} // namespace gb
