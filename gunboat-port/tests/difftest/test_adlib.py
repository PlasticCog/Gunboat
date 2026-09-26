"""The Ad Lib sound driver ADLIB.COM V1.51 (spec adlib_driver.md, port sound/adlib_driver.cpp) and
GB.EXE's AdLib back end (1af5, sound.md §4.5), against ADLIB.COM's own code in Unicorn.

The user's ADLIB.COM is loaded where the port puts it (PSP = CS = 0B00h, DS = CS + 25Ch) and runs
its own installer (start-up, main, INT 27h) in Unicorn: that state is the start of the other
tests. Every check compares all memory except the driver's stacks and the caller's SS:SP it saves
(IGNORE), the registers listed, the OPL2 writes in order (soundmodel.py: ports 388h/389h against
host_opl_write) and the PIT channel 0 divisors. INT 65h goes through the vector as on the CPU."""
import contextlib
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CS, UC_X86_REG_DX, UC_X86_REG_IP,
                               UC_X86_REG_SP, UC_X86_REG_SS)

from gbdiff import (DGROUP, DS_BASE, FAR_RET, GAME_DIR, TEST_SP, Mismatch, lin, put8, put16)
from soundmodel import SoundModel
from test_sound import MUS_SEG, SCRATCH, after, random_stream
from test_title import POLLS, main_state, spaced

PSP = 0x0B00                      # ADLIB_PSP (sound.hpp): the driver's PSP and CS
DRV = PSP + 0x025C                # its DS = SS = ES
DRV_SP = 0x0F00                   # SP for direct calls of its C functions (inside its INT 65h stack)
COM = GAME_DIR / 'ADLIB.COM'
RESIDENT_DX = 0x44E9              # INT 27h: the bytes kept (buffer FFAh + 34E0h + 0Fh)
KSL_DIV = 0x1C6C                  # SndSKslLevel's DIV BX (the driver's only DIV that can see 0)

IGNORE = [(lin(DRV, 0x0C20), lin(DRV, 0x0F20)),   # the INT 65h stack (the start-up's stack before)
          (lin(DRV, 0x011C), lin(DRV, 0x031C)),   # the timer's stack
          (lin(PSP, 0x020B), lin(PSP, 0x020F)),   # INT 65h: the caller's SS:SP
          (lin(PSP, 0x0572), lin(PSP, 0x0576))]   # INT 8: the caller's SS:SP

# The driver's routines: CS offset and convention (near C unless noted).
NEAR = False
FUNCS = {
    'adl_install_int65': 0x0273, 'adl_driver_installed': 0x0298, 'adl_snd_output': 0x06CE,
    'adl_init_event_pool': 0x070A, 'adl_clear_voice_flags': 0x074E, 'adl_init_voice_state': 0x0773,
    'adl_clear_event_ptrs': 0x079C, 'adl_set_tempo': 0x083E, 'adl_driver_setup': 0x08AA, 'adl_fn_init': 0x08DC,
    'adl_set_mode': 0x0991, 'adl_set_fn0a_value': 0x0C23, 'adl_seq_tick': 0x0CF8, 'adl_event_slot_a': 0x1212,
    'adl_event_slot_b': 0x129E, 'adl_init_event_queue': 0x1491, 'adl_set_pitch_range': 0x165A,
    'adl_set_wave_sel': 0x167E, 'adl_calc_prem_fnum': 0x16B5, 'adl_set_fnum': 0x1756, 'adl_init_fnums': 0x17D6,
    'adl_init_fnum_ptrs': 0x186F, 'adl_init_slot_volume': 0x198E, 'adl_set_perc_mode': 0x19B9,
    'adl_set_slot_prm': 0x1B36, 'adl_snd_set_prm': 0x1B60, 'adl_snd_set_all_prm': 0x1BE2,
    'adl_snd_s_ksl_level': 0x1C1D, 'adl_snd_s_note_sel': 0x1C9D, 'adl_snd_s_feed_fm': 0x1CC0,
    'adl_snd_s_att_decay': 0x1D18, 'adl_snd_s_sus_release': 0x1D59, 'adl_snd_s_avek': 0x1D9A,
    'adl_snd_s_am_vib_rhythm': 0x1E4B, 'adl_snd_wave_select': 0x1EA3, 'adl_note_on': 0x1EE7,
    'adl_note_off': 0x1FBF, 'adl_set_freq': 0x2018, 'adl_sound_chut': 0x20A6, 'adl_sound_cold_init': 0x20CF,
    'adl_sound_warm_init': 0x2100, 'adl_init_slot_params': 0x21BA, 'adl_set_gparam': 0x2200,
    'adl_set_voice_timbre': 0x223A, 'adl_set_slot_param': 0x2319, 'adl_inp': 0x24AE,
    # assembly with register arguments (near)
    'adl_pit_set_ch0': 0x0586, 'adl_clock_install': 0x05D4, 'adl_ldiv': 0x23D9, 'adl_lmul': 0x2481,
}
INTERRUPTS = {'adl_int65_handler': 0x02EF, 'adl_clock_isr': 0x0621}

