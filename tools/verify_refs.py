import struct, os

FXCODE = 0x55AA0000
GAME = r'C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]'
DATA = os.path.join(GAME, 'data')

def parse_hspv(path):
    buf = open(path, 'rb').read()
    ver, num, pt_data = struct.unpack_from('<3I', buf, 4)
    assert buf[0:4] == b'hspv' and ver == 0x1000 and pt_data == 16 + num * 64, path
    out = []
    for i in range(num):
        off = 16 + i * 64
        name_off, data_off, op, en = struct.unpack_from('<4I', buf, off)
        flag, mode = struct.unpack_from('<2h', buf, off + 16)
        size = struct.unpack_from('<I', buf, off + 16 + 24)[0]
        end = buf.index(b'\x00', pt_data + name_off)
        out.append((buf[pt_data+name_off:end].decode(), flag, size, pt_data + data_off))
    return buf, out

def str_elems(buf, base, size):
    out = []
    p = base
    for _ in range(size // 4):
        tag, sz = struct.unpack_from('<2I', buf, p)
        assert tag == FXCODE
        out.append(buf[p+8:p+8+sz].split(b'\x00')[0].decode('shift-jis', 'replace'))
        p += 8 + sz
    return out

pools = {}
for sub, ext in (('pic', '.bmp'), ('mold', '.mol'), ('mot', '.mot'), ('se', '.wav'), ('music', '.ogg'), ('map', '.map')):
    pools[sub] = set(os.path.splitext(f)[0] for f in os.listdir(os.path.join(DATA, sub)))

refs = {}  # ref -> set of (file, varname)
for sub, ext in (('mot', '.mot'), ('map', '.map'), ('mold', '.mol')):
    for f in sorted(os.listdir(os.path.join(DATA, sub))):
        if not f.lower().endswith(ext):
            continue
        buf, entries = parse_hspv(os.path.join(DATA, sub, f))
        for (vn, flag, size, base) in entries:
            if flag == 2:
                for s in str_elems(buf, base, size):
                    if s:
                        refs.setdefault(s, set()).add((f, vn))

print('distinct STR refs:', len(refs))
orphans = []
by_pool = {'pic': 0, 'mold': 0, 'mot': 0, 'se': 0, 'music': 0, 'map': 0}
for r, users in sorted(refs.items()):
    hit = [p for p in by_pool if r in pools[p]]
    if hit:
        for p in hit:
            by_pool[p] += 1
    else:
        orphans.append((r, users))
print('resolved by pool:', by_pool)
print('ORPHANS (in no pool):', len(orphans))
for r, users in orphans[:30]:
    sample = sorted(users)[0]
    print('  %-16s e.g. %s:%s' % (r, sample[0], sample[1]))
