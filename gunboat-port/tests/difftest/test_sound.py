"""Sound (sound.md): the effects driver (12ed), the music sequencer and its PC speaker back end (1ace,
1af5, 1b37) and the device detection (1ace, 1af5, 1b5f), on the modelled machine of soundmodel.py (a PC
speaker and nothing else). Every check compares all memory, the listed registers, the speaker events
(host_speaker) and the PIT channel 0 divisors (host_set_timer)."""
import contextlib
import struct

from gbdiff import DS_BASE, GAME_DIR, lin, put8, put16, randomize
from soundmodel import SoundModel

SCRATCH = 0xF000                  # DS scratch (BSS below the stack) for programs, scripts, names
POKE_AREA = 0xF3C0                # DS bytes that the scripts' op 7 may write
MUS_SEG = 0x4000                  # the music buffer (in the DOS heap, unallocated)
TANDY_ROM = 0xFC000               # F000:C000
SFX_STATE = (0xDA48, 0xDB1E)      # the effects driver's variables
SFX_PROGRAMS = 0xDB1E
VOICES = 0xE684
REC = 0x16


@contextlib.contextmanager
def sound(h):
    """The sound hardware on the Unicorn side and the speaker / timer comparison, for one test."""
    model = SoundModel(h)
    model.install()
    h.extensions.append(model)
    try:
        yield model
    finally:
        model.uninstall()


def run_original(h, m, name, regs=None, stack_args=()):
    """Memory after the original runs `name` (a set-up step)."""
    h.orig.set_memory(m)
    file_seg, off, far = h.sym.func(name)
    h.orig.call(file_seg, off, far, regs or {}, stack_args)
    return bytearray(h.orig.memory())


def after(h):
    return bytearray(h.orig.memory())


def get16(m, ds_off):
    return struct.unpack_from('<H', m, DS_BASE + ds_off)[0]


# ---------------------------------------------------------------- effects (12ed)

def effects_on(h, model, rng, tandy=0):
    """Fresh memory with the effects timer installed by engine_sound_on (checked)."""
    m = h.fresh_memory()
    put8(m, 0xDA47, tandy)
    model.set_state(rng.randrange(0x10000), rng.choice([0, 3]))
    h.check('engine_sound_on', m, label='tandy %d' % tandy)
    return after(h)


def test_sfx_install(h, rng, scale):
    n = 0
    with sound(h) as model:
        for tandy in (0, 1, 2):
            for on in (0, 1, 2):
                for gate in (0, 1, 3):
                    m = h.fresh_memory()
                    put8(m, 0xDA47, tandy)
                    put8(m, 0xDA46, on)
                    put8(m, 0xDA8F, rng.choice([8, 0x1C, rng.randrange(256)]))
                    randomize(m, DS_BASE + 0xDA54, 4, rng)
                    for name in ('engine_sound_on', 'engine_sound_off', 'sfx_install', 'sfx_remove',
                                 'sfx_speaker_init', 'sfx_silence'):
                        model.set_state(rng.randrange(0x10000), gate)
                        h.check(name, m, label='tandy %d on %d gate %d' % (tandy, on, gate))
                        n += 1
            m = effects_on(h, model, rng, tandy)       # on, off, on again
            h.check('engine_sound_off', m)
            h.check('engine_sound_on', after(h))
            n += 3
    return n


def test_sfx_play(h, rng, scale):
    n = 0
    with sound(h):
        for sid in list(range(16)) + [rng.randrange(0x10000) for _ in range(8)]:
            for on in (0, 1, 2):
                for tandy in (0, 1):
                    m = h.fresh_memory()
                    put8(m, 0xDA46, on)
                    put8(m, 0xDA47, tandy)
                    h.check('sfx_play', m, regs={'ax': sid}, label='id %X' % sid)
                    h.check('sfx_play_far', m, stack_args=[sid], label='id %X' % sid)
                    n += 2
    return n


def engine_update(h, m, rng):
    """The engine note (0919:3cc9, not ported yet) on the original: new throttles, effect 6."""
    put8(m, 0xB816, rng.randrange(256))
    put8(m, 0xB817, rng.randrange(256))
    return run_original(h, m, 'engine_sound_update')