# driver data (DS offsets), adlib_driver.md §2
VOICE_NOTE, VOICE_KEY_ON, PERC_BITS, PERCUSSION = 0x06FE, 0x0709, 0x06F8, 0x07FB
AM_DEPTH, PARAM_SLOT, REL_VOLUME, HALF_TONE, FNUM_PTR, FNUM_TBL = 0x07F8, 0x0834, 0x07D4, 0x0BDA, 0x0BF0, 0x0982
MODE, MODE_WAVE_SEL, SEQ_ACTIVE, SEQ_PLAYING, SD_PITCH = 0x06C6, 0x0C0A, 0x06BC, 0x06BE, 0x0705
C_CHAIN, C_DIVISOR, C_ACCUM, C_OLD, C_COUNTDOWN, C_COUNT, C_BUSY = 0x568, 0x56A, 0x56C, 0x56E, 0x576, 0x578, 0x57C
GB_TIMBRES = (0x0822, 0x0856, 0x088A)   # DGROUP: the instruments music_start sets


def dput8(m, off, v):
    m[lin(DRV, off)] = v & 0xFF


def dput16(m, off, v):
    struct.pack_into('<H', m, lin(DRV, off), v & 0xFFFF)


def dget16(m, off):
    return struct.unpack_from('<H', m, lin(DRV, off))[0]


def cput16(m, off, v):
    struct.pack_into('<H', m, lin(PSP, off), v & 0xFFFF)


DIV0 = {'cases': 0}


def check(h, name, m, **kw):
    """h.check, except that a divide error of the original (SndSKslLevel's DIV by 2 x a zero
    relative-volume denominator, read for a slot outside the 18) must be the port's divide error
    too (R6003): then nothing else is compared (the program ends there on both sides). The
    original is stopped at that DIV (a #DE intercepted in Unicorn leaves the CPU faulted)."""
    try:
        return h.check(name, m, **kw)
    except Mismatch as e:
        if not str(e).startswith('original: divide error'):
            raise
    h.port.set_memory(m)
    try:
        h.port.call(name, kw.get('regs') or {}, kw.get('stack_args', ()))
    except Mismatch as e:
        if 'R6003' in str(e):
            DIV0['cases'] += 1
            return None
        raise
    raise Mismatch('%s [%s]: the original divides by zero, the port does not' % (name, kw.get('label', '')))


def load_com(m):
    """DOS loads ADLIB.COM (mirror of adlib_load): the PSP and the file at PSP:0100."""
    data = COM.read_bytes()
    base = lin(PSP, 0)
    m[base:base + 0x100] = bytes(0x100)
    m[base:base + 2] = b'\xcd\x20'
    struct.pack_into('<H', m, base + 2, 0xA000)
    m[base + 5:base + 10] = b'\x9a\xf0\xfe\x1d\xf0'
    m[base + 0x50:base + 0x53] = b'\xcd\x21\xcb'
    m[base + 0x80:base + 0x82] = b'\x00\x0d'
    m[base + 0x100:base + 0x100 + len(data)] = data
    return m


