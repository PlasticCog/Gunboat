"""The headquarters and the front end (game_flow.md §4-§6): hq_quiz, the pencil menus, the roster
file, the office, the BCD helpers, and front_end with each of its screens, from the state main
reaches (config_load, kbd_install, mem_alloc_all run by the original; see test_title.main_state).

Poll points: the fades (121b:07eb, 121b:0841) and bios_wait_ticks (15d4:0019). Each gives one tick
of both kinds (DS:08C0 and the BIOS clock) and the next key of the case's list (0 = none); every
front-end input loop polls once per iteration. The quiz reads IN 42h (the PIT channel-2 counter),
set to the same value on both sides.

roster_save writes GBROSTER.DAT: its tests run on temporary copies of the game folder, one per
side, and compare the written files. The real game folder is never written."""
import contextlib
import pathlib
import shutil
import struct
import tempfile

from gbdiff import DS_BASE, GAME_DIR, Mismatch, put8, put16, randomize
from test_sound import sound
from test_title import main_state
from test_video import run_original

POLLS = ['121b:07eb', '121b:0841', '15d4:0019']
BIG = 2_000_000_000

ENTER, SPACE, BACKSPACE, F1, F2, F3, F4 = 0x0D, 0x20, 0x08, 0x81, 0x82, 0x83, 0x84
UP, DOWN, LEFT, RIGHT = 0x92, 0x98, 0x94, 0x96

ROSTER, RECORDS, REC = 0xB54E, 0xB550, 50
RANK, MEDALS, STATE, REGION, MISSION = 0xB50D, 0xB50E, 0x0084, 0xB503, 0xB505


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def spaced(keys, gap=40, lead=40):
    """Keys with `gap` empty ticks after each (menus debounce and animate), after `lead` ticks."""
    out = [0] * lead
    for k in keys:
        out += [k] + [0] * gap
    return out


def text_keys(s):
    return [ord(c) for c in s]


def load_near(h, m, name_ds, dst):
    return run_original(h, m, 'file_load_near', [name_ds, dst])


def load_far(h, m, name_ds, far_ds):
    return run_original(h, m, 'file_load_far', [name_ds, get16(m, far_ds), get16(m, far_ds + 2)])


def hq_state(h):
    """main's state when it calls hq_quiz: DAT6.DAT loaded (title_menu does it), phase 1."""
    m = main_state(h)
    m = load_near(h, m, 0x0722, 0x6E54)                 # "DAT6.DAT"
    put16(m, 0x0082, 1)
    return m


def front_state(h):
    """The memory when front_end calls name_entry the first time: its set-up (the office, the
    folders' pictures, the sprite sheet) and the officer's face, run by the original. Cached."""
    if getattr(h, '_front_state', None) is None:
        m = main_state(h)
        put16(m, 0x0082, 2)
        with sound(h):
            h.set_tick((POLLS, 'both', []))
            hook = h.orig.stub(0x02D2, 0x0BD8, lambda args: h.orig.fail('stop'))
            try:
                run_original(h, m, 'front_end')
                raise Mismatch('front_end returned before name_entry')
            except Mismatch as e:
                if str(e) != 'original: stop':
                    raise
            finally:
                h.orig.uc.hook_del(hook)
                h.set_tick(None)
        h._front_state = bytearray(h.orig.memory())
    return bytearray(h._front_state)


def region_state(h, region, rank=5):
    """front_state with a region's mission file and maps loaded as state 4 loads them."""
    m = front_state(h)
    put16(m, REGION, region)
    put8(m, RANK, rank)
    m = load_near(h, m, 0x0B26 + 9 * region, 0xB84C)
    m = load_far(h, m, 0x0AC6 + 16 * region, 0xF5CC)
    put16(m, 0xEED0, get16(m, 0xB444 + 4 * region))
    m = load_far(h, m, 0x0ACE + 16 * region, 0xF61A)
    put16(m, 0xF100, get16(m, 0xB446 + 4 * region))
    offered = m[DS_BASE + 0x7BB4 + rank]
    if (region == 0 and rank >= 5) or (region == 1 and rank >= 9):
        offered = 8
    put16(m, 0xB501, offered)
    put16(m, MISSION, offered - 1)
    return m


def check_front(h, name, m, keys, stack_args=(), outputs=(), label='', exit_code=None):
    with sound(h):
        h.check(name, m, stack_args=stack_args, outputs=outputs, tick=(POLLS, 'both', keys), max_insns=BIG,
                label=label, exit_code=exit_code)


