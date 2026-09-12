import struct, os, glob

FXCODE = 0x55AA0000
GAME = r'C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]'
DATA = os.path.join(GAME, 'data')

def parse_hspv(path):
    buf = open(path, 'rb').read()
    magic = buf[0:4]
    assert magic == b'hspv', path
    ver, num, pt_data = struct.unpack_from('<3I', buf, 4)
    assert ver == 0x1000, path
    assert pt_data == 16 + num * 64, path
    entries = []
    for i in range(num):
        off = 16 + i * 64
        name_off, data_off, opt, enc = struct.unpack_from('<4I', buf, off)
        flag, mode = struct.unpack_from('<2h', buf, off + 16)
        size = struct.unpack_from('<I', buf, off + 16 + 24)[0]
        end = buf.index(b'\x00', pt_data + name_off)
        name = buf[pt_data+name_off:end].decode('ascii', 'replace')
        entries.append((name, flag, size, pt_data + data_off))
    return buf, entries

def str_elems(buf, base, size):
    n = size // 4
    out = []
    p = base
    for _ in range(n):
        tag, sz = struct.unpack_from('<2I', buf, p)
        assert tag == FXCODE
        out.append(buf[p+8:p+8+sz].split(b'\x00')[0].decode('shift-jis', 'replace'))
        p += 8 + sz
    return out

# expected asset subdirs from script init logic (dir_c==0 branch)
SUBS = {'mot': 'mot', 'mold': 'mold', 'map': 'map', 'pic': 'pic', 'se': 'se', 'music': 'music'}
print('=== on-disk asset inventory ===')
disk = {}
total_bytes = 0
for sub in SUBS.values():
    files = sorted(os.listdir(os.path.join(DATA, sub)))
    disk[sub] = set(files)
    sz = sum(os.path.getsize(os.path.join(DATA, sub, f)) for f in files)
    total_bytes += sz
    print('  data/%-6s %4d files  %8d bytes' % (sub, len(files), sz))
print('  TOTAL %d bytes (%.1f MB)' % (total_bytes, total_bytes / 1048576.0))
print('  save.dat:', os.path.getsize(os.path.join(GAME, 'save.dat')), 'bytes')

# collect referenced pic names from hspv STR vars in mot/map/mold files
print()
print('=== cross-check: pic names referenced inside mot/map/mold dumps ===')
ref_pics = set()
ref_files = 0
for sub, ext in (('mot', '.mot'), ('map', '.map'), ('mold', '.mol')):
    # NOTE: os.listdir, not glob -- GAME path contains [ENG-JAP] brackets
    # which glob would misparse as a character class.
    for f in sorted(os.listdir(os.path.join(DATA, sub))):
        if not f.lower().endswith(ext):
            continue
        ref_files += 1
        buf, entries = parse_hspv(os.path.join(DATA, sub, f))
        for (name, flag, size, base) in entries:
            if flag == 2:
                for s in str_elems(buf, base, size):
                    if s:
                        ref_pics.add(s)
print('distinct pic refs:', len(ref_pics), 'from', ref_files, 'dump files')
missing = sorted(p for p in ref_pics if p + '.bmp' not in disk['pic'])
print('missing data/pic/*.bmp:', len(missing))
for m in missing[:20]:
    print('  MISSING:', m)
extra_note = sorted(ref_pics)[:8]
print('sample refs:', extra_note)

# se refs: script builds data\se\<name>.wav ; names come from code, spot check a few known ones exist
print()
print('=== se/music spot files ===')
for d, f in (('se', os.listdir(os.path.join(DATA, 'se'))[:3]), ('music', sorted(os.listdir(os.path.join(DATA, 'music'))))):
    print(' ', d, f)

print()
print('=== bundle verdict ===')
if not missing:
    print('BUNDLE OK: every pic referenced by game data exists on disk; tree mirrors script dir_c==0 layout')
else:
    print('BUNDLE GAP: %d referenced pics absent' % len(missing))
