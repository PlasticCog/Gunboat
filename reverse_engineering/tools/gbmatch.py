"""Match Gunboat functions against the named functions of another indexed Accolade executable.

Adapted from test-drive-3-sdl3/tools/td2match.py (MIT, (c) 2026 Krzysztof Kania). Every function is
disassembled with its immediates and displacements masked; each Gunboat function is scored against
each reference function by the share of 6-instruction windows the two have in common. The best
reference match above the threshold becomes a candidate name, to be confirmed in the specs.

usage: gbmatch.py REF_EXE REF_FUNCTIONS_JSON REF_SYMBOLS_CSV OUT_CSV [GB index base]
  e.g. gbmatch.py reverse_engineering/out/td3/TDIII_unp.exe \
           test-drive-3-sdl3/port/tdiii_functions.json test-drive-3-sdl3/port/symbols.csv \
           reverse_engineering/map/gb_td3_matches.csv
       GB index base defaults to reverse_engineering/map/gb (gbindex.py output).
"""
import csv, json, re, struct, sys
from collections import defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

N = 6
THRESHOLD = 0.5
ref_exe, ref_json, ref_symbols, out_csv = sys.argv[1:5]
base = sys.argv[5] if len(sys.argv) > 5 else 'reverse_engineering/map/gb'
md = Cs(CS_ARCH_X86, CS_MODE_16)
NUM = re.compile(r'0x[0-9a-f]+|\b\d+\b')


def image(path):
    d = open(path, 'rb').read()
    return d[struct.unpack_from('<H', d, 8)[0] * 16:]


def grams(code):
    ops = [mn + ' ' + NUM.sub('#', op) for _, _, mn, op in md.disasm_lite(code, 0)]
    return ops, {tuple(ops[k:k + N]) for k in range(len(ops) - N + 1)}


ref_img = image(ref_exe)
names, owners = {}, {}
for r in csv.DictReader(open(ref_symbols, newline='')):
    if r['kind'] == 'func':
        names[r['address']] = r['name']
        owners[r['address']] = r.get('owner', '')
ref = []
index = defaultdict(set)            # gram -> reference function ids
for f in json.load(open(ref_json))['functions']:
    s = int(f['image'], 16)
    ops, g = grams(ref_img[s:s + f['size']])
    if len(ops) < N + 2:
        continue
    fid = len(ref)
    ref.append((f['start'], names.get(f['start'], ''), owners.get(f['start'], ''), len(ops), g))
    for x in g:
        index[x].add(fid)

j = json.load(open(base + '_functions.json'))
gb_img = image(j['exe'])
rows = []
for f in j['functions']:
    s = int(f['image'], 16)
    ops, g = grams(gb_img[s:s + f['size']])
    if len(ops) < N + 2 or not g:
        continue
    votes = defaultdict(int)
    for x in g:
        for fid in index.get(x, ()):
            votes[fid] += 1
    if not votes:
        continue
    fid, v = max(votes.items(), key=lambda kv: kv[1] / max(len(g), len(ref[kv[0]][4])))
    score = v / max(len(g), len(ref[fid][4]))
    if score >= THRESHOLD:
        rows.append((f['start'], f['image'], ref[fid][1], ref[fid][0], ref[fid][2], '%.2f' % score,
                     len(ops), ref[fid][3]))

with open(out_csv, 'w', newline='') as o:
    w = csv.writer(o, lineterminator='\n')
    w.writerow(['gb_start', 'gb_image', 'ref_name', 'ref_address', 'ref_owner', 'score', 'gb_insns',
                'ref_insns'])
    w.writerows(rows)
print('%d of %d Gunboat functions matched a reference function (score >= %.2f), %d of them named'
      % (len(rows), len(j['functions']), THRESHOLD, sum(1 for r in rows if r[2])))
for r in rows:
    print('  %s  %-28s %-10s %s  %s' % (r[0], r[2] or '-', r[4], r[3], r[5]))
