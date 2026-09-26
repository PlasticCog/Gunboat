// The speaker music driver (sound.md §4.4), segment 1b37: three voices run small byte-code scripts
// (the note scripts at speaker_note_scripts, patched by speaker_note_on); on every menu timer tick the
// active voice with the highest priority sounds on the PC speaker. PORT: the Tandy output (port C0h,
// speaker_music_tandy set) is parked; it writes no memory.
//
// Voice record (16h bytes at speaker_voices + 16h * v): +0 active (80h) / free (0), +1 priority,
// +2 script far pointer, +6 wait counter, +8 register 0 (the volume), +0Ah register 1 (the divisor),
// +0Eh the saved flags of op 6, +10h the call stack depth, +12h the call stack. The script registers
// are the words at +8 + 2n.
#include "sound/sound.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 REC_SIZE = 0x16;
constexpr u16 V_ACTIVE = DS_speaker_voices;               // E684
constexpr u16 V_PRIORITY = u16(DS_speaker_voices + 1);    // E685
constexpr u16 V_SCRIPT = u16(DS_speaker_voices + 2);      // E686 (far pointer)
constexpr u16 V_WAIT = u16(DS_speaker_voices + 6);        // E68A
constexpr u16 V_REGS = u16(DS_speaker_voices + 8);        // E68C: register n at +2n (0 volume, 1 divisor)
constexpr u16 V_DIVISOR = u16(DS_speaker_voices + 0x0A);  // E68E
constexpr u16 V_FLAGS = u16(DS_speaker_voices + 0x0E);    // E692
constexpr u16 V_DEPTH = u16(DS_speaker_voices + 0x10);    // E694
constexpr u16 V_STACK = u16(DS_speaker_voices + 0x12);    // E696

u16 lodsw(FarPtr &si)
{
    const u16 v = mem_u16(si.seg, si.off);
    si.off = u16(si.off + 2);
    return v;
}

u8 lodsb(FarPtr &si)
{
    const u8 v = mem_u8(si.seg, si.off);
    si.off = u16(si.off + 1);
    return v;
}

// FLAGS after CMP r/m16, imm as PUSHF stores them in the timer interrupt: CF PF AF ZF SF OF, bit 1
// set; IF, DF, TF, IOPL, NT and bit 15 clear (PORT: a 386 in real mode with interrupts disabled).
u16 cmp_flags(u16 a, u16 b)
{
    const u16 r = u16(a - b);
    u16 f = 0x0002;
    if (a < b) f |= 0x0001;
    u8 p = u8(r);
    p ^= u8(p >> 4);
    p ^= u8(p >> 2);
    p ^= u8(p >> 1);
    if (!(p & 1)) f |= 0x0004;
    if ((a ^ b ^ r) & 0x10) f |= 0x0010;
    if (r == 0) f |= 0x0040;
    if (r & 0x8000) f |= 0x0080;
    if ((a ^ b) & (a ^ r) & 0x8000) f |= 0x0800;
    return f;
}

// CALL [speaker_script_ops + BX]: the handler the table designates.
bool run_op(u16 bx, u8 al, u16 di, FarPtr &si)
{
    const u16 target = ds_u16(u16(DS_speaker_script_ops + bx));
    switch (target) {
    case 0x01E3: return spk_op_end();
    case 0x01E5: return spk_op_set(al, di, si);
    case 0x01F3: return spk_op_wait(di, si);
    case 0x01FB: return spk_op_return(di, si);
    case 0x0218: return spk_op_add(al, di, si);
    case 0x0226: return spk_op_branch(al, di, si);
    case 0x0266: return spk_op_compare(al, di, si);
    case 0x027A: return spk_op_poke(si);
    default: break;
    }
    // PORT: the table always holds the eight handlers; another entry would run arbitrary code.
    host_fatal("speaker_music_tick: script op table entry %04X at %04X is not a handler", target,
               u16(DS_speaker_script_ops + bx));
}

} // namespace

