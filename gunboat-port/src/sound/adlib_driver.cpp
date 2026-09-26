// The Ad Lib sound driver ADLIB.COM V1.51 (Ad Lib Inc., 1989), the TSR through which GB.EXE plays
// its music on an AdLib (spec reverse_engineering/spec/adlib_driver.md). It is not part of GB.EXE:
// the player ran it before the game. The port loads the user's ADLIB.COM into mem[] where DOS would
// have put it (PSP = CS = ADLIB_PSP, DS = SS = ES = ADLIB_DS) and runs this translation of the
// routines GB.EXE reaches: the installation, the INT 65h dispatch with functions 0 (init), 13h
// (note on), 14h (note off) and 15h (timbre) and everything they call, and the INT 8 handler the
// driver hooks. The driver is a Lattice-style C program: near C functions with stack arguments,
// longs in AX:BX (AX the high word). One C++ function per routine, named in the spec and in each
// comment as "ADLIB.COM oooo name" (a CS offset; the file offset + 100h).
//
// PORT (hardware): the OPL2 at 388h/389h becomes host_opl_write(reg, value), one call per register
// write (index then data); the driver's delay reads of the status port (6 after the index, 35 after
// the data) are only a delay for the chip and do nothing here. The PIT becomes host_set_timer().
// PORT (stack): the driver runs on its own stacks (DS:0C20-0F1F for INT 65h, DS:011C-031B for its
// timer); their contents and the caller's SS:SP it saves (CS:020B, CS:0572) are not modelled.
#include "sound/sound.hpp"

#include <cstdio>
#include <vector>

#include "host.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"

namespace gb {

namespace {

// ---- the resident image: code-segment variables (CS offsets)
constexpr u16 C_CALLER_SS = 0x020B;   // INT 65h: the caller's SS and SP (not modelled)
constexpr u16 C_DRV_SP = 0x020F;      // INT 65h: the driver's stack pointer and segment
constexpr u16 C_DRV_SS = 0x0211;
constexpr u16 C_FN_TABLE = 0x0213;    // 24 near handlers, SI = 0..17h
constexpr u16 C_FN_ARGC = 0x0243;     // their argument word counts
constexpr u16 C_VERSION = 0x02D7;     // 0151h, the word before the signature
constexpr u16 C_SIGNATURE = 0x02D9;   // "SOUND-DRIVER-AD-LIB"
constexpr u16 C_CLK_CHAIN = 0x0568;   // 1 when the PIT runs at 18.2 Hz (every interrupt is the BIOS's)
constexpr u16 C_CLK_DIVISOR = 0x056A; // the PIT divisor the driver programmed
constexpr u16 C_CLK_ACCUM = 0x056C;   // divisor accumulator: a carry = one BIOS tick
constexpr u16 C_CLK_OLD = 0x056E;     // the INT 8 vector before the driver (far)
constexpr u16 C_CLK_COUNT = 0x0578;   // interrupts counted (dword, kept below 80000000h)
constexpr u16 C_CLK_COUNTDOWN = 0x0576;  // interrupts until the next sequencer tick
constexpr u16 C_CLK_BUSY = 0x057C;    // the sequencer tick runs (byte)

// ---- the data segment (DS offsets)
constexpr u16 D_DOS_VERSION = 0x0002, D_PSP = 0x0006, D_ARGS = 0x0008, D_STACK_TOP = 0x000A,
              D_DATA_END = 0x000C, D_FREE = 0x000E, D_HEAP = 0x0010, D_RESIDENT_END = 0x0012;
constexpr u16 D_QUEUE = 0x031E, D_QUEUE_COUNT = 0x053A, D_QUEUE_HEAD = 0x0544, D_QUEUE_FREE = 0x0546;
constexpr u16 D_VOICE_STATE = 0x0548;  // 11 x 5 bytes
constexpr u16 D_EVENT_POOL = 0x063B, D_VOICE_FLAGS = 0x063D;
constexpr u16 D_CLOCK_TICKS = 0x06B6, D_TEMPO = 0x06B8, D_FN0A_VALUE = 0x06BA, D_SEQ_ACTIVE = 0x06BC,
              D_SEQ_PLAYING = 0x06BE, D_ACT_VOICE = 0x06C0, D_SEQ_START_LO = 0x06C2, D_SEQ_START_HI = 0x06C4,
              D_MODE = 0x06C6, D_EVENT_TYPES = 0x06C8, D_BUFFER = 0x06D4, D_BUFFER_NODES = 0x06D6,
              D_TICKS_PER_BEAT = 0x06D8;
constexpr u16 D_GEN_PORT = 0x06F6, D_PERC_BITS = 0x06F8, D_PERC_MASKS = 0x06F9, D_VOICE_NOTE = 0x06FE,
              D_SD_PITCH = 0x0705, D_TOM_PITCH = 0x0706, D_VOICE_KEY_ON = 0x0709, D_NOTE_DIV12 = 0x0714,
              D_NOTE_MOD12 = 0x0774, D_SLOT_REL_VOLUME = 0x07D4, D_AM_DEPTH = 0x07F8, D_VIB_DEPTH = 0x07F9,
              D_NOTE_SEL = 0x07FA, D_PERCUSSION = 0x07FB, D_PIANO_OP0 = 0x07FC, D_PIANO_OP1 = 0x0818,
              D_PARAM_SLOT = 0x0834, D_SLOT_VOICE = 0x0930, D_SLOT_PERC = 0x0942, D_OFFSET_SLOT = 0x094C,
              D_CARRIER_SLOT = 0x095E, D_VOICE_SLOT = 0x0970, D_FNUM_TBL = 0x0982, D_HALF_TONE = 0x0BDA,
              D_FNUM_PTR = 0x0BF0, D_PITCH_RANGE = 0x0C06, D_PITCH_RANGE_STEP = 0x0C08,
              D_MODE_WAVE_SEL = 0x0C0A;
constexpr u16 PARAM_SLOT_SIZE = 14;    // paramSlot[18][14] bytes

// The file this translation was made from (and the only one the port accepts).
constexpr u32 COM_SIZE = 13280;
constexpr u32 COM_FNV1A = 0x44561E68u;  // FNV-1a 32 of the whole file

inline u8 &dv_u8(u16 off) { return mem_u8(ADLIB_DS, off); }
inline u16_m &dv_u16(u16 off) { return mem_u16(ADLIB_DS, off); }
inline s16_m &dv_s16(u16 off) { return mem_s16(ADLIB_DS, off); }
inline u16_m &dc_u16(u16 off) { return mem_u16(ADLIB_PSP, off); }
inline u8 &dc_u8(u16 off) { return mem_u8(ADLIB_PSP, off); }

[[noreturn]] void adl_unported(const char *what)
{
    host_fatal("ADLIB.COM: %s is not ported (GB.EXE never reaches it)", what);
}

u16 lo16(u32 v) { return u16(v); }

} // namespace

// ================================================================ loading and installation

// PORT (DOS loads ADLIB.COM): the PSP that DOS builds (only what the driver reads and the usual
// entry bytes: INT 20h, the memory top, the CP/M call with PSP:0006 = FEF0h bytes in the segment,
// INT 21h/RETF at 50h, an empty command tail) and the file at PSP:0100. Memory after the file keeps
// what it held (zero in the port). No MCB or environment: the port's DOS arena starts above GB.EXE
// (dos.cpp). Checks that the file is the V1.51 this translation was made from.
bool adlib_load(const std::string &path, std::string &err)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::vector<u8> data(COM_SIZE + 1);
    const size_t n = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ data[i]) * 16777619u;
    if (n != COM_SIZE || h != COM_FNV1A) {
        err = path + " is not the Ad Lib sound driver V1.51 (13,280 bytes) that the port translates";
        return false;
    }
    u8 *psp = mp(ADLIB_PSP, 0);
    for (int i = 0; i < 0x100; i++) psp[i] = 0;
    psp[0x00] = 0xCD;  // INT 20h
    psp[0x01] = 0x20;
    mem_u16(ADLIB_PSP, 0x02) = VRAM_SEG;  // the first segment after the program's memory
    psp[0x05] = 0x9A;                     // CALL FAR F01D:FEF0 (CP/M entry; PSP:0006 = bytes available)
    mem_u16(ADLIB_PSP, 0x06) = 0xFEF0;
    mem_u16(ADLIB_PSP, 0x08) = 0xF01D;
    psp[0x50] = 0xCD;  // INT 21h, RETF
    psp[0x51] = 0x21;
    psp[0x52] = 0xCB;
    psp[0x80] = 0x00;  // the command tail: no options
    psp[0x81] = 0x0D;
    for (u32 i = 0; i < COM_SIZE; i++) mem_u8(ADLIB_PSP, u16(0x100 + i)) = data[i];
    return true;
}