# ---------------------------------------------------------------- pure helpers

def test_bcd(h, rng, scale):
    m = h.fresh_memory()
    n = 0
    edge = [0, 1, 9, 0x10, 0x99, 0x100, 0x999, 0x1000, 0x9998, 0x9999, 0xA000, 0xFFFF, 0x270F, 0x2710, 9999, 10000]
    values = edge + [rng.randrange(0x10000) for _ in range(150 * scale)]
    for v in values:
        for name in ('bcd_to_bin', 'bin_to_bcd', 'bcd_inc'):
            h.check(name, m, stack_args=[v], outputs=['ax'], label='%04X' % v)
            n += 1
    for _ in range(300 * scale):
        a, b = rng.choice(values), rng.choice(values)
        h.check('bcd_add', m, stack_args=[a, b], outputs=['ax'], label='%04X + %04X' % (a, b))
        n += 1
    for a, b in ((0x9999, 1), (0x5000, 0x5000), (0xFFFF, 0xFFFF), (0, 0)):
        h.check('bcd_add', m, stack_args=[a, b], outputs=['ax'], label='%04X + %04X' % (a, b))
        n += 1
    return n


def test_small_helpers(h, rng, scale):
    m = h.fresh_memory()
    n = 0
    h.check('world_a_base', m, outputs=['ax'])
    for _ in range(20 * scale):
        base, pit = rng.randrange(0x10000), rng.randrange(256)
        h.set_pit2(pit)
        h.check('pit_random', m, stack_args=[base], outputs=['ax'], label='%04X + %02X' % (base, pit))
        n += 1
    for _ in range(50 * scale):
        mm = bytearray(m)
        a, b = 0xF626, 0xF640 - 0x40
        s1 = bytes(rng.choice(b'AB\0') for _ in range(rng.randrange(1, 20)))
        s2 = bytes(rng.choice(b'AB\0') for _ in range(rng.randrange(1, 20)))
        mm[DS_BASE + a:DS_BASE + a + len(s1)] = s1
        mm[DS_BASE + b:DS_BASE + b + len(s2)] = s2
        k = rng.randrange(0, 22)
        h.check('strncmp', mm, stack_args=[a, b, k], outputs=['ax'], label='%r %r %d' % (s1, s2, k))
        h.check('strcpy', mm, stack_args=[b, a], outputs=['ax'], label='%r' % s1)
        n += 2
    return n + 1


# ---------------------------------------------------------------- the office (02d2)

def test_office_face(h, rng, scale):
    n = 0
    for mood in range(8):
        for hidden in (0, 1):
            m = front_state(h)
            put16(m, 0x0AAE, mood)
            put8(m, 0xF27E, hidden)
            h.check('office_face_draw', m, label='mood %d hidden %d' % (mood, hidden))
            n += 1
    return n


def test_office_idle(h, rng, scale):
    n = 0
    base = front_state(h)
    for _ in range(300 * scale):
        m = bytearray(base)
        m[DS_BASE + 0x88:DS_BASE + 0x8C] = bytes(rng.randrange(256) for _ in range(4))
        put16(m, 0x0AB4, rng.choice([0, 0, 1, 2]))
        put16(m, 0x0AB2, rng.choice([0, 0, 0, 1, 8]))
        put16(m, 0x0AB0, rng.randrange(0x10000))
        put16(m, 0x0AAE, rng.randrange(8))
        put8(m, 0xF27E, rng.randrange(2))
        h.check('office_idle', m, label='seed %s' % bytes(m[DS_BASE + 0x88:DS_BASE + 0x8C]).hex())
        n += 1
    return n


def test_office_screens(h, rng, scale):
    n = 0
    base = front_state(h)
    for page in (0, 1):
        m = bytearray(base)
        put16(m, 0x007A, page)
        run = run_original(h, m, 'gfx_set_draw_page', [page])
        h.check('speech_clear', run, label='page %d' % page)
        h.check('folder_present', run, label='page %d' % page)
        n += 2
    for c1, c2 in ((ord('S'), ord('1')), (ord('D'), ord('6')), (ord('A'), ord('8')), (0, 0xFF)):
        h.check('folder_draw', base, stack_args=[c1, c2], label='%02X %02X' % (c1, c2))
        n += 1
    h.check('office_restore', base)
    with sound(h):
        h.check('office_draw', base, tick=(POLLS, 'both', []), max_insns=BIG)
    return n + 2


