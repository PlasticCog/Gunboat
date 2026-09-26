"""Extract one file from the original DATAA/DATAB archives by its original name.

usage: gbfile.py NAME [OUT]         e.g. gbfile.py DAT1.DAT reverse_engineering/out/files/DAT1.DAT
       gbfile.py --list             every archived name with bank, offset and length
Without OUT the file goes to reverse_engineering/out/files/NAME. Names are matched with the
original two-key filename hash (ORIGINAL_WORLD_FORMAT.md), case-insensitively.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gunboat_formats import ROOT, ORIGINAL_DIR, read_datac, filename_key  # noqa: E402


def extract(name):
    key = filename_key(name.upper().encode('ascii'))
    for r in read_datac():
        if (r.meta0, r.meta1) == key or r.candidate_name.upper() == name.upper():
            with open(os.path.join(ORIGINAL_DIR, r.source_file), 'rb') as fh:
                fh.seek(r.offset)
                return fh.read(r.length)
    raise SystemExit('not in the archive: %s' % name)


def main():
    if sys.argv[1] == '--list':
        for r in read_datac():
            print('%2d  %-14s %s  %6x  %6d' % (r.index, r.candidate_name, r.source_file, r.offset,
                                               r.length))
        return
    name = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'reverse_engineering', 'out',
                                                              'files', name.upper())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = extract(name)
    open(out, 'wb').write(data)
    print('%s: %d bytes -> %s' % (name, len(data), out))


if __name__ == '__main__':
    main()