// PORT: ADLIB.COM's run, from its start-up (0100) to INT 27h: the state it leaves resident. The
// start-up code (the Lattice-style C runtime) and main (0371) are replaced by their effects on the
// data segment, for a command tail without options (buffer FFAh bytes, port 388h, /W 0); the
// messages it prints are not shown. The stack contents (the argument string, main's frame) are not
// written. Then main's calls: install_int65 (0273), driver_setup (08aa), keep_resident (02c0,
// INT 27h: DOS keeps 44Fh paragraphs; no DOS arena is modelled there).
void adlib_install()
{
    // 0100 com_start: DS = SS = CS + 25Ch, SP = 0F20h
    dv_u16(D_DOS_VERSION) = 0x0005;  // INT 21h 30h: AL = 5, AH = 0 (DOS 5.00). PORT: the DOS the port models
    dv_u16(D_PSP) = ADLIB_PSP;
    const u16 len = mem_u8(ADLIB_PSP, 0x80);
    const u16 top = u16(u16(0x300 >> 1) + u16(0x300 >> 1) + dv_u16(D_DATA_END));  // 300h bytes of stack
    dv_u16(D_STACK_TOP) = top;
    dv_u16(D_HEAP) = top;
    dv_u16(D_RESIDENT_END) = u16(u16((ADLIB_DS - ADLIB_PSP) << 4) + dv_u16(D_STACK_TOP));
    dv_u16(D_FREE) = u16(mem_u16(ADLIB_PSP, 0x06) - dv_u16(D_RESIDENT_END));
    // the argument string "c[ tail]" below SP after the pushes of the tail length, ES and 0
    dv_u16(D_ARGS) = u16(u16(top - 4) - (u16(len + 4) & 0xFFFE));
    // 0371 main(heap, free, args): the options (none), the banner, the size checks
    const u16 heap = dv_u16(D_HEAP), buffer = 0x0FFA, port = 0x0388, w = 0;
    if (buffer > dv_u16(D_FREE)) adl_unported("main: the buffer-too-large exit");
    if (adl_install_int65() != 0) adl_unported("main: the already-installed exit");
    adl_driver_setup(heap, buffer, port, w);
    // 02c0 keep_resident(buffer): INT 27h with DX = buffer + resident_end + 0Fh
}

bool adlib_is_int65(FarPtr v) { return v.seg == ADLIB_PSP && v.off == ADLIB_INT65_OFF; }
bool adlib_is_clock_isr(FarPtr v) { return v.seg == ADLIB_PSP && v.off == ADLIB_CLOCK_ISR_OFF; }

// PORT: INT 65h. The port runs the resident driver's handler when the vector points at it; there is
// no other INT 65h handler on the modelled machine.
void int65(u16 si, FarPtr esbx)
{
    const FarPtr v = mem_far(0, 0x65 * 4);
    if (!adlib_is_int65(v)) host_fatal("INT 65h: no Ad Lib sound driver is installed (vector %04X:%04X)", v.seg, v.off);
    adl_int65_handler(si, esbx);
}

// ================================================================ resident assembly

// ADLIB.COM 02ef int65_handler (adlib_driver.md §3): saves the caller's SS:SP (CS:020B), switches to
// the driver's stack (CS:020F/0211), copies argc[SI] words from ES:BX onto it and calls the C
// function table[SI] with DS = ES = SS = the driver's data; then IRET. SI is doubled in 16 bits
// (quirk: SI >= 8000h wraps onto 0..7FFFh). PORT: the stack switch is not modelled; AX after the
// IRET (whatever the function left) is not used by GB.EXE.
void adl_int65_handler(u16 si, FarPtr esbx)
{
    si = u16(si + si);
    if (si > 0x2E) adl_unported("int65_handler: the bad-function-number message (INT 21h 09h)");
    const u16 fn = dc_u16(u16(C_FN_TABLE + si));
    const u16 argc = dc_u16(u16(C_FN_ARGC + si));
    u16 a[5] = {0, 0, 0, 0, 0};
    for (u16 i = 0; i < argc && i < 5; i++) a[i] = mem_u16(esbx.seg, u16(esbx.off + 2 * i));
    switch (fn) {
    case 0x08DC: adl_fn_init(); return;
    case 0x1EE7: adl_note_on(a[0], a[1]); return;
    case 0x1FBF: adl_note_off(a[0]); return;
    case 0x223A: adl_set_voice_timbre(a[0], {a[1], a[2]}); return;
    default: break;
    }
    char what[64];
    std::snprintf(what, sizeof what, "INT 65h function %Xh (%04x)", unsigned(si >> 1), unsigned(fn));
    adl_unported(what);
}