def test_sfx_effects(h, rng, scale):
    """Every effect through the timer interrupt, 260 interrupts each, with and without the engine
    note; then a long run with interruptions, mute and engine changes, as the Codex-era test did."""
    n = 0
    with sound(h) as model:
        for effect in range(13):
            for engine in (False, True):
                m = effects_on(h, model, rng)
                if engine:
                    m = engine_update(h, m, rng)
                h.check('sfx_play_far', m, stack_args=[effect])
                m = after(h)
                n += 2
                for t in range(130 * scale):
                    h.check('sfx_timer_isr', m, label='effect %d engine %s interrupt %d' % (effect, engine, t))
                    m = after(h)
                    n += 1
        m = effects_on(h, model, rng)
        for t in range(1500 * scale):
            if t % 11 == 0:
                m = engine_update(h, m, rng)
            if t % 47 == 0:
                h.check('sfx_play_far', m, stack_args=[rng.randrange(13)])
                m = after(h)
                n += 1
            put16(m, 0x007E, 1 if t % 149 >= 137 else 0)
            if t % 97 == 0:
                put16(m, 0xDA52, rng.choice([12, 0xFFFF, 0x8000, 0x7FFF]))
                put16(m, 0x08C0, rng.randrange(0x10000))
                struct.pack_into('<I', m, 0x46C, rng.choice([0x1800AF, rng.randrange(0x1800B0)]))
            h.check('sfx_timer_isr', m, label='scenario interrupt %d' % t)
            m = after(h)
            n += 1
    return n


def terminating_program(rng, length):
    """Random effect commands without the backward loop D8 (which could loop forever), ending in 0Fh."""
    out = bytearray()
    for _ in range(length):
        b = rng.randrange(256)
        if b & 0x0F == 0x0D and b >> 4 == 8:
            b = 0x7D
        out += bytes([b, rng.choice([rng.randrange(256), rng.randrange(8), 0x13, 0x0B])])
    return out + b'\x0f\x00'


def random_sfx_state(h, rng, tandy, programs=True):
    """Random driver state with valid voice counts and terminating programs for every voice."""
    m = h.fresh_memory()
    lo, hi = SFX_STATE
    randomize(m, DS_BASE + lo, hi - lo, rng)
    put8(m, 0xDA46, 1)
    put8(m, 0xDA47, tandy)
    put16(m, 0xDA88, 0xDA70 if tandy else 0xDA58)
    put8(m, 0xDA8A, 1 if tandy else 7)
    put16(m, 0xDA8D, 6 if tandy else 0)
    put16(m, 0xDAE2, rng.randrange(0x6000))          # D1/D2 cannot overflow the division
    put16(m, 0xDADE, rng.randrange(0x6000))
    put16(m, 0xDAE0, rng.randrange(0x6000))
    put16(m, 0xDAFC, rng.choice([0, 0, 0, 1, 2]))
    put16(m, 0xDAF0, rng.choice([0, 1, 2, 2]))
    put16(m, 0x007E, rng.choice([0, 0, 0, 1]))
    put16(m, 0xDB14, rng.choice([6, 4, 0, rng.randrange(0x10000)]))
    put16(m, 0xDB08, rng.choice([0, 1, rng.randrange(0x10000)]))
    for v in range(4):
        put16(m, 0xDAF2 + 2 * v, rng.choice([0, 1, 2, 2, rng.randrange(0x10000)]))
        put16(m, 0xDAE4 + 2 * v, rng.choice([0, 1, 4, 6, rng.randrange(20), rng.randrange(0x10000)]))
    if programs:
        at = SCRATCH
        for p in (0xDAD2, 0xDAD4, 0xDAD6, 0xDAD8, 0xDADA, 0xDADC):
            prog = terminating_program(rng, rng.randrange(1, 12))
            m[DS_BASE + at:DS_BASE + at + len(prog)] = prog
            put16(m, p, at)
            at += len(prog)
        env = bytes(rng.choice([rng.randrange(16), 0xFF]) for _ in range(64))
        m[DS_BASE + 0xF300:DS_BASE + 0xF340] = env
        for v in range(4):
            put16(m, 0xDA9A + 2 * v, 0xF300 + rng.randrange(48))
            put16(m, 0xDA90 + 2 * v, 0xF300 + rng.randrange(48))
    return m