@contextlib.contextmanager
def adlib(h):
    """The sound hardware with the OPL2, ADLIB.COM's routines callable by name, its stacks not
    compared, and the DOS services its installer uses (INT 21h 30h, 06h; INT 27h)."""
    model = SoundModel(h)
    model.install()
    h.extensions.append(model)
    for name, off in FUNCS.items():
        h.add_function(name, PSP, off, NEAR, ds=DRV, sp=DRV_SP)
    for name, off in INTERRUPTS.items():
        h.add_function(name, PSP, off, 'iret')
    h.add_function('adlib_install', PSP, 0x0100, True)
    if not getattr(h.sym, '_adlib_regions', False):
        h.sym.add_region(lin(DRV, 0), lin(DRV, 0x2000), 'ADLIB DS', lin(DRV, 0))
        h.sym.add_region(lin(PSP, 0), lin(DRV, 0), 'ADLIB CS', lin(PSP, 0))
        h.sym._adlib_regions = True
    saved_ignore = list(h.ignore)
    h.ignore += IGNORE
    o = h.orig
    int21, int27 = o.ints.get(0x21), o.ints.get(0x27)
    kept = []

    def dos(uc):
        ax = uc.reg_read(UC_X86_REG_AX)
        if ax >> 8 == 0x30:                               # DOS version: 5.00
            uc.reg_write(UC_X86_REG_AX, 0x0005)
        elif ax >> 8 == 0x06:                             # direct console output: no console
            uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | (uc.reg_read(UC_X86_REG_DX) & 0xFF))
        else:
            int21(uc)

    def keep(uc):                                        # INT 27h: the run ends (returns to the test)
        kept.append(uc.reg_read(UC_X86_REG_DX))
        uc.reg_write(UC_X86_REG_SS, DGROUP)
        uc.reg_write(UC_X86_REG_SP, TEST_SP)
        uc.reg_write(UC_X86_REG_CS, FAR_RET[0])
        uc.reg_write(UC_X86_REG_IP, FAR_RET[1])
    def div0(uc, address, size, _):                      # stop before a divide error (see check)
        if uc.reg_read(UC_X86_REG_BX) == 0:
            o.fail('divide error at %04X:%04X' % (PSP, KSL_DIV))
    o.ints[0x21] = dos
    o.ints[0x27] = keep
    div_hook = o.uc.hook_add(UC_HOOK_CODE, div0, None, lin(PSP, KSL_DIV), lin(PSP, KSL_DIV))
    model.kept = kept
    try:
        yield model
    finally:
        o.uc.hook_del(div_hook)
        o.ints[0x21] = int21
        if int27 is None:
            o.ints.pop(0x27, None)
        else:
            o.ints[0x27] = int27
        h.ignore[:] = saved_ignore
        for name in list(FUNCS) + list(INTERRUPTS) + ['adlib_install']:
            h.extra_funcs.pop(name, None)
        model.uninstall()


def install(h, model, m):
    """ADLIB.COM run on m (with the file loaded), checked: the resident state."""
    del model.kept[:]
    h.check('adlib_install', load_com(m), regs={'es': PSP})
    if model.kept != [RESIDENT_DX]:
        raise Mismatch('adlib_install: INT 27h DX %s, expected %04X' % (model.kept, RESIDENT_DX))
    return after(h)


_cache = {}


def installed(h, model):
    """Fresh memory with the driver installed (cached per harness)."""
    if id(h) not in _cache:
        _cache[id(h)] = install(h, model, h.fresh_memory())
    return bytearray(_cache[id(h)])


def initialised(h, model):
    """Installed, then INT 65h function 0 as sound_detect does (adlib_driver_init)."""
    key = ('init', id(h))
    if key not in _cache:
        h.check('adlib_driver_init', installed(h, model))
        _cache[key] = after(h)
    return bytearray(_cache[key])