def test_wait_key_idle(h, rng, scale):
    n = 0
    base = front_state(h)
    for count, keys in ((0, [0, 0, 0, SPACE]), (1, []), (2, []), (5, [0, 0, ENTER]), (0x1E, []), (3, [0, 0, 0, 0, 0x41])):
        with sound(h):
            h.check('wait_key_idle', base, stack_args=[count], outputs=['ax'], tick=(POLLS, 'both', keys),
                    label='n %d keys %s' % (count, keys))
        n += 1
    return n


def test_print_helpers(h, rng, scale):
    n = 0
    base = front_state(h)
    for _ in range(10 * scale):
        m = bytearray(base)
        for i in range(20):
            m[DS_BASE + 0xF626 + i] = rng.randrange(0x20, 0x80)
        cnt = rng.randrange(0, 21)
        h.check('print_chars', m, stack_args=[0xF626, cnt], label='%d' % cnt)
        n += 1
    for table, cells in ((0xB4A8, 0x7EDF), (0xB4C4, 0xB27F)):
        for _ in range(10 * scale):
            m = bytearray(base)
            for off in range(0xB50E, 0xB546, 2):
                put16(m, off, rng.choice([0, 1, 9, 0x10, 0x99, 0x100, 0x1234, 0x9999, rng.randrange(0x10000)]))
            h.check('bcd_stats_print', m, stack_args=[table, cells], label='%04X' % table)
            put16(m, 0x0082, rng.choice([2, 3]))
            put16(m, 0x0074, rng.choice([0, 1]))
            h.check('byte_stats_print', m, stack_args=[table, cells], label='%04X' % table)
            n += 2
    return n


# ---------------------------------------------------------------- menus (020d)

def test_menu_helpers(h, rng, scale):
    n = 0
    base = front_state(h)
    for phase in (1, 2):
        for _ in range(3 * scale):
            m = bytearray(base)
            put16(m, 0x0082, phase)
            x, y = rng.randrange(0x20, 0xE0), rng.randrange(0x30, 0xC0)
            with sound(h):
                h.check('menu_tick_mark', m, stack_args=[x, y, y - 0x18, y + 3], tick=(POLLS, 'both', []),
                        max_insns=BIG, label='phase %d (%d, %d)' % (phase, x, y))
            n += 1
    for index in range(3):
        h.check('menu_cursor_move', base, stack_args=[index, 0x7ED3, 0x94, 0xAF], label='region %d' % index)
        n += 1
    h.check('menu_cursor_draw', base, stack_args=[0x43, 0x43])
    return n + 1


def test_choice_menu(h, rng, scale):
    cases = [
        # (items, deltas, y0, y1, key_fe, count, keys)
        (0x7ED3, 0x7EC1, 0x94, 0xAF, 0, 3, spaced([ENTER])),                      # region menu: the last
        (0x7ED3, 0x7EC1, 0x94, 0xAF, 0, 3, spaced([LEFT, LEFT, LEFT, ENTER])),
        (0x7ED3, 0x7EC1, 0x94, 0xAF, 0, 2, spaced([RIGHT, 0x91, 0x99, 0x95, ENTER])),
        (0x7BED, 0x7EC1, 0x2E, 0x47, F1, 3, spaced([RIGHT, RIGHT, F1])),           # roster menu, F1
        (0x7BED, 0x7EC1, 0x2E, 0x47, F1, 3, spaced([0x41, RIGHT, ENTER])),
        (0x7BF9, 0x7ECA, 0x3E, 0xBF, F1, 13, spaced([DOWN] * 14 + [UP, ENTER])),   # the 13 slots
        (0x7BED, 0x7EC1, 0x2E, 0x47, F1, 3, [ENTER]),                              # Enter at once
        (0x7BED, 0x7EC1, 0x2E, 0x47, F1, 3, [0, RIGHT, ENTER]),                    # within the debounce
        (0x7BED, 0x7EC1, 0x2E, 0x47, F1, 3, [0, 0, 0, RIGHT, F1, 0, 0, F1]),
    ]
    base = front_state(h)
    for items, deltas, y0, y1, fe, count, keys in cases:
        check_front(h, 'choice_menu', base, keys, stack_args=[0, items, deltas, y0, y1, fe, count, 0],
                    outputs=['ax'], label='items %04X keys %s' % (items, bytes(keys).hex()))
    check_front(h, 'choice_menu', base, [], stack_args=[0xFFFF, 0x7ED3, 0x7EC1, 0x94, 0xAF, 0, 3, 0],
                outputs=['ax'], label='timeout -1')
    return len(cases) + 1