// ADLIB.COM 0273 install_int65: if driver_installed() is non-zero it is returned (already
// installed); else the driver's stack (the start-up's stack top, SS) goes to CS:020F/0211, INT 65h to
// CS:02EF (DOS 25h), AX = 0.
u16 adl_install_int65()
{
    const u16 ax = adl_driver_installed();
    if (ax != 0) return ax;
    dc_u16(C_DRV_SP) = dv_u16(D_STACK_TOP);
    dc_u16(C_DRV_SS) = ADLIB_DS;
    mem_far_set(0, 0x65 * 4, {ADLIB_INT65_OFF, ADLIB_PSP});
    return 0;
}

// ADLIB.COM 0298 driver_installed: the INT 65h vector v (DOS 3565h); AX = the word at v.off - 18h if
// the 19 bytes at v.off - 16h are the driver's signature (CS:02D9), else 0.
u16 adl_driver_installed()
{
    const FarPtr v = mem_far(0, 0x65 * 4);
    const u16 di = u16(v.off - 0x18);
    const u16 ax = mem_u16(v.seg, di);
    for (u16 i = 0; i < 0x13; i++)  // REPE CMPSB CS:[SI], ES:[DI+2]
        if (dc_u8(u16(C_SIGNATURE + i)) != mem_u8(v.seg, u16(di + 2 + i))) return 0;
    return ax;
}

// ADLIB.COM 0586 pit_set_ch0 (AX): PIT channel 0, mode 3, divisor AX (36h to port 43h, AL, AH to
// port 40h). PORT: the host's timer; the handler is the INT 8 dispatch (timer.cpp).
void adl_pit_set_ch0(u16 ax) { host_set_timer(ax, timer_interrupt); }

// ADLIB.COM 05d4 clock_install: the PIT at 18.2 Hz (divisor 0), the chain flag set (every interrupt
// is a BIOS tick), divisor and accumulator 0; the old INT 8 vector to CS:056E (DOS 3508h), INT 8 to
// CS:0621 (DOS 2508h).
void adl_clock_install()
{
    adl_pit_set_ch0(0);
    dc_u16(C_CLK_CHAIN) = 1;
    dc_u16(C_CLK_DIVISOR) = 0;
    dc_u16(C_CLK_ACCUM) = 0;
    const FarPtr old = mem_far(0, 8 * 4);
    dc_u16(C_CLK_OLD) = old.off;
    dc_u16(u16(C_CLK_OLD + 2)) = old.seg;
    mem_far_set(0, 8 * 4, {ADLIB_CLOCK_ISR_OFF, ADLIB_PSP});
}

// ADLIB.COM 0621 clock_isr (INT 8, adlib_driver.md §5): the divisor is added to the accumulator; the
// old handler (the BIOS) is called when the chain flag or the carry says a BIOS tick is due, else
// the PIC gets its EOI. The interrupt count +1 (a dword kept below 80000000h); the countdown -1: when
// it reaches 0 and the sequencer tick is not running, seq_tick(count) on the driver's timer stack
// (SS = DS, SP = 031Ch) until the countdown is set again from its result. PORT: no PIC (the EOI does
// nothing); the saved registers and SS:SP (CS:0572) are not modelled.
void adl_clock_isr()
{
    const u32 sum = u32(dc_u16(C_CLK_ACCUM)) + dc_u16(C_CLK_DIVISOR);
    dc_u16(C_CLK_ACCUM) = u16(sum);
    const u16 ax = u16(dc_u16(C_CLK_CHAIN) + (sum >> 16));
    if (ax != 0) run_int8_handler(mem_far(ADLIB_PSP, C_CLK_OLD));  // PUSHF / CALL FAR CS:[056E]
    // else OUT 20h,20h: the EOI (PORT: no PIC)
    if (++dc_u16(C_CLK_COUNT) == 0) {
        if (s16(++dc_u16(u16(C_CLK_COUNT + 2))) < 0) dc_u16(u16(C_CLK_COUNT + 2)) = 0;
    }
    if (--dc_u16(C_CLK_COUNTDOWN) != 0) return;
    if (dc_u8(C_CLK_BUSY) != 0) return;
    for (;;) {
        const u16 hi = dc_u16(u16(C_CLK_COUNT + 2)), lo = dc_u16(C_CLK_COUNT);
        dc_u8(C_CLK_BUSY)++;
        const u16 next = adl_seq_tick(lo, hi);
        dc_u8(C_CLK_BUSY)--;
        const u16 bx = u16(-dc_u16(C_CLK_COUNTDOWN));
        if (bx < next) {
            dc_u16(C_CLK_COUNTDOWN) = u16(dc_u16(C_CLK_COUNTDOWN) + next);
            return;
        }
        dc_u16(C_CLK_COUNTDOWN) = 0;
    }
}

// ADLIB.COM 06ce SndOutput(reg, val): OUT genPort, reg; 6 reads of genPort; OUT genPort+1, val; 35
// reads (the OPL2's write delays). PORT: genPort is 388h (the installation has no /P option): the
// host's OPL2; the delay reads do nothing. (AL on return is the last status read, unused.)
void adl_snd_output(u16 reg, u16 val) { host_opl_write(u8(reg), u8(val)); }