def random_driver(h, model, rng):
    """The initialised driver with random voices, slots and modes (relative-volume denominators
    stay non-zero: a zero one is a divide error on both sides, not compared)."""
    m = initialised(h, model)
    for v in range(11):
        dput8(m, VOICE_NOTE + v, rng.choice([rng.randrange(96), rng.randrange(256)]))
        dput8(m, VOICE_KEY_ON + v, rng.choice([0, 0x20]))
        dput16(m, HALF_TONE + 2 * v, rng.choice([0, 0, rng.randrange(-12, 13), rng.randrange(0x10000)]))
        dput16(m, FNUM_PTR + 2 * v, rng.choice([FNUM_TBL, FNUM_TBL + 0x18 * rng.randrange(25), rng.randrange(0x10000)]))
    dput8(m, PERC_BITS, rng.randrange(256))
    dput8(m, PERCUSSION, rng.choice([0, 1, 1, rng.randrange(256)]))
    for i in range(3):
        dput8(m, AM_DEPTH + i, rng.choice([0, 1, rng.randrange(256)]))
    for i in range(18 * 14):
        dput8(m, PARAM_SLOT + i, rng.choice([rng.randrange(16), rng.randrange(256)]))
    for i in range(18):
        dput8(m, REL_VOLUME + 2 * i, rng.randrange(256))
        dput8(m, REL_VOLUME + 2 * i + 1, rng.randrange(1, 256))
    dput16(m, MODE_WAVE_SEL, rng.choice([0, 1, rng.randrange(0x10000)]))
    dput16(m, MODE, rng.choice([0, 1, rng.randrange(0x10000)]))
    return m


# ---------------------------------------------------------------- loading and installation

def test_adlib_load(h, rng, scale):
    """The port's loader (adlib_load) against the Python mirror; a wrong file is refused."""
    import ctypes
    dll = h.port.dll
    dll.gb_adlib_load.argtypes = [ctypes.c_char_p]
    dll.gb_adlib_error.restype = ctypes.c_char_p
    m = h.fresh_memory()
    h.port.set_memory(m)
    if not dll.gb_adlib_load(str(COM).encode()):
        raise Mismatch('adlib_load: ' + dll.gb_adlib_error().decode())
    want = load_com(bytearray(m))
    got = h.port.memory()
    if got != bytes(want):
        diff = [i for i in range(len(want)) if want[i] != got[i]]
        raise Mismatch('adlib_load: %d bytes differ from the mirror, first at %06X' % (len(diff), diff[0]))
    if dll.gb_adlib_load(str(GAME_DIR / 'GB.EXE').encode()):
        raise Mismatch('adlib_load accepted GB.EXE')
    return 2


def test_adlib_install(h, rng, scale):
    """ADLIB.COM's installer (start-up, main without options, INT 27h) against adlib_install, on
    fresh memory and on the memory of GB.EXE's start-up (config, keyboard, heap)."""
    with adlib(h) as model:
        m = installed(h, model)
        if struct.unpack_from('<HH', m, 0x65 * 4) != (0x02EF, PSP) or struct.unpack_from('<HH', m, 8 * 4) != (0x0621, PSP):
            raise Mismatch('adlib_install: the INT 65h / INT 8 vectors are not the driver\'s')
        install(h, model, main_state(h))
    return 2


# ---------------------------------------------------------------- the driver's routines

def test_adlib_arithmetic(h, rng, scale):
    """ldiv / lmul (AX:BX with CX:DX), CalcPremFNum, SetFNum, InitFNums, and the event slots."""
    n = 0
    edge = [0, 1, 2, 3, 0x7FFF, 0x8000, 0xFFFF, 0xFFFE, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x80000001,
            100, 60, 0x1B503, 0xCB78]
    with adlib(h) as model:
        m = installed(h, model)
        for k in range(600 * scale):
            a = rng.choice(edge + [rng.randrange(1 << 32), rng.randrange(1 << 16), rng.randrange(1 << 24)])
            b = rng.choice(edge + [rng.randrange(1 << 32), rng.randrange(1, 1 << 16), rng.randrange(1, 256)])
            regs = {'ax': a >> 16, 'bx': a & 0xFFFF, 'cx': b >> 16, 'dx': b & 0xFFFF}
            h.check('adl_ldiv', m, regs=regs, outputs=['ax', 'bx', 'cx', 'dx'], label='%08X / %08X' % (a, b))
            h.check('adl_lmul', m, regs=regs, outputs=['ax', 'bx'], label='%08X * %08X' % (a, b))
            n += 2
        for k in range(300 * scale):
            num = rng.choice([0, 4, 96, rng.randrange(0x10000), rng.randrange(-200, 200) & 0xFFFF])
            den = rng.choice([100, 1, 0, rng.randrange(0x10000), rng.randrange(1, 300)])
            h.check('adl_calc_prem_fnum', m, stack_args=[num, den], outputs=['ax', 'bx'], label='%04X/%04X' % (num, den))
            h.check('adl_set_fnum', m, stack_args=[rng.choice([FNUM_TBL, 0x1000 + rng.randrange(0x800)]), num, den],
                    label='%04X/%04X' % (num, den))
            n += 2
        h.check('adl_init_fnums', m)
        h.check('adl_init_fnum_ptrs', m)
        n += 2
        for t in list(range(12)) + [0xFFFF, 0x8000, rng.randrange(0x10000)]:
            for v in (0, 1, 5, 10, 11, rng.randrange(0x10000)):
                h.check('adl_event_slot_a', m, stack_args=[t, v], outputs=['ax'], label='type %X voice %X' % (t, v))
                h.check('adl_event_slot_b', m, stack_args=[t, v], outputs=['ax'], label='type %X voice %X' % (t, v))
                n += 2
    return n