def test_sfx_commands(h, rng, scale):
    """Each helper of the driver on random state; the command loop on terminating programs."""
    n = 0
    with sound(h) as model:
        for _ in range(250 * scale):
            tandy = rng.choice([0, 0, 1])
            di = rng.choice([0, 2, 4, 6]) if tandy else rng.choice([0, 0, 2])
            m = random_sfx_state(h, rng, tandy)
            model.set_state(rng.randrange(0x10000), rng.choice([0, 3, 1]))
            cmd = rng.randrange(256)
            arg = rng.choice([rng.randrange(256), rng.randrange(16), 0])
            put8(m, SCRATCH + 0x200, cmd)
            put8(m, SCRATCH + 0x201, arg)
            single = get16(m, 0xDADC)
            put16(m, 0xDADC, rng.choice([SCRATCH + 0x200, single]))
            ax = rng.choice([cmd >> 4 << 8 | cmd & 15, rng.randrange(0x10000)])
            label = 'tandy %d di %d cmd %02X %02X ax %04X' % (tandy, di, cmd, arg, ax)
            for name, regs in (('sfx_parse_command', {'di': di}),
                               ('sfx_note_on', {'ax': (ax & 0x0F00) | rng.choice([rng.randrange(1, 13), ax & 0xFF]),
                                                'di': di}),
                               ('sfx_note_duration', {'di': di}),
                               ('sfx_control', {'ax': ax, 'di': di}),
                               ('sfx_cmd_legato', {'ax': ax}),
                               ('sfx_cmd_tempo_shift', {'ax': ax}),
                               ('sfx_cmd_tempo', {'ax': ax}),
                               ('sfx_cmd_envelope', {'ax': ax, 'di': di}),
                               ('sfx_cmd_volume', {'ax': ax, 'di': di}),
                               ('sfx_cmd_raw_length', {'ax': ax}),
                               ('sfx_raw_pitch', {'ax': ax, 'di': di}),
                               ('sfx_channel_start', {'di': di}),
                               ('sfx_channel_run', {'di': di}),
                               ('sfx_channel_step', {'di': di}),
                               ('sfx_channel_transfer', {'di': di}),
                               ('sfx_secondary_step', {'di': 0}),
                               ('sfx_tandy_envelope', {}),
                               ('sfx_timer_tick', {})):
                if name in ('sfx_channel_run', 'sfx_channel_step', 'sfx_channel_transfer', 'sfx_secondary_step'):
                    regs_m = random_sfx_state(h, rng, tandy)   # the command loop: terminating programs only
                else:
                    regs_m = m
                h.check(name, regs_m, regs=regs, label=label)
                n += 1
        # sequences of timer steps on random state, as the interrupt runs them
        for _ in range(30 * scale):
            tandy = rng.choice([0, 1])
            m = random_sfx_state(h, rng, tandy)
            for t in range(20):
                h.check('sfx_timer_tick', m, label='tandy %d step %d' % (tandy, t))
                m = after(h)
                n += 1
    return n


def test_sfx_tandy(h, rng, scale):
    """The Tandy branches' memory effects: four voices, envelopes, the noise voice (ports parked)."""
    n = 0
    with sound(h) as model:
        for effect in range(13):
            m = effects_on(h, model, rng, tandy=1)
            prog = rng.choice([b'\x0f\x00', terminating_program(rng, 6), b'\x31\x02\x05\x03\x0f\x00'])
            m[DS_BASE + 0x6E55:DS_BASE + 0x6E55 + len(prog)] = prog
            put16(m, 0xDA90, 0xDC3C)
            h.check('sfx_play_far', m, stack_args=[effect])
            m = after(h)
            n += 2
            for t in range(60 * scale):
                h.check('sfx_timer_tick', m, label='tandy effect %d step %d' % (effect, t))
                m = after(h)
                n += 1
    return n


# ---------------------------------------------------------------- detection (1ace:00e8, 1af5, 1b5f)