// ADLIB.COM 23d9 ldiv: AX:BX / CX:DX, signed; quotient AX:BX, remainder CX:DX, both negated by the
// signs (the quotient's by sign(a) ^ sign(b), the remainder's by sign(a)). A zero divisor or
// dividend gives 0 and 0. The magnitudes (8000_0000h stays 2^31) are divided by shift-and-subtract:
// a divisor below 8000h by a 32-step loop with a 16-bit remainder (an exact division); a larger one
// by a 16-step loop that takes the dividend's high word as its first remainder, which is exact only
// when that word is below the divisor (the quotient then fits in BX); otherwise it keeps its
// (wrong) bits, and the remainder loses its top bit in RCL SI when it outgrows 32 bits.
AdlLdiv adl_ldiv(u32 a, u32 b)
{
    if (b == 0 || a == 0) return {0, 0};
    const u32 sign = (a ^ b) & 0x80000000u;  // [bp]: the dividend's high word XOR the divisor's
    const u32 ua = (a & 0x80000000u) ? u32(0u - a) : a;
    const u32 ub = (b & 0x80000000u) ? u32(0u - b) : b;
    u32 q, r;
    if (ub < 0x8000) {  // CX = 0 and DX < 8000h
        q = ua / ub;
        r = ua % ub;
    } else {
        u16 ax = u16(ua >> 16), bx = u16(ua), si = 0;
        const u16 cx = u16(ub >> 16), dx = u16(ub);
        for (int i = 0; i < 16; i++) {  // SHL BX,1 / RCL AX,1 / RCL SI,1
            const u16 c1 = u16(bx >> 15), c2 = u16(ax >> 15);
            bx = u16(bx << 1);
            ax = u16(ax << 1 | c1);
            si = u16(si << 1 | c2);
            if (si > cx || (si == cx && ax >= dx)) {  // SUB AX,DX / SBB SI,CX / INC BX
                const u32 rest = (u32(si) << 16 | ax) - ub;
                si = u16(rest >> 16);
                ax = u16(rest);
                bx = u16(bx + 1);
            }
        }
        q = bx;  // AX = 0
        r = u32(si) << 16 | ax;
    }
    if (sign) q = u32(0u - q);
    if (a & 0x80000000u) r = u32(0u - r);
    return {q, r};
}

// ADLIB.COM 2481 lmul: AX:BX * CX:DX, the low 32 bits in AX:BX.
u32 adl_lmul(u32 a, u32 b) { return a * b; }

// ADLIB.COM 24ae inp(port): AX = IN port (AH = 0). PORT: the only port read is 388h, the OPL2's
// status: 06h, the chip with no timer flags (its low bits read as 1 on the OPL2).
u16 adl_inp(u16 port)
{
    (void)port;
    return 0x0006;
}

// ================================================================ installation (C)

// ADLIB.COM 08aa driver_setup(buf, size, port, w): the event buffer (size / 10 nodes of 10 bytes),
// SoundColdInit(port, w), the sequencer stopped, the timer hook.
void adl_driver_setup(u16 buf, u16 size, u16 port, u16 w)
{
    dv_u16(D_BUFFER) = buf;
    dv_u16(D_BUFFER_NODES) = div32_16(size, 10);
    adl_sound_cold_init(port, w);
    dv_u16(D_SEQ_PLAYING) = 0;
    adl_clock_install();
}

// ADLIB.COM 20cf SoundColdInit(port, w): genPort; the frequency tables; OPL register 1 = 20h (wave
// select enable), register 4 = E0h (timer flags reset, both timers masked); SoundWarmInit. (w unused.)
void adl_sound_cold_init(u16 port, u16 w)
{
    (void)w;
    dv_u16(D_GEN_PORT) = port;
    adl_init_fnums();
    adl_snd_output(0x01, 0x20);
    adl_snd_output(0x04, 0xE0);
    adl_sound_warm_init();
}

// ADLIB.COM 17d6 InitFNums: 25 tables of 12 F-numbers (fNumTbl, 24 bytes each) for pitch steps of
// 4/100 of a half-tone; fNumFreqPtr[0..10] = fNumTbl, halfToneOffset = 0; noteDIV12 / noteMOD12
// for the 96 notes.
void adl_init_fnums()
{
    u16 num = 0;
    for (u16 i = 0; i < 25; i++) {
        adl_set_fnum(u16(D_FNUM_TBL + i * 0x18), num, 100);
        num = u16(num + 4);
    }
    for (u16 i = 0; i < 11; i++) {
        dv_u16(u16(D_FNUM_PTR + 2 * i)) = D_FNUM_TBL;
        dv_u16(u16(D_HALF_TONE + 2 * i)) = 0;
    }
    u16 k = 0;
    for (u16 oct = 0; oct < 8; oct++)
        for (u16 n = 0; n < 12; n++, k++) {
            dv_u8(u16(D_NOTE_DIV12 + k)) = u8(oct);
            dv_u8(u16(D_NOTE_MOD12 + k)) = u8(n);
        }
}

// ADLIB.COM 1756 SetFNum(fvec, num, den): fvec[0] = (CalcPremFNum + 4) >> 3, then each half-tone
// val = val * 106 / 100 and fvec[i] = (val + 4) >> 3; the sum and the shift in 16 bits (the carry of
// the +4 is lost, SHR).
void adl_set_fnum(u16 fvec, u16 num, u16 den)
{
    u32 val = adl_calc_prem_fnum(num, den);
    dv_u16(fvec) = u16(u16(lo16(val) + 4) >> 3);
    fvec = u16(fvec + 2);
    for (s16 i = 1; i < 12; i++) {
        val = adl_lmul(val, 0x6A);
        const u16 at = fvec;
        fvec = u16(fvec + 2);
        val = adl_ldiv(val, 0x64).quot;
        dv_u16(at) = u16(u16(lo16(val) + 4) >> 3);
    }
}

// ADLIB.COM 16b5 CalcPremFNum(num, den): the F-number (x8) of the first note of a table:
// ((num*6 + den*100) * 52088 / (den*100 * 25)) << 14, * 9 / 111875 (1B503h). The products num*6 and
// den*100 are 16-bit (IMUL, low word) and sign-extended; the rest is long arithmetic.
u32 adl_calc_prem_fnum(u16 num, u16 den)
{
    const u32 den100 = u32(s32(s16(u16(den * 100))));
    const u32 num6 = u32(s32(s16(u16(num * 6))));
    u32 v = adl_lmul(u32(num6 + den100), 0xCB78);
    const u32 d = adl_lmul(den100, 0x19);
    v = adl_ldiv(v, d).quot;
    v = v << 14;  // 14 x SHL BX / RCL AX
    v = adl_lmul(v, 9);
    return adl_ldiv(v, 0x1B503).quot;
}