def test_adlib_setup(h, rng, scale):
    """The installation and initialisation routines on random driver states."""
    n = 0
    with adlib(h) as model:
        m = installed(h, model)
        h.check('adl_driver_installed', m, outputs=['ax'])
        h.check('adl_driver_installed', h.fresh_memory(), outputs=['ax'])
        h.check('adl_install_int65', m, outputs=['ax'])
        n += 3
        for k in range(20 * scale):
            m = random_driver(h, model, rng)
            dput16(m, 0x000A, rng.choice([0x0F20, rng.randrange(0x10000)]))
            if k % 2:
                struct.pack_into('<HH', m, 0x65 * 4, rng.randrange(0x10000), rng.choice([0, PSP, rng.randrange(0xA000)]))
            h.check('adl_install_int65', m, outputs=['ax'])
            h.check('adl_driver_installed', m, outputs=['ax'])
            h.check('adl_pit_set_ch0', m, regs={'ax': rng.choice([0, 0x3400, rng.randrange(0x10000)])})
            h.check('adl_clock_install', m)
            size = rng.choice([0x0FFA, 0x3E8, rng.randrange(0x10, 0x2000)])
            h.check('adl_driver_setup', m, stack_args=[0x0F20, size, 0x0388, rng.randrange(0x10000)])
            h.check('adl_sound_cold_init', m, stack_args=[0x0388, rng.randrange(0x10000)])
            h.check('adl_sound_warm_init', m)
            h.check('adl_fn_init', m)
            dput16(m, 0x06D6, rng.choice([0x199, 0, 1, 2, rng.randrange(0x200), 0x8001]))
            h.check('adl_init_event_pool', m)
            for name in ('adl_clear_voice_flags', 'adl_init_voice_state', 'adl_clear_event_ptrs',
                         'adl_init_event_queue', 'adl_init_slot_volume', 'adl_init_slot_params',
                         'adl_snd_s_note_sel', 'adl_snd_s_am_vib_rhythm'):
                h.check(name, m)
            dput16(m, 0x06D8, rng.choice([0xF0, rng.randrange(0x10000)]))
            h.check('adl_set_tempo', m, stack_args=[rng.choice([0x5A, 0, 0x12, 0x13, rng.randrange(0x10000)])])
            h.check('adl_set_mode', m, stack_args=[rng.choice([0, 1, rng.randrange(0x10000)])])
            h.check('adl_set_fn0a_value', m, stack_args=[rng.randrange(0x10000)])
            h.check('adl_set_perc_mode', m, stack_args=[rng.choice([0, 1, rng.randrange(0x10000)])])
            h.check('adl_set_wave_sel', m, stack_args=[rng.choice([0, 1, rng.randrange(0x10000)])])
            h.check('adl_set_pitch_range', m, stack_args=[rng.choice([0, 1, 12, 13, rng.randrange(0x10000)])])
            h.check('adl_set_gparam', m, stack_args=[rng.choice([AM_DEPTH, rng.randrange(0x2000)]), DRV])
            h.check('adl_set_gparam', m, stack_args=[SCRATCH + rng.randrange(0x100), DGROUP])
            h.check('adl_inp', m, stack_args=[0x388], outputs=['ax'])
            h.check('adl_seq_tick', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000)], outputs=['ax'])
            n += 24
    return n