def test_detect(h, rng, scale):
    n = 0
    with sound(h) as model:
        for mask in range(16):
            for rom in (0, 0x21):
                m = h.fresh_memory()
                m[TANDY_ROM] = rom
                randomize(m, DS_BASE + VOICES, 3 * REC, rng)
                if rng.randrange(2):
                    struct.pack_into('<HH', m, 0x65 * 4, rng.randrange(0x10000), rng.randrange(0xA000))
                model.set_state(rng.randrange(0x10000), rng.choice([0, 3]))
                h.check('sound_detect', m, stack_args=[mask | rng.choice([0, 0xF0, 0xFF00]), rng.randrange(0x10000),
                                                       rng.randrange(0x10000), rng.randrange(0x10000),
                                                       rng.randrange(0x10000)],
                        label='mask %X rom %02X' % (mask, rom))
                n += 1
        for _ in range(2):
            h.check('mpu_command', h.fresh_memory(), stack_args=[rng.randrange(0x10000)], outputs=['ax'])
            h.check('mpu_reset', h.fresh_memory(), regs={'ax': rng.randrange(0x10000)}, outputs=['ax'])
            n += 2
        for base in list(range(0x200, 0x300, 0x10)) + [0x2F8]:
            m = h.fresh_memory()
            put16(m, 0xE56E, base)
            h.check('cms_detect', m, outputs=['ax'], label='base %X' % base)
            for name, regs, outputs in (('cms_opl_wait', {'ax': rng.randrange(0x10000)}, []),
                                        ('cms_opl_write', {'ax': rng.randrange(0x10000)}, []),
                                        ('cms_opl_delay', {}, []),
                                        ('cms_dsp_read', {'ax': rng.randrange(0x10000)}, ['ax']),
                                        ('cms_dsp_write', {'ax': rng.randrange(0x10000)}, [])):
                h.check(name, m, regs=regs, outputs=outputs, label='base %X' % base)
                n += 1
            n += 1
        # the AdLib driver signature: absent, random vectors, planted (with the word before it)
        sig = b'SOUND-DRIVER-AD-LIB'
        for k in range(40 * scale):
            m = h.fresh_memory()
            off, seg = rng.randrange(0x10000), rng.choice([0, 0x5000, rng.randrange(0xA000)])
            if k % 4:
                at = lin(seg, (off - 0x16) & 0xFFFF)
                word = rng.choice([0, rng.randrange(0x10000)])
                m[at - 2:at] = struct.pack('<H', word)
                good = sig if k % 4 != 3 else sig[:18] + b'?'
                m[at:at + 19] = good
            struct.pack_into('<HH', m, 0x65 * 4, off, seg)
            h.check('adlib_driver_present', m, outputs=['ax'], label='vector %04X:%04X' % (seg, off))
            n += 1
    return n


# ---------------------------------------------------------------- files (1af5:0238, 03bd..03dc)

def put_name(m, name):
    b = name.encode() + b'\0'
    m[DS_BASE + SCRATCH:DS_BASE + SCRATCH + len(b)] = b


def test_music_files(h, rng, scale):
    n = 0
    with sound(h):
        for name in ('VALKPC.MUS', 'valk12.mus', 'VALK3V.MUS', 'NOFILE.MUS', 'ADLIB.BIN'):
            m = h.fresh_memory()
            put_name(m, name)
            h.check('mus_open', m, stack_args=[SCRATCH], outputs=['ax'], label=name)
            n += 1
        for name in ('VALKPC.MUS', 'VALK12.MUS'):
            for count in (2, 0x100, 0x2400, 0, 0xFFFF):
                m = h.fresh_memory()
                h.reset_files()
                fh = h.open_both(name)
                h.check('mus_read', m, stack_args=[fh, rng.randrange(0x10), MUS_SEG, count], outputs=['ax'],
                        keep_files=True, label='%s %X' % (name, count))
                h.check('mus_close', after(h), stack_args=[fh], keep_files=True)
                n += 2
        m = h.fresh_memory()
        h.check('mus_read', m, stack_args=[19, 0, MUS_SEG, 10], outputs=['ax'], label='closed handle')
        h.check('mus_close', m, stack_args=[19], label='closed handle')
        n += 2
        for _ in range(3):
            m = h.fresh_memory()
            h.check('adlib_load_bin', m, stack_args=[rng.randrange(0x100), MUS_SEG], outputs=['ax'])
            n += 1
    return n


# ---------------------------------------------------------------- music (1ace, 1af5:0006, 033f..03b8)