# ---------------------------------------------------------------- the roster file (020d)

@contextlib.contextmanager
def game_copies(h, n=2):
    """n temporary copies of the game folder's files (without subfolders); the harness uses the
    first for the original and the second for the port."""
    tmp = pathlib.Path(tempfile.mkdtemp(prefix='gbdiff_'))
    dirs = []
    try:
        for i in range(n):
            d = tmp / ('side%d' % i)
            d.mkdir()
            for f in GAME_DIR.iterdir():
                if f.is_file():
                    shutil.copy2(f, d / f.name)
            dirs.append(d)
        h.set_game_dirs(dirs[0], dirs[-1])
        yield dirs
    finally:
        h.set_game_dirs(GAME_DIR)
        shutil.rmtree(tmp, ignore_errors=True)


def roster_bytes(rng):
    data = bytearray(rng.randrange(256) for _ in range(702))
    check = 0
    for b in data:
        check ^= b
    return data, check ^ 0x5B


def test_roster_file(h, rng, scale):
    n = 0
    base = hq_state(h)
    with game_copies(h) as dirs:
        variants = []
        data, check = roster_bytes(rng)
        variants.append(('valid', bytes(data) + bytes([check])))
        variants.append(('bad check byte', bytes(data) + bytes([check ^ 1])))
        variants.append(('704 bytes', bytes(data) + bytes([check, 0])))
        variants.append(('702 bytes', bytes(data)))
        variants.append(('empty', b''))
        variants.append(('missing', None))
        for label, content in variants:
            for d in dirs:
                p = d / 'GBROSTER.DAT'
                if content is None:
                    p.unlink(missing_ok=True)
                else:
                    p.write_bytes(content)
            h.check('roster_load', base, label=label)
            n += 1
        for i in range(5 * scale):
            m = bytearray(base)
            randomize(m, DS_BASE + ROSTER, 703, rng)
            for d in dirs:
                (d / 'GBROSTER.DAT').unlink(missing_ok=True)
            h.check('roster_save', m, label='random roster %d' % i)
            a, b = ((d / 'GBROSTER.DAT').read_bytes() for d in dirs)
            if a != b:
                raise Mismatch('roster_save: GBROSTER.DAT differs (original %d bytes, port %d bytes)' % (len(a), len(b)))
            if len(a) != 703:
                raise Mismatch('roster_save wrote %d bytes' % len(a))
            h.check('roster_load', m, label='saved roster %d' % i)
            n += 2
    return n


# ---------------------------------------------------------------- the quiz (020d)

def test_hq_quiz(h, rng, scale):
    cases = [
        [ENTER, 0x38, ENTER],                                              # Enter first (ignored), "8"
        text_keys('12.5') + [BACKSPACE, BACKSPACE, BACKSPACE, BACKSPACE, BACKSPACE, 0x37, ENTER],
        text_keys('1234567') + [0x41, SPACE, ENTER],                       # 5 characters at most
        [0x30, F1, ENTER],
    ]
    n = 0
    base = hq_state(h)
    for keys in cases:
        for _ in range(2 * scale):
            m = bytearray(base)
            m[DS_BASE + 0x88:DS_BASE + 0x8C] = bytes(rng.randrange(256) for _ in range(4))
            pit = rng.randrange(256)
            h.set_pit2(pit)
            check_front(h, 'hq_quiz', m, spaced(keys, gap=4), outputs=['ax'],
                        label='pit %02X keys %s' % (pit, bytes(keys).hex()))
            n += 1
    return n


# ---------------------------------------------------------------- the front end (02d2)

def roster_with(m, records):
    """The roster: records = [(name, rank, medals)]."""
    put16(m, ROSTER, len(records))
    for i, (name, rank, medals) in enumerate(records):
        r = RECORDS + REC * i
        m[DS_BASE + r:DS_BASE + r + 17] = name.ljust(17).encode()
        m[DS_BASE + r + 20] = rank
        m[DS_BASE + r + 21] = medals & 0xFF
        m[DS_BASE + r + 22] = medals >> 8
        for k in range(23, 49, 2):
            m[DS_BASE + r + k] = (i * 7 + k) & 0x99


