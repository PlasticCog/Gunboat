"""DOS files, the archive and the far buffers (platform.md §4-§5, game_flow.md §1)."""
import struct
import sys

from gbdiff import DS_BASE, GAME_DIR, RE, STACK_BOTTOM, load_image, randomize

sys.path.insert(0, str(RE / 'tools'))
from gunboat_formats import read_datac  # noqa: E402

SCRATCH = 0xF000          # DS offset for test strings (in the BSS, below the stack)
ARCHIVE_DIR = 0x027C
HEAP_SEG = 0x5000         # a far buffer in the heap area for far reads


def put_str(m, off, text):
    data = text.encode('latin-1') + b'\0'
    m[DS_BASE + off:DS_BASE + off + len(data)] = data


def archive_names():
    return [r.candidate_name for r in read_datac()]


def with_directory(h, rng=None):
    """Memory after main's first step: DATAC.DAT (the archive directory) at DS:027C."""
    m = h.fresh_memory()
    if rng:
        randomize(m, DS_BASE, STACK_BOTTOM, rng)
        m[DS_BASE + ARCHIVE_DIR:DS_BASE + ARCHIVE_DIR + 1190] = bytes(1190)
        m[DS_BASE + 0x66:DS_BASE + 0x70] = load_image()[DS_BASE + 0x66:DS_BASE + 0x70]  # data_file_name
    data = (GAME_DIR / 'DATAC.DAT').read_bytes()
    m[DS_BASE + ARCHIVE_DIR:DS_BASE + ARCHIVE_DIR + len(data)] = data
    return m


def test_dos_wrappers(h, rng, scale):
    n = 0
    for name in ['DATAC.DAT', 'dataa.dat', 'A:DATAB.DAT', 'GUNBOAT.CFG', 'NOFILE.XYZ', '', 'C:\\GB\\ADLIB.BIN']:
        m = h.fresh_memory()
        put_str(m, SCRATCH, name)
        h.check('dos_open_read', m, stack_args=[SCRATCH], outputs=['ax'], label=name)
        n += 1
    for _ in range(40 * scale):
        name = rng.choice(['DATAA.DAT', 'DATAB.DAT', 'DATAC.DAT', 'VALK12.MUS'])
        size = (GAME_DIR / name).stat().st_size
        m = h.fresh_memory()
        randomize(m, DS_BASE, STACK_BOTTOM, rng)
        fh = h.open_both(name)
        h.check('dos_file_size', m, stack_args=[fh], outputs=['ax'], keep_files=True)
        pos = rng.randrange(size + 100)
        h.check('dos_seek', m, stack_args=[fh, pos & 0xFFFF, pos >> 16], outputs=['ax'], keep_files=True)
        count = rng.choice([0, 1, 7, 256, 4000, 0xFFFF])
        seg, off = rng.choice([(HEAP_SEG, rng.randrange(0x100)), (0x2B73, SCRATCH)])
        if seg == 0x2B73:
            count = min(count, 0x600)
        h.check('dos_read', m, stack_args=[off, seg, count, fh], outputs=['ax'], keep_files=True,
                label='%s at %d, %d bytes' % (name, pos, count))
        h.check('dos_close', m, stack_args=[fh], keep_files=True)
        h.check('dos_read', m, stack_args=[0, HEAP_SEG, 10, fh], outputs=['ax'], keep_files=True,
                label='closed handle')
        n += 5
    return n


def test_name_hash(h, rng, scale):
    names = archive_names() + ['', 'A', 'AB', 'x' * 40, '\xff\x80\x81', 'TITLE1A.LZ']
    names += [''.join(chr(rng.randrange(1, 256)) for _ in range(rng.randrange(1, 30))) for _ in range(200 * scale)]
    for name in names:
        m = h.fresh_memory()
        put_str(m, SCRATCH, name)
        for fn in ('name_hash_h1', 'name_hash_h2'):
            h.check(fn, m, stack_args=[SCRATCH, rng.randrange(0x10000)], outputs=['ax', 'dx'], label=repr(name))
        h.check('name_hash', m, stack_args=[SCRATCH], outputs=['ax', 'dx'], label=repr(name))
    return 3 * len(names)


def test_archive_open(h, rng, scale):
    names = archive_names()
    names += [n.lower() for n in names[:10]] + ['NOTHERE.LZ', 'DATAC.DAT', 'GB.EXE', '']
    for name in names:
        m = with_directory(h, rng)
        put_str(m, SCRATCH, name)
        h.check('archive_open', m, stack_args=[SCRATCH], outputs=['ax'], label=name)
    return len(names)


def test_file_load(h, rng, scale):
    n = 0
    # main's first step: the directory itself, a plain file, into an empty directory
    m = h.fresh_memory()
    put_str(m, SCRATCH, 'DATAC.DAT')
    h.check('file_load_near', m, stack_args=[SCRATCH, ARCHIVE_DIR], label='DATAC.DAT')
    n += 1
    for r in read_datac():
        m = with_directory(h, rng)
        put_str(m, SCRATCH, r.candidate_name)
        if r.length <= 0x6000:
            h.check('file_load_near', m, stack_args=[SCRATCH, 0x1094], label=r.candidate_name)
            n += 1
        h.check('file_load_far', m, stack_args=[SCRATCH, rng.randrange(0x10), HEAP_SEG], label=r.candidate_name)
        n += 1
    m = with_directory(h, rng)
    put_str(m, SCRATCH, 'GUNBOAT.CFG')   # not archived: a plain file
    h.check('file_load_far', m, stack_args=[SCRATCH, 0, HEAP_SEG], label='GUNBOAT.CFG')
    return n + 1


def test_memory(h, rng, scale):
    m = h.fresh_memory()
    h.check('mem_alloc_all', m, label='fresh heap')
    # allocate on the original, then free on both sides
    h.orig.set_memory(m)
    file_seg, off, far = h.sym.func('mem_alloc_all')
    h.orig.call(file_seg, off, far, {})
    allocated = bytearray(h.orig.memory())
    h.check('mem_free_all', allocated, label='after mem_alloc_all')
    h.check('lzw_alloc', h.fresh_memory(), outputs=['ax'])
    h.check('lzw_free', allocated, label='after mem_alloc_all')
    return 4


TESTS = [test_dos_wrappers, test_name_hash, test_archive_open, test_file_load, test_memory]