def detected(h, model, rng, rom=0, mask=0x0F):
    """Fresh memory after sound_detect(mask) (music_start: 0Fh), checked. Without bit 3 there is no
    MPU-401 probe (2 x 20000 port reads, slow in Unicorn)."""
    m = h.fresh_memory()
    m[TANDY_ROM] = rom
    model.set_state(rng.randrange(0x10000), 0)
    h.check('sound_detect', m, stack_args=[mask, 0, 0x5000, 0, 0x6000], label='rom %02X mask %X' % (rom, mask))
    return after(h)


def load_mus(m, name):
    """The stream as music_start leaves it: the count word, then that many bytes at MUS_SEG:0000."""
    data = (GAME_DIR / name).read_bytes()
    count = struct.unpack_from('<H', data)[0]
    body = data[2:2 + count]
    m[lin(MUS_SEG, 0):lin(MUS_SEG, 0) + len(body)] = body
    return m


def play_sequence(h, m, ticks, label):
    """The menu timer's music calls: music_tick then speaker_music_tick, every tick."""
    n = 0
    for t in range(ticks):
        h.check('music_tick', m, label='%s tick %d' % (label, t))
        h.check('speaker_music_tick', after(h), label='%s tick %d' % (label, t))
        m = after(h)
        n += 2
    return n, m


def test_music_songs(h, rng, scale):
    """The three songs as the title plays them (the PC speaker song on the speaker, the 3-voice song
    on a Tandy's voices, the 12-voice song filtered to one voice), long enough to loop."""
    n = 0
    with sound(h) as model:
        for name, rom, ticks in (('VALKPC.MUS', 0, 2600), ('VALK3V.MUS', 0x21, 1200), ('VALK12.MUS', 0, 700)):
            m = load_mus(detected(h, model, rng, rom), name)
            h.check('music_play', m, stack_args=[0, MUS_SEG, 1], label=name)
            k, m = play_sequence(h, after(h), ticks * scale, name)
            n += 2 + k
            h.check('music_silence', m, label=name)
            n += 1
    return n


DATA_BYTES = {0x80: 0, 0x90: 2, 0xC0: 1, 0xD0: 1}   # what music_tick consumes per status


def random_stream(rng, length):
    """Random well-formed .MUS events: notes (some with velocity 0 or data bytes >= 80h), note offs,
    programs, loop marks, other statuses, running status, one- and two-byte deltas. It ends with two
    note offs with a delta of 5 and the end FCh (padded): a loop point then always has a non-zero
    delta before the end (else the original loops forever, as the port does)."""
    out = bytearray(b'\x00')                         # the byte music_play skips
    status = None
    for _ in range(length):
        kind = rng.randrange(8)
        ch = rng.choice([0, 0, 1, 2, rng.randrange(16)])
        if kind < 3:
            status = 0x90 | ch
            out += bytes([status, rng.choice([rng.randrange(0x18, 0x80), rng.randrange(256)]),
                          rng.choice([0x64, 0, rng.randrange(256)])])
        elif kind == 3:
            status = 0x80 | ch
            out += bytes([status])
        elif kind == 4:
            status = 0xC0 | ch
            out += bytes([status, rng.randrange(256)])
        elif kind == 5:
            status = 0xD0 | ch
            out += bytes([status, rng.randrange(256)])
        elif kind == 6 and status is not None:       # running status: data bytes below 80h
            k = DATA_BYTES.get(status & 0xF0, 0)
            out += bytes(rng.randrange(0x80) for _ in range(k))
            if k == 0:                               # the delta is the event's only byte
                out += bytes([rng.randrange(0x80)])
                continue
        else:
            status = rng.choice([0xA0, 0xB0, 0xE0, 0xF0]) | ch
            if status == 0xFC:
                status = 0xFD
            out += bytes([status])
        if rng.randrange(4):
            out += bytes([rng.choice([0, 1, 2, rng.randrange(0x80)])])
        else:
            out += bytes([0x80 | rng.randrange(0x80), rng.randrange(256)])
    return out + b'\x80\x05\x80\x05' + b'\xfc' * 16


