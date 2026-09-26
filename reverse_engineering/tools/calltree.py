"""Print the call tree below a GB.EXE function, with names from symbols.csv.

usage: calltree.py ADDR_OR_NAME [max_depth]
Each function is expanded once; later occurrences are marked "(see above)". Per line: address,
name, size, and flags: V = touches VGA ports or calls the graphics library, P = port I/O,
I = software interrupts.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fn import RE, load_names, resolve, label  # noqa: E402


def main():
    j = json.load(open(os.path.join(RE, 'map', 'gb_functions.json')))
    funcs = {f['start']: f for f in j['functions']}
    names = load_names()
    root = resolve(sys.argv[1], funcs, names)
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 99
    seen = set()

    def flags(f):
        s = ''
        if any(0x137e <= int(c[:4], 16) <= 0x15ea for c in f['calls']) or f['ports'] and \
                any('0x3c' in p or 'dx' in p for p in f['ports']):
            s += 'V'
        if f['ports']:
            s += 'P'
        if f['ints']:
            s += 'I'
        return s

    def walk(addr, level):
        f = funcs.get(addr)
        if f is None:
            print('  ' * level + label(addr, names) + '  (not indexed)')
            return
        again = addr in seen
        print('  ' * level + '%s  %d %s%s' % (label(addr, names), f['size'], flags(f),
                                             '  (see above)' if again and f['calls'] else ''))
        if again or level >= depth:
            return
        seen.add(addr)
        for c in f['calls']:
            walk(c, level + 1)

    walk(root, 0)
    print('%d distinct functions' % len(seen))


if __name__ == '__main__':
    main()