// ADLIB.COM 2100 SoundWarmInit: all 9 voices key off (B0h+v = 0); relative volumes 1/1; every slot's
// level 3Fh and release 0Fh (with its register writes); 100 status reads (a delay: summed into a
// local that is never used); amDepth, vibDepth, noteSel, percussion = 0; SetWaveSel(0);
// SetPitchRange(1); melodic mode; SetGParam(&amDepth) (quirk below); the default slot parameters;
// the F-number pointers.
// Quirk: SetGParam takes three words, but gets the byte array amDepth..percussion: amDepth = its own
// byte, vibDepth = noteSel's byte (0) and noteSel = the low byte of pianoParamsOp0[0] (1): the note
// select bit (register 8 = 40h) ends up set.
void adl_sound_warm_init()
{
    for (s16 i = 0; i < 9; i++) adl_snd_output(u16(0xB0 + i), 0);
    adl_init_slot_volume();
    for (s16 i = 0; i < 18; i++) {
        adl_set_slot_prm(u16(i), 8, 0x3F);
        adl_set_slot_prm(u16(i), 7, 0x0F);
    }
    u16 delay = 0;  // [bp+6], uninitialised in the original; never read
    for (s16 i = 0; i < 100; i++) delay = u16(delay + adl_inp(0x388));
    (void)delay;
    for (s16 i = 0; i < 4; i++) dv_u8(u16(D_AM_DEPTH + i)) = 0;
    adl_set_wave_sel(0);
    adl_set_pitch_range(1);
    adl_set_perc_mode(0);
    adl_set_gparam({D_AM_DEPTH, ADLIB_DS});
    adl_init_slot_params();
    adl_init_fnum_ptrs();
}

// ================================================================ INT 65h function 0 and its callees

// ADLIB.COM 08dc fn_init (function 0, adlib_driver.md §4.1): the sequencer stopped; SoundWarmInit;
// SetMode(0); function 0Ah's value 0; the event queue, pool and voice tables reset; tempo 90.
void adl_fn_init()
{
    dv_u16(D_SEQ_PLAYING) = 0;
    dv_u16(D_SEQ_ACTIVE) = 0;
    adl_sound_warm_init();
    adl_set_mode(0);
    adl_set_fn0a_value(0);
    adl_init_event_queue();
    adl_init_event_pool();
    dv_u16(D_ACT_VOICE) = 0;
    dv_u16(D_SEQ_START_HI) = 0;
    dv_u16(D_SEQ_START_LO) = 0;
    adl_init_voice_state();
    adl_clear_voice_flags();
    adl_clear_event_ptrs();
    adl_set_tempo(0x5A);
}

// ADLIB.COM 0991 SetMode(mode) (function 6): keeps modeWaveSel; SoundWarmInit when the mode changes;
// mode; SetPercMode(mode); SetWaveSel(the kept value).
void adl_set_mode(u16 mode)
{
    const u16 saved = dv_u16(D_MODE_WAVE_SEL);
    if (dv_u16(D_MODE) != mode) adl_sound_warm_init();
    dv_u16(D_MODE) = mode;
    adl_set_perc_mode(mode);
    adl_set_wave_sel(saved);
}

// ADLIB.COM 0c23 (function 0Ah): stores its argument (read back by function 0Bh).
void adl_set_fn0a_value(u16 v) { dv_u16(D_FN0A_VALUE) = v; }

// ADLIB.COM 1491 init_event_queue: 54 nodes of 10 bytes at DS:031E linked by their first word (the
// last one to DS:053A); head and count 0, the free list at DS:031E.
void adl_init_event_queue()
{
    for (s16 i = 0; i < 0x36; i++) dv_u16(u16(D_QUEUE + i * 10)) = u16(D_QUEUE + 10 + i * 10);
    dv_u16(D_QUEUE_HEAD) = 0;
    dv_u16(D_QUEUE_COUNT) = 0;
    dv_u16(D_QUEUE_FREE) = D_QUEUE;
}

// ADLIB.COM 070a init_event_pool: the resident buffer as a list of 10-byte nodes: pool = buffer,
// node i -> node i + 1 for i < nodes - 1 (signed), the last node's link 0.
void adl_init_event_pool()
{
    u16 node = dv_u16(D_BUFFER);
    const s16 last = s16(dv_u16(D_BUFFER_NODES) - 1);
    dv_u16(D_EVENT_POOL) = node;
    for (s16 i = 0; i < last; i++) {
        dv_u16(node) = u16(node + 10);
        node = u16(node + 10);
    }
    dv_u16(node) = 0;
}

// ADLIB.COM 0773 init_voice_state: the first byte of each of the 11 voice records (5 bytes) = 1.
void adl_init_voice_state()
{
    for (s16 i = 0; i < 11; i++) dv_u8(u16(D_VOICE_STATE + i * 5)) = 1;
}

// ADLIB.COM 074e clear_voice_flags: 77 bytes at DS:063D = 0.
void adl_clear_voice_flags()
{
    for (s16 i = 0; i < 0x4D; i++) dv_u8(u16(D_VOICE_FLAGS + i)) = 0;
}

// ADLIB.COM 079c clear_event_ptrs: for the 6 event types (DS:06C8) and the 11 voices, the words at
// event_slot_b and then event_slot_a = 0. Quirk: for the types whose slot exists only for voice 0,
// event_slot_a returns 0 for the other voices, and DS:0000 is written (with 0).
void adl_clear_event_ptrs()
{
    for (s16 t = 0; t < 6; t++)
        for (s16 v = 0; v < 11; v++) {
            const u16 pa = adl_event_slot_a(dv_u16(u16(D_EVENT_TYPES + 2 * t)), u16(v));
            const u16 pb = adl_event_slot_b(dv_u16(u16(D_EVENT_TYPES + 2 * t)), u16(v));
            dv_u16(pb) = 0;
            dv_u16(pa) = 0;
        }
}

// ADLIB.COM 1212 event_slot_a(type, voice): a word per voice for the event types 2, 4, 5, 7; one
// word, for voice 0 only (else 0), for type 3 and the others.
u16 adl_event_slot_a(u16 type, u16 voice)
{
    switch (u16(type - 2)) {  // SUB SI,2 / JB default / CMP SI,6 / JAE default
    case 0: return u16(u16(voice << 1) + 0x05D7);  // type 2
    case 1: return voice == 0 ? 0x062F : 0;          // type 3
    case 2: return u16(u16(voice << 1) + 0x05AB);  // type 4
    case 3: return u16(u16(voice << 1) + 0x0603);  // type 5
    case 5: return u16(u16(voice << 1) + 0x057F);  // type 7
    default: return voice == 0 ? 0x0633 : 0;         // type 6 and the others
    }
}

// ADLIB.COM 129e event_slot_b(type, voice): the second word of the same events.
u16 adl_event_slot_b(u16 type, u16 voice)
{
    switch (u16(type - 2)) {
    case 0: return u16(u16(voice << 1) + 0x05ED);  // type 2
    case 1: return 0x0631;                           // type 3
    case 2: return u16(u16(voice << 1) + 0x05C1);  // type 4
    case 3: return u16(u16(voice << 1) + 0x0619);  // type 5
    case 5: return u16(u16(voice << 1) + 0x0595);  // type 7
    default: return 0x0635;                          // type 6 and the others
    }
}