def test_music_random(h, rng, scale):
    n = 0
    with sound(h) as model:
        for k in range(12 * scale):
            m = detected(h, model, rng, rom=0x21 if k % 3 == 2 else 0, mask=7)
            stream = random_stream(rng, rng.randrange(5, 60))
            m[lin(MUS_SEG, 0):lin(MUS_SEG, 0) + len(stream)] = stream
            h.check('music_play', m, stack_args=[0, MUS_SEG, rng.randrange(2)])
            k2, m = play_sequence(h, after(h), 120, 'stream %d' % k)
            n += 2 + k2
            put8(m, 0xF290, 0)
            h.check('music_resume', m)
            h.check('music_resume', after(h))
            n += 2
        for dev in (0, 1):                           # music_play / silence / resume on random state
            for _ in range(20 * scale):
                m = detected(h, model, rng, rom=0x21 if dev else 0, mask=7)
                randomize(m, DS_BASE + 0xF288, 0x0B, rng)
                put16(m, 0xF2A2, rng.choice([0, 1, rng.randrange(0x10000)]))
                put16(m, 0xF292, rng.choice([0, 1, 2, 3]))
                randomize(m, DS_BASE + 0xE570, 4, rng)
                h.check('music_play', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000),
                                                     rng.randrange(0x10000)])
                h.check('music_silence', m)
                h.check('music_resume', m)
                n += 3
    return n


# ---------------------------------------------------------------- the speaker music driver (1b37)

OP_SIZE = {0: 1, 1: 3, 2: 3, 3: 1, 4: 3, 5: 3, 6: 3, 7: 6}


def gen_script(rng, base):
    """A script at DS:base that always ends a tick: segments of ops ending in op 0; branches go
    forward inside their segment; calls go to subroutines that end in op 3; the script ends with
    op 3 (the voice ends) or jumps back to its start."""
    items = []                                       # (op, nibble, arg or label)

    def ops(k, allow_calls):
        seg = []
        for _ in range(k):
            op = rng.choice([1, 1, 2, 4, 5, 6, 6, 7, 5] + ([5] if allow_calls else []))
            if op in (1, 4, 6):
                seg.append((op, rng.choice([0, 1, 1, 2]), rng.choice([rng.randrange(0x10000), rng.randrange(8),
                                                                        0, 0x8000, 0x7FFF])))
            elif op == 2:
                seg.append((2, rng.randrange(16), rng.choice([0, 1, 2, 3, 0xFFFF])))
            elif op == 5:
                cond = rng.choice([0, 1, 2, 3, 4, 5, 6] + ([7, 7] if allow_calls else []))
                seg.append((5, cond, 'fwd'))
            else:
                seg.append((7, rng.randrange(16), (rng.randrange(256), POKE_AREA + rng.randrange(0x30), None)))
        return seg

    subs = [ops(rng.randrange(4), False) + [(0, 0, None)] * rng.randrange(2) + [(3, rng.randrange(16), None)]
            for _ in range(rng.randrange(1, 3))]
    main = []
    for _ in range(rng.randrange(1, 6)):
        main += ops(rng.randrange(5), True) + [(0, rng.randrange(16), None)]
    main.append((3, 0, None) if rng.randrange(2) else (5, 6, 'start'))
    # layout
    layout, addr = [], base
    for it in main:
        layout.append(('main', it, addr))
        addr += OP_SIZE[it[0]]
    sub_addr = []
    for s in subs:
        sub_addr.append(addr)
        for it in s:
            layout.append(('sub', it, addr))
            addr += OP_SIZE[it[0]]
    code = bytearray()
    for i, (where, (op, nib, arg), at) in enumerate(layout):
        code.append(nib << 4 | op)
        if op in (1, 2, 4, 6):
            code += struct.pack('<H', arg)
        elif op == 5:
            if nib == 7:
                target = rng.choice(sub_addr)
            elif arg == 'start':
                target = base
            else:                                    # forward, up to the segment's op 0
                later = []
                for w, (op2, _n, _a), at2 in layout[i + 1:]:
                    if w != where:
                        break
                    later.append(at2)
                    if op2 == 0 or op2 == 3:
                        break
                target = rng.choice(later) if later else at + 3
            code += struct.pack('<H', target)
        elif op == 7:
            b, off, _ = arg
            code += bytes([b]) + struct.pack('<HH', off, 0x2B73)
    return bytes(code)