// 1b37:0002 speaker_music_reset (sound.md §4.4, was tandy_detect): clears voice 0's record (only the
// first; the other two keep their state), turns the speaker off and sets speaker_music_tandy from the
// Tandy ROM byte FC00:0000 = 21h; on a Tandy the chip's four channels are silenced.
void speaker_music_reset()
{
    for (u16 i = 0; i < 0x0B; i++) ds_u16(u16(DS_speaker_voices + 2 * i)) = 0;  // cli; rep stosw
    spk_gate(false);
    const u8 al = mem_u8(0xFC00, 0) == 0x21 ? 1 : 0;
    ds_u8(DS_speaker_music_tandy) = al;
    if (al != 0)
        for (u8 v = 0x9F, n = 4; n; n--, v = u8(v + 0x20)) tandy_out(0xC0, v);
}

// 1b37:003c speaker_voice_start (sound.md §4.4): voice (0..2), or for another value the first free
// voice, else the one with the lowest priority (the last of equals) if it is below priority; the
// record gets priority, the script DS:script and cleared counters, registers and call stack depth.
void speaker_voice_start(u16 script_ds, u16 priority, u16 voice)
{
    // pushf; cli
    u16 di = 0;
    const u8 al = u8(voice);
    if (al < 3) {
        for (u8 n = al; n; n--) di = u16(di + REC_SIZE);
    } else {
        u8 lowest = 0xFF;
        u16 dx = 0;
        bool found_free = false;
        for (u16 cx = 3; cx; cx--) {
            if (ds_u8(u16(V_ACTIVE + di)) == 0) {
                found_free = true;
                break;
            }
            if (ds_u8(u16(V_PRIORITY + di)) <= lowest) {
                lowest = ds_u8(u16(V_PRIORITY + di));
                dx = di;
            }
            di = u16(di + REC_SIZE);
        }
        if (!found_free) {
            if (u8(priority) <= lowest) return;
            di = dx;
        }
    }
    ds_u8(u16(V_ACTIVE + di)) = 0x80;
    ds_u8(u16(V_PRIORITY + di)) = u8(priority);
    ds_u16(u16(V_SCRIPT + di)) = script_ds;
    ds_u16(u16(V_SCRIPT + 2 + di)) = DGROUP;
    for (u16 i = 0; i < 6; i++) ds_u16(u16(V_WAIT + di + 2 * i)) = 0;  // rep stosw
    // popf
}

// 1b37:00a6 speaker_voice_play (unreached): script on any voice with priority 64h.
void speaker_voice_play(u16 script_ds) { speaker_voice_start(script_ds, 0x64, 0xFFFF); }

// 1b37:00c0 speaker_music_tick (sound.md §4.4): each active voice whose wait counter runs out (goes
// below 0) runs script commands until one returns carry. Then, if some voice ran (SI not 0), the
// speaker plays the divisor of the active voice with the highest priority (the last of equals) when
// its priority, volume and divisor are not 0, else it is turned off; PIT channel 2 is only
// reprogrammed when the divisor changes (speaker_divisor).
void speaker_music_tick()
{
    FarPtr si = {0, 0};
    u16 di = 0;
    for (u16 cx = 3; cx; cx--, di = u16(di + REC_SIZE)) {
        if (ds_u8(u16(V_ACTIVE + di)) == 0) continue;
        if (s16(--ds_u16(u16(V_WAIT + di))) >= 0) continue;
        si = ds_far(u16(V_SCRIPT + di));
        bool carry;
        do {
            const u8 cmd = lodsb(si);
            const u16 bx = u16((cmd & 7) << 1);
            carry = run_op(bx, u8(cmd >> 4), di, si);
        } while (!carry);
        ds_far_set(u16(V_SCRIPT + di), si);
    }
    if (si.off == 0) return;
    if (ds_u8(DS_speaker_music_tandy) != 0) return;  // PORT: the Tandy output writes ports only
    if (ds_u8(DS_speaker_music_enabled) == 0) {
        spk_gate(false);
        return;
    }
    u8 dl = 0, dh = 0;
    u16 best = 0;
    di = 0;
    for (u16 cx = 3; cx; cx--, di = u16(di + REC_SIZE)) {
        if (ds_u8(u16(V_ACTIVE + di)) == 0) continue;
        if (dl > ds_u8(u16(V_PRIORITY + di))) continue;
        dl = ds_u8(u16(V_PRIORITY + di));
        dh = ds_u8(u16(V_REGS + di));
        best = di;
    }
    if (dl == 0 || dh == 0 || ds_u16(u16(V_DIVISOR + best)) == 0) {
        spk_gate(false);
        return;
    }
    spk_gate(true);
    const u16 bx = ds_u16(u16(V_DIVISOR + best));
    if (ds_u16(DS_speaker_divisor) == bx) return;
    ds_u16(DS_speaker_divisor) = bx;
    // pushf; cli
    spk_pit2_mode();
    spk_pit2_divisor(bx);
}