# Longer than any timed wait in the front end (a tick mark, 17 ticks, then wait_key_idle(1Eh) or
# (3Ch)), so that no key of a flow is taken by a wait it was not meant for.
FLOW_GAP = 80


def front_flows():
    """(label, roster or None, keys, exit code) for whole front_end runs."""
    outfit = [ENTER, ENTER, ENTER, ENTER]
    return [
        ('new commander', None,
         text_keys('NEW') + [ENTER, SPACE, ENTER, SPACE, SPACE, ENTER] + outfit + [SPACE], None),
        ('known commander, rank 5, the maps and specs', None,
         text_keys('accolade') + [ENTER, ENTER, SPACE, SPACE, UP, UP, F2, RIGHT, LEFT, F3, F4, RIGHT, RIGHT, LEFT,
                                  F3, ENTER, DOWN, UP, UP, ENTER, F2, F3, F4, RIGHT, F3, DOWN, ENTER, ENTER, ENTER,
                                  SPACE], None),
        ('rank 9, Panama, vacation', [('ACCOLADE', 9, 3), ('SMITH', 2, 0)],
         text_keys('ACCOLADE') + [ENTER, RIGHT, ENTER, SPACE, SPACE, DOWN, ENTER, SPACE], 0),
        ('TJL, personnel files, replace, redo', [('ACCOLADE', 5, 3), ('BAKER', 12, 0x3FF), ('X', 1, 0)],
         [F1, RIGHT, RIGHT, RIGHT, LEFT, F1] + text_keys('TJL') + [ENTER, SPACE, RIGHT, ENTER, DOWN, DOWN, F1,
                                                                   F1, DOWN, ENTER, SPACE, ENTER]
         + [SPACE, SPACE, ENTER] + outfit + [SPACE], None),
        ('roster menu: redo, then add', [('A', 3, 0)] * 13,
         text_keys('Z Z') + [BACKSPACE, BACKSPACE, BACKSPACE, 0x51, ENTER, SPACE, RIGHT, RIGHT, ENTER]
         + text_keys('Q') + [ENTER, SPACE, ENTER, SPACE, SPACE, ENTER] + outfit + [SPACE], None),
    ]


def test_front_end(h, rng, scale):
    n = 0
    for label, records, keys, code in front_flows():
        m = main_state(h)
        put16(m, 0x0082, 2)
        if records:
            roster_with(m, records)
        check_front(h, 'front_end', m, spaced(keys, gap=FLOW_GAP), label=label, exit_code=code)
        n += 1
    return n


def test_front_end_after_mission(h, rng, scale):
    """front_end in state 12 (after a mission): the debrief, the roster file, then a new round."""
    n = 0
    with game_copies(h) as dirs:
        for progress, result, hits, mtype in ((5, 2, 0, 5), (3, 2, 7, 0x0D), (0, 1, 0, 1), (4, 0, 0, 2)):
            m = main_state(h)
            put16(m, 0x0082, 2)
            put16(m, STATE, 12)
            roster_with(m, [('ACCOLADE', 5, 3), ('MILLER', 2, 0)])
            put16(m, 0xB54C, 1)
            put8(m, RANK, 2)
            put8(m, 0xB544, progress)
            put8(m, 0xB545, result)
            put8(m, 0xB546, hits)
            put16(m, 0xB509, mtype)
            put16(m, REGION, mtype >> 3)
            put16(m, MISSION, mtype & 7)
            for i in range(12):
                put16(m, 0xB52A + 2 * i, rng.choice([0, 1, 5, 0x12, 0x99]))
            keys = [SPACE] * 4 + text_keys('MILLER') + [ENTER, SPACE, SPACE, ENTER, ENTER, ENTER, ENTER, ENTER, SPACE]
            check_front(h, 'front_end', m, spaced(keys, gap=FLOW_GAP),
                        label='progress %d result %d hits %d type %d' % (progress, result, hits, mtype))
            a, b = ((d / 'GBROSTER.DAT').read_bytes() for d in dirs)
            if a != b:
                raise Mismatch('front_end: GBROSTER.DAT differs')
            n += 1
    return n


