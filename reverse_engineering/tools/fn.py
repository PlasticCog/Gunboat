"""Show one GB.EXE function: index facts, Ghidra decompilation and/or disassembly.

usage: fn.py ADDR_OR_NAME [-c] [-d] [-x]
       fn.py -r SSSS:OOOO LEN      disassemble any range (e.g. code between indexed functions)
  ADDR_OR_NAME  SSSS:OOOO, image offset (0x11ac0) or a name from symbols.csv
  -c  decompiled C only        -d  disassembly only        -x  index facts only
  -k  calls with their pushed constant arguments (strings resolved)
  (default: facts + C + disassembly)

Reads reverse_engineering/map/gb_functions.json, symbols.csv, out/decomp/gb_ds.c and
out/GB_unp.exe (run decompile.ps1 first for the C).
"""
import csv, json, os, re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_names():
    with open(os.path.join(RE, 'symbols.csv'), newline='', encoding='utf-8') as fh:
        return {r['address'].lower(): r for r in csv.DictReader(fh)}


def resolve(arg, funcs, names):
    a = arg.lower()
    if re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{4}', a):
        return a
    if re.fullmatch(r'(0x)?[0-9a-f]{5}', a):
        img = int(a, 16)
        for f in funcs.values():
            if int(f['image'], 16) == img:
                return f['start']
    for addr, r in names.items():
        if r['name'].lower() == a:
            return addr
    sys.exit('unknown function: %s' % arg)


def label(addr, names):
    r = names.get(addr)
    return '%s %s' % (addr, r['name']) if r else addr


def decompiled(f, names):
    path = os.path.join(RE, 'out', 'decomp', 'gb_ds.c')
    if not os.path.exists(path):
        return '(no decompilation: run tools/decompile.ps1)'
    text = open(path, encoding='utf-8').read()
    m = re.search(r'// ==== \S+\s+image 0x%s .*?(?=\n// ==== |\Z)' % f['image'], text, re.S)
    return m.group(0).strip() if m else '(not decompiled separately)'


def disassemble(start_addr, length, names, show_bytes=True):
    d = open(os.path.join(RE, 'out', 'GB_unp.exe'), 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    seg, off = (int(x, 16) for x in start_addr.split(':'))
    start = seg * 16 + off
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    out = []
    for ins in md.disasm(img[start:start + length], off):
        note = ''
        if ins.bytes[0] == 0x9A:
            o, s = struct.unpack_from('<HH', ins.bytes, 1)
            note = '  ; ' + label('%04x:%04x' % (s, o), names)
        elif ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
            note = '  ; ' + label('%04x:%04x' % (seg, int(ins.op_str, 16) & 0xFFFF), names)
        raw = '%-14s ' % ins.bytes.hex() if show_bytes else ''
        out.append('%04x:%04x  %s%s %s%s' % (seg, ins.address, raw, ins.mnemonic, ins.op_str, note))
    return '\n'.join(out)


def disassembly(f, names):
    return disassemble(f['start'], f['size'], names)


def call_trace(f, names):
    """Each call with the constants pushed before it (C calling convention), strings resolved."""
    d = open(os.path.join(RE, 'out', 'GB_unp.exe'), 'rb').read()
    img = d[struct.unpack_from('<H', d, 8)[0] * 16:]
    ds = 0x1b730

    def show(v):
        p = ds + v
        if 0 < v < 0xffff and p < len(img):
            m = re.match(rb'[\x20-\x7e]{3,40}', img[p:p + 40])
            if m and (p + len(m.group(0)) >= len(img) or img[p + len(m.group(0))] in (0, 0x80, 0xaa)):
                return '%xh "%s"' % (v, m.group(0).decode('latin-1'))
        return '%xh' % v

    seg, off = (int(x, 16) for x in f['start'].split(':'))
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    pushed, ax, out = [], None, []
    for ins in md.disasm(img[seg * 16 + off:seg * 16 + off + f['size']], off):
        mn, op = ins.mnemonic, ins.op_str
        if mn == 'mov' and op.startswith('ax, 0x') and ',' in op:
            ax = int(op.split(', ')[1], 16)
        elif mn == 'mov' and op.startswith('ax, ') and op[4:].isdigit():
            ax = int(op[4:])
        elif mn == 'sub' and op == 'ax, ax':
            ax = 0
        elif mn == 'push':
            pushed.append(show(ax) if op == 'ax' and ax is not None else
                          show(int(op, 16)) if op.startswith('0x') else op)
        elif mn in ('call', 'lcall'):
            if ins.bytes[0] == 0x9A:
                o, s = struct.unpack_from('<HH', ins.bytes, 1)
                tgt = label('%04x:%04x' % (s, o), names)
            elif op.startswith('0x'):
                tgt = label('%04x:%04x' % (seg, int(op, 16) & 0xFFFF), names)
            else:
                tgt = op
            args = [a for a in reversed(pushed) if a not in ('cs',)]
            out.append('%04x:%04x  %s(%s)' % (seg, ins.address, tgt, ', '.join(args)))
            pushed, ax = [], None
        elif mn in ('add',) and op.startswith('sp'):
            pushed = []
        if mn not in ('mov', 'push', 'sub', 'call', 'lcall', 'add'):
            ax = None if mn not in ('xor',) else ax
    return '\n'.join(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    flags = {a for a in sys.argv[1:] if a.startswith('-')}
    names = load_names()
    if '-r' in flags:
        print(disassemble(args[0].lower(), int(args[1], 0), names, show_bytes=False))
        return
    j = json.load(open(os.path.join(RE, 'map', 'gb_functions.json')))
    funcs = {f['start']: f for f in j['functions']}
    f = funcs[resolve(args[0], funcs, names)]
    everything = not flags
    if '-k' in flags:
        print(call_trace(f, names))
        return
    if everything or '-x' in flags:
        r = names.get(f['start'])
        print('%s  image %s  size %d  %s  %s' % (label(f['start'], names), f['image'], f['size'],
                                                'far' if f['far'] else 'near',
                                                (r or {}).get('notes', '')))
        print('callers:', ', '.join(label(c, names) for c in f['callers']))
        print('calls:  ', ', '.join(label(c, names) for c in f['calls']))
        print('DS reads: ', ' '.join(f['ds_reads']))
        print('DS writes:', ' '.join(f['ds_writes']))
        if f['ints'] or f['ports'] or f['strings']:
            print('ints', f['ints'], 'ports', f['ports'], 'strings', list(f['strings'].values()))
    if everything or '-c' in flags:
        print()
        print(decompiled(f, names))
    if everything or '-d' in flags:
        print()
        print(disassembly(f, names))


if __name__ == '__main__':
    main()