def div0_note(name, before):
    k = DIV0['cases'] - before
    if k:
        print('    (%s: %d cases ended in a divide error on both sides; nothing else compared)' % (name, k))


def test_adlib_slots(h, rng, scale):
    """The slot parameter routines and their register writes, on random slot parameters."""
    n, before = 0, DIV0['cases']
    with adlib(h) as model:
        for k in range(40 * scale):
            m = random_driver(h, model, rng)
            m[DS_BASE + SCRATCH:DS_BASE + SCRATCH + 0x80] = rng.randbytes(0x80)
            slot = rng.choice([rng.randrange(18), rng.randrange(18), rng.randrange(0x10000)])
            for name in ('adl_snd_s_ksl_level', 'adl_snd_s_feed_fm', 'adl_snd_s_att_decay', 'adl_snd_s_sus_release',
                         'adl_snd_s_avek', 'adl_snd_wave_select', 'adl_snd_set_all_prm'):
                check(h, name, m, stack_args=[slot], label='slot %X' % slot)
            for prm in list(range(19)) + [rng.randrange(0x10000)]:
                check(h, 'adl_snd_set_prm', m, stack_args=[rng.randrange(18), prm], label='prm %X' % prm)
                check(h, 'adl_set_slot_prm', m, stack_args=[rng.randrange(18), prm, rng.randrange(0x10000)],
                        label='prm %X' % prm)
            check(h, 'adl_set_slot_param', m, stack_args=[rng.randrange(18), SCRATCH + rng.randrange(0x40), DGROUP,
                                                         rng.randrange(0x10000)])
            check(h, 'adl_snd_output', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000)])
            n += 7 + 40 + 2
    div0_note('test_adlib_slots', before)
    return n


def test_adlib_notes(h, rng, scale):
    """NoteOn, NoteOff, SetFreq, SoundChut and SetVoiceTimbre on random voices and modes."""
    n, before = 0, DIV0['cases']
    with adlib(h) as model:
        for k in range(120 * scale):
            m = random_driver(h, model, rng)
            m[DS_BASE + SCRATCH:DS_BASE + SCRATCH + 0x80] = rng.randbytes(0x80)
            voice = rng.choice([rng.randrange(9), rng.randrange(11), rng.randrange(6, 12), 0xFFFF, 0x8000,
                                rng.randrange(0x10000)])
            pitch = rng.choice([rng.randrange(0x3C - 0x30, 0x60), rng.randrange(0x100), 0xFFD0, 0xFFCF, 0x7FD0, 0x7FCF,
                                0x8000, rng.randrange(0x10000)])
            label = 'voice %X pitch %X perc %d' % (voice, pitch, m[lin(DRV, PERCUSSION)])
            check(h, 'adl_note_on', m, stack_args=[voice, pitch], label=label)
            check(h, 'adl_note_off', m, stack_args=[voice], label=label)
            check(h, 'adl_set_freq', m, stack_args=[voice, pitch, rng.choice([0, 0x20, rng.randrange(0x10000)])],
                    label=label)
            check(h, 'adl_sound_chut', m, stack_args=[voice], label=label)
            params = rng.choice([(rng.choice(GB_TIMBRES), DGROUP), (SCRATCH + rng.randrange(0x40), DGROUP),
                                 (rng.randrange(0x10000), rng.randrange(0x10000))])
            check(h, 'adl_set_voice_timbre', m, stack_args=[voice, params[0], params[1]], label=label)
            n += 5
    div0_note('test_adlib_notes', before)
    return n


# ---------------------------------------------------------------- INT 65h, the timer, GB.EXE's back end