def random_voices(h, rng, m, scripts_at):
    """Three voice records with random priorities, counters and registers on generated scripts."""
    at = scripts_at
    for v in range(3):
        rec = DS_BASE + VOICES + REC * v
        script = gen_script(rng, at)
        m[DS_BASE + at:DS_BASE + at + len(script)] = script
        m[rec:rec + REC] = bytes(REC)
        m[rec] = rng.choice([0, 0x80, 0x80, rng.randrange(256)])
        m[rec + 1] = rng.choice([0, 0xC8, 0x64, rng.randrange(256)])
        struct.pack_into('<HH', m, rec + 2, at, 0x2B73)
        struct.pack_into('<H', m, rec + 6, rng.choice([0, 0, 1, 3, 0x8000]))
        struct.pack_into('<HHH', m, rec + 8, rng.choice([0, 12, rng.randrange(0x10000)]),
                         rng.choice([0, 0x1234, rng.randrange(0x10000)]), rng.randrange(0x10000))
        at += len(script)
    put16(m, 0xE6C6, rng.choice([0, 0x1234, rng.randrange(0x10000)]))
    put8(m, 0xE6C8, rng.choice([0, 0, 0, 1]))
    put8(m, 0xE6C9, rng.choice([1, 1, 1, 0]))
    return m


def test_speaker_scripts(h, rng, scale):
    n = 0
    with sound(h) as model:
        for k in range(60 * scale):
            m = random_voices(h, rng, h.fresh_memory(), SCRATCH)
            model.set_state(rng.randrange(0x10000), rng.choice([0, 3]))
            for t in range(40):
                h.check('speaker_music_tick', m, label='scripts %d tick %d' % (k, t))
                m = after(h)
                n += 1
        for _ in range(200 * scale):                 # voice start, reset, note on / off on random records
            m = h.fresh_memory()
            randomize(m, DS_BASE + VOICES, 3 * REC, rng)
            m[TANDY_ROM] = rng.choice([0, 0x21, rng.randrange(256)])
            put16(m, 0xF2A0, rng.choice([0, 1, 1, rng.randrange(0x10000)]))
            model.set_state(rng.randrange(0x10000), rng.choice([0, 1, 3]))
            h.check('speaker_voice_start', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000),
                                                          rng.choice([0, 1, 2, 3, 0xFFFF, rng.randrange(0x10000)])])
            h.check('speaker_voice_play', m, stack_args=[rng.randrange(0x10000)])
            h.check('speaker_music_reset', m)
            ch = rng.choice([0, 1, 2])
            note = rng.choice([rng.randrange(0x80), rng.randrange(0x18), 0xFF00 | rng.randrange(0x80, 0x100)])
            h.check('speaker_note_on', m, stack_args=[ch, note, rng.randrange(0x80)], label='note %04X' % note)
            h.check('speaker_note_off', m, stack_args=[ch])
            h.check('speaker_program', m, stack_args=[ch, rng.randrange(0x80)])
            n += 6
        # the ops alone: AL = the nibble, BX = the table index (BH = 0), ES:SI into random bytes
        for _ in range(300 * scale):
            m = h.fresh_memory()
            randomize(m, DS_BASE + VOICES, 3 * REC, rng)
            randomize(m, DS_BASE + SCRATCH, 0x40, rng)
            di = rng.choice([0, REC, 2 * REC])
            struct.pack_into('<H', m, DS_BASE + VOICES + di + 0x10, rng.choice([0, 2, 4, rng.randrange(8) * 2]))
            struct.pack_into('<HH', m, DS_BASE + SCRATCH + 1, POKE_AREA + rng.randrange(0x30), 0x2B73)
            regs = {'ax': rng.randrange(16) | rng.randrange(256) << 8, 'di': di, 'si': SCRATCH, 'es': 0x2B73}
            for op, name in enumerate(('spk_op_end', 'spk_op_set', 'spk_op_wait', 'spk_op_return', 'spk_op_add',
                                       'spk_op_branch', 'spk_op_compare', 'spk_op_poke')):
                r = dict(regs, bx=op << 1)
                if name == 'spk_op_branch' and rng.randrange(3) == 0:
                    r['ax'] = rng.randrange(256)
                h.check(name, m, regs=r, outputs=['si', 'es'], label='ax %04X di %X' % (r['ax'], di))
                n += 1
    return n


TESTS = [test_sfx_install, test_sfx_play, test_sfx_effects, test_sfx_commands, test_sfx_tandy, test_detect,
         test_music_files, test_music_songs, test_music_random, test_speaker_scripts]
