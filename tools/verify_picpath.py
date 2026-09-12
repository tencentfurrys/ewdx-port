import struct, os

FXCODE = 0x55AA0000
GAME = r'C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]'
DATA = os.path.join(GAME, 'data')
pic_pool = set(os.path.splitext(f)[0] for f in os.listdir(os.path.join(DATA, 'pic')))

def entries(path):
    buf = open(path, 'rb').read()
    ver, num, pt = struct.unpack_from('<3I', buf, 4)
    out = []
    for i in range(num):
        off = 16 + i * 64
        no, do, op, en = struct.unpack_from('<4I', buf, off)
        fl, mo = struct.unpack_from('<2h', buf, off + 16)
        sz = struct.unpack_from('<I', buf, off + 16 + 24)[0]
        end = buf.index(b'\x00', pt + no)
        out.append((buf[pt+no:end].decode(), fl, sz, pt + do, buf))
    return out

def strs(buf, base, size):
    r, p = [], base
    for _ in range(size // 4):
        tag, sz = struct.unpack_from('<2I', buf, p)
        assert tag == FXCODE
        r.append(buf[p+8:p+8+sz].split(b'\x00')[0].decode('shift-jis', 'replace'))
        p += 8 + sz
    return [x for x in r if x]

# 1. map dio pic names (fed to npic/bmpload per map loader L1854-1873)
dio_names = set()
for f in sorted(os.listdir(os.path.join(DATA, 'map'))):
    if not f.endswith('.map'):
        continue
    for (vn, fl, sz, base, buf) in entries(os.path.join(DATA, 'map', f)):
        if fl == 2 and vn in ('m_dio0_w', 'm_dio1_w', 'm_dio2_w'):
            dio_names.update(strs(buf, base, sz))
print('map dio pic refs:', len(dio_names))
miss = sorted(n for n in dio_names if n not in pic_pool)
print('missing bmps:', len(miss), miss[:10])

# 2. mot "pic" vars
mot_pic = set()
for f in sorted(os.listdir(os.path.join(DATA, 'mot'))):
    if not f.endswith('.mot'):
        continue
    for (vn, fl, sz, base, buf) in entries(os.path.join(DATA, 'mot', f)):
        if fl == 2 and vn == 'pic':
            mot_pic.update(strs(buf, base, sz))
print('mot pic-var refs:', len(mot_pic), sorted(mot_pic)[:12])
miss2 = sorted(n for n in mot_pic if n not in pic_pool)
print('missing bmps:', len(miss2), miss2[:10])

# 3. mold t_obj_w orphans: confirm they are internal labels (never used as paths)
#    -> check decompiled script for t_obj_w consumers
txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
import re
uses = sorted(set(re.findall(r't_obj_w[^\n]{0,60}', txt)))[:8]
print('t_obj_w script consumers (sample):')
for u in uses:
    print('   ', u.encode('ascii', 'replace').decode())