// ADLIB.COM 083e set_tempo(tempo): tempo (at least 13h); clock_ticks = ticks_per_beat * tempo / 60
// (long; the unclamped argument). The PIT would be reprogrammed only while the sequencer is active.
void adl_set_tempo(u16 tempo)
{
    dv_u16(D_TEMPO) = tempo < 0x13 ? 0x13 : tempo;
    const u32 p = adl_lmul(dv_u16(D_TICKS_PER_BEAT), tempo);
    const u32 q = adl_ldiv(p, 0x3C).quot;
    dv_u16(D_CLOCK_TICKS) = lo16(q);
    // PORT: the sequencer (set_clock_rate 160f, INT 65h functions 1..12h) is not ported; GB.EXE never
    // starts it, so seq_active stays 0.
    if (dv_u16(D_SEQ_ACTIVE) != 0) adl_unported("set_clock_rate (160f): the driver's sequencer");
}

// ADLIB.COM 0cf8 seq_tick(lo, hi): the driver's own sequencer, run by clock_isr. Idle (FFFFh: the
// next call in 65535 interrupts) unless it is active and playing. PORT: the playing part is not
// ported (GB.EXE never starts the sequencer).
u16 adl_seq_tick(u16 lo, u16 hi)
{
    (void)lo;
    (void)hi;
    if (dv_u16(D_SEQ_ACTIVE) == 0 || dv_u16(D_SEQ_PLAYING) == 0) return 0xFFFF;
    adl_unported("seq_tick (0cf8): the driver's sequencer");
}

// ================================================================ the chip set-up (C)

// ADLIB.COM 198e InitSlotVolume: slotRelVolume[18] = 1/1.
void adl_init_slot_volume()
{
    for (s16 i = 0; i < 18; i++) {
        dv_u8(u16(D_SLOT_REL_VOLUME + 2 * i + 1)) = 1;
        dv_u8(u16(D_SLOT_REL_VOLUME + 2 * i)) = 1;
    }
}

// ADLIB.COM 165a SetPitchRange(range): at most 12 (unsigned); pitchRangeStep = range * 25.
void adl_set_pitch_range(u16 range)
{
    if (range > 12) range = 12;
    dv_u16(D_PITCH_RANGE) = range;
    dv_u16(D_PITCH_RANGE_STEP) = u16(range * 25);
}

// ADLIB.COM 167e SetWaveSel(state): modeWaveSel = state; every slot's wave select register
// (E0h + offsetSlot) = 0.
void adl_set_wave_sel(u16 state)
{
    dv_u16(D_MODE_WAVE_SEL) = state;
    for (s16 i = 0; i < 18; i++) adl_snd_output(u16(0xE0 + dv_u8(u16(D_OFFSET_SLOT + i))), 0);
}

// ADLIB.COM 186f InitFNumPtrs: fNumFreqPtr[0..10] = fNumTbl, halfToneOffset[0..10] = 0.
void adl_init_fnum_ptrs()
{
    for (s16 i = 0; i < 11; i++) {
        dv_u16(u16(D_FNUM_PTR + 2 * i)) = D_FNUM_TBL;
        dv_u16(u16(D_HALF_TONE + 2 * i)) = 0;
    }
}

// ADLIB.COM 19b9 SetPercMode(mode): for the percussion mode, voices 6-8 off and the tom (voice 8,
// pitch 24) and snare (voice 7, pitch 31) frequencies set; percussion = mode != 0, percBits = 0;
// register BDh; the default slot parameters.
void adl_set_perc_mode(u16 mode)
{
    if (mode != 0) {
        adl_sound_chut(6);
        adl_sound_chut(7);
        adl_sound_chut(8);
        adl_set_freq(8, 0x18, 0);
        dv_u8(D_TOM_PITCH) = 0x18;
        dv_u8(D_SD_PITCH) = 0x1F;
        adl_set_freq(7, 0x1F, 0);
    }
    dv_u8(D_PERC_BITS) = 0;
    dv_u8(D_PERCUSSION) = mode != 0 ? 1 : 0;
    adl_snd_s_am_vib_rhythm();
    adl_init_slot_params();
}

// ADLIB.COM 21ba InitSlotParams: every slot gets the piano parameters (operator 0 or, for a carrier
// slot, operator 1) with wave 0.
void adl_init_slot_params()
{
    for (s16 i = 0; i < 18; i++) {
        if (dv_u8(u16(D_CARRIER_SLOT + i)) != 0) adl_set_slot_param(u16(i), {D_PIANO_OP1, ADLIB_DS}, 0);
        else adl_set_slot_param(u16(i), {D_PIANO_OP0, ADLIB_DS}, 0);
    }
}

// ADLIB.COM 2200 SetGParam(params far): amDepth, vibDepth, noteSel = the low bytes of the three
// words at params, each followed by SndSetPrm(-1, 14 + i) (register BDh or 08h).
void adl_set_gparam(FarPtr params)
{
    for (s16 i = 0; i < 3; i++) {
        const u16 w = far_u16(params);
        params = far_add(params, 2);
        dv_u8(u16(D_AM_DEPTH + i)) = u8(w);
        adl_snd_set_prm(0xFFFF, u16(i + 14));
    }
}

// ADLIB.COM 20a6 SoundChut(voice): A0h+voice = 0, B0h+voice = 0 (frequency and key off).
void adl_sound_chut(u16 voice)
{
    adl_snd_output(u16(voice + 0xA0), 0);
    adl_snd_output(u16(voice + 0xB0), 0);
}

// ================================================================ slot parameters (C)

// ADLIB.COM 1b36 SetSlotParamValue(slot, prm, val): paramSlot[slot][prm] = val (byte), then the
// register of that parameter (SndSetPrm).
void adl_set_slot_prm(u16 slot, u16 prm, u16 val)
{
    const u16 si = u16(u16(slot * PARAM_SLOT_SIZE) + prm);
    dv_u8(u16(D_PARAM_SLOT + si)) = u8(val);
    adl_snd_set_prm(slot, prm);
}