// 1b37:01e3 op 0: the voice is done for this tick.
bool spk_op_end() { return true; }

// 1b37:01e5 op 1: register AL = the next word.
bool spk_op_set(u8 al, u16 di, FarPtr &si)
{
    const u16 bx = u16(u16(al << 1) + di + V_REGS);
    ds_u16(bx) = lodsw(si);
    return false;
}

// 1b37:01f3 op 2: the wait counter = the next word.
bool spk_op_wait(u16 di, FarPtr &si)
{
    ds_u16(u16(V_WAIT + di)) = lodsw(si);
    return false;
}

// 1b37:01fb op 3: return from a call; with an empty call stack the voice ends (carry).
bool spk_op_return(u16 di, FarPtr &si)
{
    if (ds_u16(u16(V_DEPTH + di)) == 0) {
        ds_u8(u16(V_ACTIVE + di)) = 0;
        return true;
    }
    ds_u16(u16(V_DEPTH + di)) = u16(ds_u16(u16(V_DEPTH + di)) - 2);
    const u16 bx = ds_u16(u16(V_DEPTH + di));
    si.off = ds_u16(u16(V_STACK + bx + di));
    return false;
}

// 1b37:0218 op 4: register AL += the next word.
bool spk_op_add(u8 al, u16 di, FarPtr &si)
{
    const u16 bx = u16(u16(al << 1) + di + V_REGS);
    ds_u16(bx) = u16(ds_u16(bx) + lodsw(si));
    return false;
}

// 1b37:0226 op 5: AL 7 call, 6 jump, 0..5 jump on the flags of op 6 (0 equal, 1 below, 2 not equal,
// 3 above, 4 below or equal, 5 above or equal) to the next word; otherwise skip it.
bool spk_op_branch(u8 al, u16 di, FarPtr &si)
{
    if (al == 7) {  // call
        const u16 bx = ds_u16(u16(V_DEPTH + di));
        ds_u16(u16(V_STACK + bx + di)) = u16(si.off + 2);
        ds_u16(u16(V_DEPTH + di)) = u16(ds_u16(u16(V_DEPTH + di)) + 2);
    } else if (al != 6) {
        const u16 bx = al;  // BH = 0
        const u8 t = u8((ds_u8(u16(V_FLAGS + di)) ^ ds_u8(u16(DS_speaker_branch_xor + bx))) &
                        ds_u8(u16(DS_speaker_branch_mask + bx)));
        if ((t != 0) == (bx == 3)) {
            si.off = u16(si.off + 2);
            return false;
        }
    }
    si.off = mem_u16(si.seg, si.off);
    return false;
}

// 1b37:0266 op 6: compare register AL with the next word; the FLAGS word is saved at +0Eh.
bool spk_op_compare(u8 al, u16 di, FarPtr &si)
{
    const u16 bx = u16(u16(al << 1) + di + V_REGS);
    const u16 ax = lodsw(si);
    ds_u16(u16(V_FLAGS + di)) = cmp_flags(ds_u16(bx), ax);
    return false;
}

// 1b37:027a op 7: the byte b to seg:off (the next byte, word, word).
bool spk_op_poke(FarPtr &si)
{
    const u8 dl = lodsb(si);
    const u16 bx = lodsw(si);
    const u16 es = lodsw(si);
    mem_u8(es, bx) = dl;
    return false;
}

} // namespace gb