def test_adlib_int65(h, rng, scale):
    """The INT 65h handler (SI, ES:BX) and GB.EXE's calls of it (1af5), which go through the vector."""
    n, before = 0, DIV0['cases']
    with adlib(h) as model:
        m = installed(h, model)
        h.check('adlib_driver_init', m)
        h.check('adlib_driver_present', m, outputs=['ax'])
        n += 2
        for k in range(60 * scale):
            m = random_driver(h, model, rng)
            ch = rng.choice([rng.randrange(9), rng.randrange(16), rng.randrange(0x10000)])
            note = rng.choice([rng.randrange(0x18, 0x80), rng.randrange(256), rng.randrange(0x10000)])
            check(h, 'adlib_note_on', m, stack_args=[ch, note, rng.randrange(0x80)], label='ch %X note %X' % (ch, note))
            check(h, 'adlib_note_off', m, stack_args=[ch], label='ch %X' % ch)
            m[DS_BASE + 0xE59A:DS_BASE + 0xE59A + 0x80] = bytes(rng.randrange(16) for _ in range(0x80))
            put16(m, 0xE596, rng.choice([0, rng.randrange(0x10000)]))
            put16(m, 0xE598, rng.choice([DGROUP, 0x4000, rng.randrange(0x10000)]))
            prog, ah = rng.randrange(0x80), rng.choice([0, 0, rng.randrange(256)])
            check(h, 'adlib_program', m, regs={'ax': ah << 8 | ch & 0xFF}, stack_args=[ch, prog],
                    label='ch %X program %X AH %02X' % (ch, prog, ah))
            check(h, 'adlib_call', m, stack_args=[rng.choice([4, 5, 8, ch]), rng.choice(GB_TIMBRES), DGROUP])
            # the handler itself: SI (bit 15 wraps), ES:BX = arguments in DGROUP
            args = [rng.choice([ch, rng.randrange(11)]), note, DGROUP]
            for i, w in enumerate(args):
                put16(m, SCRATCH + 2 * i, w)
            si = rng.choice([0x13, 0x14, 0x15, 0, 0x8013, 0x8014, 0x8015])
            if si & 0x7FFF == 0x15:
                put16(m, SCRATCH + 2, rng.choice(GB_TIMBRES))
            check(h, 'adl_int65_handler', m, regs={'si': si, 'es': DGROUP, 'bx': SCRATCH}, label='SI %X' % si)
            n += 5
    div0_note('test_adlib_int65', before)
    return n


def test_adlib_clock(h, rng, scale):
    """The driver's INT 8 handler: the BIOS chain, the EOI path, the count, the idle sequencer; and
    through the game's timer handlers, which chain to it."""
    n = 0
    with adlib(h) as model:
        base = initialised(h, model)
        for k in range(150 * scale):
            m = bytearray(base)
            cput16(m, C_CHAIN, rng.choice([1, 1, 0]))
            cput16(m, C_DIVISOR, rng.choice([0, 0x3400, rng.randrange(0x10000)]))
            cput16(m, C_ACCUM, rng.randrange(0x10000))
            cput16(m, C_COUNT, rng.choice([0xFFFF, rng.randrange(0x10000)]))
            cput16(m, C_COUNT + 2, rng.choice([0x7FFF, 0xFFFF, rng.randrange(0x10000)]))
            cput16(m, C_COUNTDOWN, rng.choice([1, 1, 2, 0, rng.randrange(0x10000)]))
            m[lin(PSP, C_BUSY)] = rng.choice([0, 0, 1])
            dput16(m, SEQ_ACTIVE, rng.choice([0, 0, 1]))
            dput16(m, SEQ_PLAYING, 0 if m[lin(DRV, SEQ_ACTIVE)] else rng.choice([0, 1]))
            struct.pack_into('<I', m, 0x46C, rng.choice([0x1800AF, rng.randrange(0x1800B0)]))
            h.check('adl_clock_isr', m, label='case %d' % k)
            m = after(h)
            h.check('adl_clock_isr', m, label='case %d again' % k)
            n += 2
        # INT 8 through the menu timer, which chains to the driver every fifth interrupt
        m = bytearray(base)
        h.check('timer_install', m)
        m = after(h)
        cput16(m, C_COUNTDOWN, 3)
        for t in range(40 * scale):
            h.check('menu_timer_isr', m, label='tick %d' % t)
            m = after(h)
            n += 1
        h.check('timer_restore', m)
        n += 2
        # the effects timer of the missions (sfx_install saves the driver's vector; every 13th
        # interrupt chains to it)
        m = bytearray(base)
        h.check('engine_sound_on', m)
        m = after(h)
        cput16(m, C_COUNTDOWN, 2)
        for t in range(60 * scale):
            h.check('sfx_timer_isr', m, label='effects tick %d' % t)
            m = after(h)
            n += 1
        h.check('engine_sound_off', m)
        n += 2
    return n