// ADLIB.COM 1b60 SndSetPrm(slot, prm): the register write of parameter prm (unsigned < 18; else
// nothing): 0/8 KSL-level, 2/12 feedback-FM, 3/6 attack-decay, 4/7 sustain-release, 1/5/9/10/11
// AM-VIB-EG-KSR-multi, 13 wave select, 14/15/17 register BDh, 16 note select.
void adl_snd_set_prm(u16 slot, u16 prm)
{
    switch (prm) {
    case 0: case 8: adl_snd_s_ksl_level(slot); break;
    case 1: case 5: case 9: case 10: case 11: adl_snd_s_avek(slot); break;
    case 2: case 12: adl_snd_s_feed_fm(slot); break;
    case 3: case 6: adl_snd_s_att_decay(slot); break;
    case 4: case 7: adl_snd_s_sus_release(slot); break;
    case 13: adl_snd_wave_select(slot); break;
    case 14: case 15: case 17: adl_snd_s_am_vib_rhythm(); break;
    case 16: adl_snd_s_note_sel(); break;
    default: break;
    }
}

// ADLIB.COM 1be2 SndSetAllPrm(slot): registers BDh, 08h and the slot's six.
void adl_snd_set_all_prm(u16 slot)
{
    adl_snd_s_am_vib_rhythm();
    adl_snd_s_note_sel();
    adl_snd_s_ksl_level(slot);
    adl_snd_s_feed_fm(slot);
    adl_snd_s_att_decay(slot);
    adl_snd_s_sus_release(slot);
    adl_snd_s_avek(slot);
    adl_snd_wave_select(slot);
}

// ADLIB.COM 1c1d SndSKslLevel(slot): register 40h + offsetSlot[slot] = KSL << 6 | (63 - v), v = (den +
// 2 * num * (63 - level)) / (2 * den) with the slot's relative volume num/den (16-bit MUL and DIV;
// a zero den divides by zero).
void adl_snd_s_ksl_level(u16 slot)
{
    const u16 p = u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE));
    const u16 cx = u16(0x3F - (dv_u8(u16(p + 8)) & 0x3F));
    const u16 num = dv_u8(u16(D_SLOT_REL_VOLUME + u16(slot << 1)));
    const u16 den = dv_u8(u16(D_SLOT_REL_VOLUME + u16(slot << 1) + 1));
    const u16 ax = u16(num * cx);  // MUL CX: the low word
    const u16 sum = u16(den + ax + ax);
    const u16 q = div32_16(sum, u16(den + den));
    const u16 val = u16(u16(0x3F - q) | u16(dv_u8(p) << 6));
    adl_snd_output(u16(dv_u8(u16(D_OFFSET_SLOT + slot)) + 0x40), val);
}

// ADLIB.COM 1c9d SndSNoteSel: register 08h = noteSel ? 40h : 0.
void adl_snd_s_note_sel() { adl_snd_output(0x08, dv_u8(D_NOTE_SEL) != 0 ? 0x40 : 0); }

// ADLIB.COM 1cc0 SndSFeedFm(slot): for a modulator slot only: register C0h + voiceSlot[slot] =
// feedback << 1 | (FM ? 0 : 1).
void adl_snd_s_feed_fm(u16 slot)
{
    if (dv_u8(u16(D_CARRIER_SLOT + slot)) != 0) return;
    const u16 p = u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE));
    const u16 val = u16(u16(dv_u8(u16(p + 2)) << 1) | (dv_u8(u16(p + 12)) != 0 ? 0 : 1));
    adl_snd_output(u16(dv_u8(u16(D_VOICE_SLOT + slot)) + 0xC0), val);
}

// ADLIB.COM 1d18 SndSAttDecay(slot): register 60h + offsetSlot = attack << 4 | decay & 0Fh.
void adl_snd_s_att_decay(u16 slot)
{
    const u16 p = u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE));
    const u16 val = u16(u16(dv_u8(u16(p + 3)) << 4) | (dv_u8(u16(p + 6)) & 0x0F));
    adl_snd_output(u16(dv_u8(u16(D_OFFSET_SLOT + slot)) + 0x60), val);
}

// ADLIB.COM 1d59 SndSSusRelease(slot): register 80h + offsetSlot = sustain << 4 | release & 0Fh.
void adl_snd_s_sus_release(u16 slot)
{
    const u16 p = u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE));
    const u16 val = u16(u16(dv_u8(u16(p + 4)) << 4) | (dv_u8(u16(p + 7)) & 0x0F));
    adl_snd_output(u16(dv_u8(u16(D_OFFSET_SLOT + slot)) + 0x80), val);
}

// ADLIB.COM 1d9a SndSAVEK(slot): register 20h + offsetSlot = AM 80h + VIB 40h + EG 20h + KSR 10h +
// multi & 0Fh (each flag: its byte non-zero).
void adl_snd_s_avek(u16 slot)
{
    const u16 p = u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE));
    u16 val = dv_u8(u16(p + 9)) != 0 ? 0x80 : 0;
    val = u16(val + (dv_u8(u16(p + 10)) != 0 ? 0x40 : 0));
    val = u16(val + (dv_u8(u16(p + 5)) != 0 ? 0x20 : 0));
    val = u16(val + (dv_u8(u16(p + 11)) != 0 ? 0x10 : 0));
    val = u16(val + (dv_u8(u16(p + 1)) & 0x0F));
    adl_snd_output(u16(dv_u8(u16(D_OFFSET_SLOT + slot)) + 0x20), val);
}

// ADLIB.COM 1e4b SndSAmVibRhythm: register BDh = amDepth 80h | vibDepth 40h | percussion 20h |
// percBits.
void adl_snd_s_am_vib_rhythm()
{
    u16 val = dv_u8(D_AM_DEPTH) != 0 ? 0x80 : 0;
    val |= dv_u8(D_VIB_DEPTH) != 0 ? 0x40 : 0;
    val = u16((dv_u8(D_PERCUSSION) != 0 ? 0x20 : 0) | val);
    val |= dv_u8(D_PERC_BITS);
    adl_snd_output(0xBD, val);
}

// ADLIB.COM 1ea3 SndWaveSelect(slot): register E0h + offsetSlot = the slot's wave (& 3) when
// modeWaveSel, else 0.
void adl_snd_wave_select(u16 slot)
{
    u16 val = 0;
    if (dv_u16(D_MODE_WAVE_SEL) != 0) val = dv_u8(u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE) + 13)) & 3;
    adl_snd_output(u16(dv_u8(u16(D_OFFSET_SLOT + slot)) + 0xE0), val);
}