def test_screens(h, rng, scale):
    n = 0
    base = front_state(h)
    for page in range(17):
        for ret in (5, 9):
            m = bytearray(base)
            put8(m, 0xB508, page)
            h.check('spec_sheet_draw', m, stack_args=[ret], label='page %d ret %d' % (page, ret))
            n += 1
    for region in range(3):
        for mission in range(8):
            for chosen in (0, 1):
                m = bytearray(base)
                put16(m, REGION, region)
                put16(m, MISSION, mission)
                put8(m, 0xB50C, chosen)
                h.check('mission_folder_draw', m, label='region %d mission %d chosen %d' % (region, mission, chosen))
                n += 1
    for region in range(3):
        base_r = region_state(h, region)
        for mission, drawn in ((0, 0), (1, 0), (3, 0), (3, 1), (0, 5)):
            for ret in (5, 9):
                m = bytearray(base_r)
                put16(m, MISSION, mission)
                put16(m, 0xEA84, drawn)
                h.check('map_draw', m, stack_args=[ret], label='region %d mission %d drawn %d' % (region, mission, drawn))
                n += 1
    for item in range(4):
        for day in (0, 1):
            m = bytearray(base)
            put8(m, 0xB4DA, item)
            put8(m, 0xB7FC, day)
            for i in range(4):
                put8(m, 0xB804 + i, rng.randrange(3))
            h.check('outfitting_draw', m, outputs=['ax'], label='item %d day %d' % (item, day))
            n += 1
    for slot in range(13):
        m = bytearray(base)
        # medals below 8000h: with bit 15 set, the medal loop's SAR never ends (as in the original)
        roster_with(m, [('C%d' % i, 1 + i % 13, rng.choice([0, 0x3FF, 0x7FFF, rng.randrange(0x8000)]))
                        for i in range(13)])
        h.check('personnel_file_show', m, stack_args=[slot], label='slot %d' % slot)
        n += 1
    return n


def vacation(m, keys):
    """Whether mission_select's keys end with Enter on mission 0 (Rest & Relaxation: quit to DOS)."""
    offered, mission = get16(m, 0xB501), get16(m, MISSION)
    for k in keys:
        if k == ENTER:
            return mission == 0
        if k in (0x91, 0x92, 0x93, 0x96):
            mission = 0 if mission == offered - 1 else mission + 1
        elif k in (0x94, 0x97, 0x98, 0x99):
            mission = offered - 1 if mission == 0 else mission - 1
        elif k in (F2, F4):
            return False
    return False


def test_screen_loops(h, rng, scale):
    n = 0
    base = front_state(h)
    for ret in (0, 1):
        m = bytearray(base)
        roster_with(m, [('A', 1, 0), ('B', 13, 0x7FFF), ('C', 7, 0x155)])
        put16(m, STATE, 2 + ret)
        check_front(h, 'personnel_files', m, spaced([RIGHT, RIGHT, RIGHT, 0x95, LEFT, UP, F1]), stack_args=[ret],
                    label='ret %d' % ret)
        n += 1
    # (with key_delay left non-zero by choice_menu, pbr_specs, assignment_map and mission_select ignore
    # the keys for ever: the soft-lock is kept, but a call that never returns cannot be compared)
    for ret, state in ((5, 7), (9, 10)):
        for keys in ([RIGHT] * 18 + [LEFT] * 3 + [F3], [LEFT, LEFT, F2]):
            m = bytearray(base)
            put16(m, STATE, state)
            check_front(h, 'pbr_specs', m, spaced(keys), stack_args=[ret], label='ret %d keys %s' % (ret, bytes(keys).hex()))
            n += 1
    for region in range(3):
        base_r = region_state(h, region, rank=9)
        for ret, state in ((5, 6), (9, 8)):
            for chosen in (0, 1):
                m = bytearray(base_r)
                put16(m, STATE, state)
                put8(m, 0xB50C, chosen)
                check_front(h, 'assignment_map', m, spaced([RIGHT, RIGHT, 0x95, LEFT] * 3 + [F4]), stack_args=[ret],
                            label='region %d ret %d chosen %d' % (region, ret, chosen))
                n += 1
        for keys in ([RIGHT, RIGHT, F2], [LEFT, F4], [DOWN, ENTER], [ENTER, RIGHT]):
            m = bytearray(base_r)
            put16(m, STATE, 5)
            check_front(h, 'mission_select', m, spaced(keys), label='region %d keys %s' % (region, bytes(keys).hex()),
                        exit_code=0 if vacation(m, keys) else None)
            n += 1
    m = region_state(h, 0)
    put16(m, STATE, 5)
    put16(m, MISSION, 0)
    check_front(h, 'mission_select', m, spaced([ENTER]), label='vacation', exit_code=0)
    for visited in (0, 1):
        for keys in ([ENTER] * 4, [UP, UP, DOWN, DOWN, DOWN, ENTER, UP, ENTER, DOWN, DOWN, ENTER, UP, UP, UP, ENTER],
                     [F2], [DOWN, F4], [0x91, 0x93, 0x97, 0x99, ENTER, ENTER, ENTER, ENTER]):
            m = region_state(h, 1)
            put16(m, STATE, 9)
            put8(m, 0xB4DB, visited)
            put8(m, 0xB4DA, 2)
            check_front(h, 'outfitting', m, spaced(keys, gap=25),
                        label='visited %d keys %s' % (visited, bytes(keys).hex()))
            n += 1
    return n + 1