# ---------------------------------------------------------------- the title music on the AdLib

def title_music_state(h, model):
    """main's memory before the title (config, keyboard, heap) with ADLIB.COM installed."""
    key = ('title', id(h))
    if key not in _cache:
        _cache[key] = install(h, model, main_state(h))
    return bytearray(_cache[key])


def test_adlib_detect(h, rng, scale):
    """sound_detect finds the driver (mask bit 2) and initialises it; ADLIB.BIN is loaded."""
    n = 0
    with adlib(h) as model:
        for mask in (0x0F, 0x07, 0x04, 0x05, 0x03, 0x0C):
            m = installed(h, model)
            h.check('sound_detect', m, stack_args=[mask, 0, 0x5000, 0, 0x6000], label='mask %X' % mask)
            n += 1
    return n


def test_adlib_music(h, rng, scale):
    """music_start with the AdLib (detection, instruments, VALK12.MUS), then the menu timer's
    interrupts (music_tick, the speaker driver's tick and the chain to the driver's INT 8 handler)
    for 4000 ticks (45 s of music: the song loops at tick 3804), then music_stop (the notes off,
    the timer back to the driver's)."""
    n = 0
    with adlib(h) as model:
        m = title_music_state(h, model)
        h.check('music_start', m)
        m = after(h)
        n += 1
        if struct.unpack_from('<H', m, DS_BASE + 0xF2A0)[0] != 4:
            raise Mismatch('music_start did not choose the AdLib')
        for t in range(4000 * scale):
            h.check('menu_timer_isr', m, label='tick %d' % t)
            m = after(h)
            n += 1
        h.check('music_stop', m)
        n += 1
    return n


def test_adlib_music_random(h, rng, scale):
    """Random .MUS streams (test_sound.random_stream: notes, note offs, programs, loop marks, running
    status, both delta forms) on the AdLib back end: 9 voices, ADLIB.BIN's program map and timbres."""
    n = 0
    with adlib(h) as model:
        for k in range(12 * scale):
            m = installed(h, model)
            h.check('sound_detect', m, stack_args=[7, 0, 0x5000, 0, 0x6000])
            m = after(h)
            stream = random_stream(rng, rng.randrange(5, 60))
            m[lin(MUS_SEG, 0):lin(MUS_SEG, 0) + len(stream)] = stream
            h.check('music_play', m, stack_args=[0, MUS_SEG, rng.randrange(2)])
            m = after(h)
            n += 2
            for t in range(150):
                h.check('music_tick', m, label='stream %d tick %d' % (k, t))
                m = after(h)
                n += 1
            h.check('music_silence', m)
            n += 1
    return n


def test_adlib_title(h, rng, scale):
    """title_menu's first run (the intro with the music started and stopped, then the menu) on the
    machine with ADLIB.COM installed (test_title's key script and poll points)."""
    n = 0
    with adlib(h) as model:
        for keys, first_run in (([0] * 40 + [0x20] + [0] * 400 + [0x0D], 1), (spaced([0x96, 0, 0, 0x0D]), 0)):
            m = title_music_state(h, model)
            put8(m, 0xF398, first_run)
            h.check('title_menu', m, outputs=['ax'], tick=(POLLS, 'both', keys), max_insns=2_000_000_000,
                    label='first run %d' % first_run)
            n += 1
    return n


TESTS = [test_adlib_load, test_adlib_install, test_adlib_arithmetic, test_adlib_setup, test_adlib_slots,
         test_adlib_notes, test_adlib_int65, test_adlib_clock, test_adlib_detect, test_adlib_music,
         test_adlib_music_random, test_adlib_title]