// ADLIB.COM 2319 SetSlotParam(slot, params far, wave): paramSlot[slot][0..12] = the low bytes of the
// 13 words at params, [13] = wave & 3; then all its registers (SndSetAllPrm).
void adl_set_slot_param(u16 slot, FarPtr params, u16 wave)
{
    u16 dst = u16(D_PARAM_SLOT + u16(slot * PARAM_SLOT_SIZE));
    for (s16 i = 0; i < 13; i++) {
        const u16 w = far_u16(params);
        params = far_add(params, 2);
        dv_u8(dst) = u8(w);
        dst = u16(dst + 1);
    }
    dv_u8(dst) = u8(wave & 3);
    adl_snd_set_all_prm(slot);
}

// ================================================================ INT 65h functions 13h..15h (C)

// ADLIB.COM 223a SetVoiceTimbre(voice, params far) (function 15h, adlib_driver.md §4.4): params =
// operator 0's 13 words, operator 1's 13 words (+1Ah), and the two wave selects at +34h and +36h.
// Melodic voices (or no percussion mode: voice < 6 always): both slots of slotVoice[voice]; the bass
// drum (6): slotPerc[0]'s two slots; voices 7..10: slotPerc[voice - 6]'s first slot, operator 0; a
// percussion voice above 10: nothing. Quirk: GB.EXE's timbres are 34h bytes, so the wave selects are
// the next four bytes (the next timbre's, or whatever follows).
void adl_set_voice_timbre(u16 voice, FarPtr params)
{
    const u16 wave0 = far_u16(params, 0x34);
    const u16 wave1 = far_u16(params, 0x36);
    const FarPtr op1 = far_add(params, 0x1A);
    if (dv_u8(D_PERCUSSION) == 0 || s16(voice) < 6) {
        adl_set_slot_param(dv_u8(u16(D_SLOT_VOICE + u16(voice << 1))), params, wave0);
        adl_set_slot_param(dv_u8(u16(D_SLOT_VOICE + u16(voice << 1) + 1)), op1, wave1);
        return;
    }
    if (s16(voice) > 10) return;
    if (voice == 6) {
        adl_set_slot_param(dv_u8(D_SLOT_PERC), params, wave0);
        adl_set_slot_param(dv_u8(u16(D_SLOT_PERC + 1)), op1, wave1);
        return;
    }
    adl_set_slot_param(dv_u8(u16(D_SLOT_PERC - 12 + u16(voice << 1))), params, wave0);  // slotPerc[voice - 6][0]
}

// ADLIB.COM 1ee7 NoteOn(voice, pitch) (function 13h, adlib_driver.md §4.2; GB.EXE passes the MIDI
// note - 3Ch): keyOn = 0 for a percussion voice (percussion mode and voice >= 6, unsigned), else 20h,
// stored in voiceKeyOn[voice]; pitch += 30h (below 0, signed: 0), stored in voiceNote[voice] (byte).
// A melodic voice (<= 8) sounds with SetFreq(voice, pitch, 20h); a percussion voice: the bass drum
// (6) or the tom (8, and the snare 7 at pitch + 7) get their frequency, then its bit in percBits and
// register BDh.
void adl_note_on(u16 voice, u16 pitch)
{
    const u16 key_on = dv_u8(D_PERCUSSION) != 0 && voice >= 6 ? 0 : 0x20;
    dv_u8(u16(voice + D_VOICE_KEY_ON)) = u8(key_on);
    // ADD BX,30h / JGE: SF = OF tests the true signed sum; a sum past 7FFFh is kept as it wraps.
    const u16 p = int(s16(pitch)) + 0x30 < 0 ? 0 : u16(pitch + 0x30);
    dv_u8(u16(voice + D_VOICE_NOTE)) = u8(p);
    if (key_on != 0) {
        if (voice > 8) return;
        adl_set_freq(voice, p, key_on);
        return;
    }
    if (voice < 6) return;
    if (voice == 6) {
        adl_set_freq(6, p, 0);
    } else if (voice == 8) {
        adl_set_freq(8, p, 0);
        const u16 sd = u16(p + 7);
        dv_u8(D_SD_PITCH) = u8(sd);
        adl_set_freq(7, u16(u8(sd)), 0);
    }
    const u16 mask = dv_u8(u16(voice - 6 + D_PERC_MASKS));
    dv_u8(D_PERC_BITS) = u8(dv_u8(D_PERC_BITS) | mask);
    adl_snd_s_am_vib_rhythm();
}

// ADLIB.COM 1fbf NoteOff(voice) (function 14h, adlib_driver.md §4.3): voiceKeyOn[voice] = 0; a
// percussion voice (percussion mode and voice >= 6, signed) loses its bit in percBits (register
// BDh); a melodic voice (<= 8, signed) gets its last note again with the key off.
void adl_note_off(u16 voice)
{
    dv_u8(u16(voice + D_VOICE_KEY_ON)) = 0;
    if (dv_u8(D_PERCUSSION) != 0 && s16(voice) >= 6) {
        const u16 mask = u16(~u16(dv_u8(u16(voice + D_PERC_MASKS - 6))));
        dv_u8(D_PERC_BITS) = u8(dv_u8(D_PERC_BITS) & mask);
        adl_snd_s_am_vib_rhythm();
        return;
    }
    if (s16(voice) > 8) return;
    adl_set_freq(voice, dv_u8(u16(voice + D_VOICE_NOTE)), 0);
}

// ADLIB.COM 2018 SetFreq(voice, pitch, keyOn): pitch += halfToneOffset[voice], limited to 0..95
// (signed); fN = the word at fNumFreqPtr[voice] + 2 * noteMOD12[pitch]; register A0h + voice = fN low
// byte; B0h + voice = keyOn + noteDIV12[pitch] * 4 + (fN >> 8 & 3) (added, low byte).
void adl_set_freq(u16 voice, u16 pitch, u16 key_on)
{
    s16 p = s16(u16(dv_u16(u16(D_HALF_TONE + u16(voice << 1))) + pitch));
    if (p > 0x5F) p = 0x5F;
    if (p < 0) p = 0;
    const u16 fn = dv_u16(u16(dv_u16(u16(D_FNUM_PTR + u16(voice << 1))) + u16(dv_u8(u16(D_NOTE_MOD12 + p)) << 1)));
    adl_snd_output(u16(voice + 0xA0), fn);
    const u16 val = u16(key_on + u16(dv_u8(u16(D_NOTE_DIV12 + p)) << 2) + (u8(fn >> 8) & 3));
    adl_snd_output(u16(voice + 0xB0), val);
}

} // namespace gb