def test_debrief(h, rng, scale):
    n = 0
    base = front_state(h)
    outcomes = [(p, r, 0) for p in range(7) for r in (0, 1, 2)] + [(5, 2, 6), (3, 3, 9), (4, 2, 5)]
    for progress, result, hits in outcomes:
        for mtype in (rng.randrange(24), 5, 0x16):
            m = bytearray(base)
            roster_with(m, [('ACCOLADE', 5, 3), ('MILLER', 2, 0)])
            put16(m, 0xB54C, 1)
            put8(m, RANK, rng.randrange(1, 14))
            put16(m, MEDALS, rng.choice([0, 1, 0x3FF, rng.randrange(0x400)]))
            put8(m, 0xB544, progress)
            put8(m, 0xB545, result)
            put8(m, 0xB546, hits)
            put16(m, 0xB509, mtype)
            put16(m, REGION, mtype >> 3)
            put16(m, MISSION, mtype & 7)
            for i in range(12):
                put16(m, 0xB52A + 2 * i, rng.choice([0, 1, 5, 0x12, 0x99, 0x9999]))
            for i in range(12):
                put16(m, 0xB512 + 2 * i, rng.choice([0, 0x99, 0x9990, 0x1234]))
            keys = spaced([SPACE] * 4, gap=5, lead=5)
            check_front(h, 'debrief', m, keys, label='progress %d result %d hits %d type %d' % (progress, result, hits, mtype))
            check_front(h, 'roster_update', m, keys, label='type %d' % mtype)
            n += 2
    return n


def test_roster_edit(h, rng, scale):
    n = 0
    base = front_state(h)
    cases = [
        ([('A', 3, 0)], 0, [ENTER]),                                     # ADD
        ([('A', 3, 0)] * 13, 0, [ENTER]),                                # ADD with 13: the last slot
        ([('A', 3, 0), ('B', 4, 0)], 0, [RIGHT, ENTER, DOWN, DOWN, ENTER]),   # REPLACE an empty slot: append
        ([('A', 3, 0), ('B', 4, 0)], 0, [RIGHT, ENTER, DOWN, ENTER]),    # REPLACE slot 1
        ([('A', 3, 0)], 0, [RIGHT, RIGHT, ENTER]),                       # REDO
        ([('A', 3, 0)], 0, [F1]),
        ([('A', 3, 0)], 1, [F1]),                                        # REPLACE mode left set
        ([('A', 3, 0)], 1, [UP, ENTER]),
    ]
    for records, replace, keys in cases:
        m = bytearray(base)
        roster_with(m, records)
        put16(m, STATE, 1)
        put8(m, 0xB50B, replace)
        for i in range(17):
            m[DS_BASE + 0xF626 + i] = ord('NEWNAME          '[i])
        check_front(h, 'roster_edit', m, spaced(keys), label='%d records replace %d keys %s' % (
            len(records), replace, bytes(keys).hex()))
        n += 1
    return n


TESTS = [test_bcd, test_small_helpers, test_office_face, test_office_idle, test_office_screens, test_wait_key_idle,
         test_print_helpers, test_menu_helpers, test_choice_menu, test_roster_file, test_hq_quiz, test_screens,
         test_screen_loops, test_debrief, test_roster_edit, test_front_end, test_front_end_after_mission]
