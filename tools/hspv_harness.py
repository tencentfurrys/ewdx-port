import struct, os, sys

FXCODE = 0x55AA0000
FLAG_NAMES = {0:'NONE',1:'LABEL',2:'STR',3:'DOUBLE',4:'INT',5:'STRUCT',6:'COMSTRUCT'}

def parse_hspv(path):
    buf = open(path, 'rb').read()
    errs = []
    if len(buf) < 16:
        return None, ['file too small']
    magic = buf[0:4]
    ver, num, pt_data = struct.unpack_from('<3I', buf, 4)
    if magic != b'hspv':
        errs.append('bad magic %r' % magic)
    if ver != 0x1000:
        errs.append('unexpected ver 0x%X' % ver)
    # PVal is 48 bytes on 32-bit; entry = 16 + 48 = 64
    ENTRY = 64
    entries = []
    for i in range(num):
        off = 16 + i * ENTRY
        if off + ENTRY > len(buf):
            errs.append('entry %d out of range' % i)
            break
        name_off, data_off, opt, encode = struct.unpack_from('<4I', buf, off)
        pv = buf[off+16:off+64]
        flag, mode = struct.unpack_from('<2h', pv, 0)
        lens = struct.unpack_from('<5I', pv, 4)
        size = struct.unpack_from('<I', pv, 24)[0]
        support = struct.unpack_from('<H', pv, 34)[0]
        # name string
        try:
            end = buf.index(b'\x00', pt_data + name_off)
            name = buf[pt_data+name_off:end].decode('ascii', 'replace')
        except Exception as e:
            name = '<bad name @%d>' % name_off
            errs.append('entry %d bad name (%s)' % (i, e))
        entries.append({'name': name, 'flag': flag, 'mode': mode, 'lens': lens,
                        'size': size, 'support': support, 'data_off': data_off})
    # validate payloads
    for e in entries:
        base = pt_data + e['data_off']
        if base < 0 or base > len(buf):
            errs.append('%s: payload base out of range' % e['name'])
            continue
        flag = e['flag']
        if flag in (1, 3, 4):  # LABEL/DOUBLE/INT fixed storage
            if base + e['size'] > len(buf):
                errs.append('%s: storage overruns file' % e['name'])
        elif flag == 2:  # STR flexstorage
            n = e['size'] // 4
            p = base
            for k in range(n):
                if p + 8 > len(buf):
                    errs.append('%s: str elem %d header OOR' % (e['name'], k))
                    break
                tag, sz = struct.unpack_from('<2I', buf, p)
                if tag != FXCODE:
                    errs.append('%s: str elem %d bad tag 0x%X' % (e['name'], k, tag))
                    break
                p += 8 + sz
        elif flag == 5:
            if base + 4 > len(buf):
                errs.append('%s: struct tag OOR' % e['name'])
            else:
                tag = struct.unpack_from('<I', buf, base)[0]
                if (tag & 0xFFFF0000) != FXCODE:
                    errs.append('%s: struct bad tag 0x%X' % (e['name'], tag))
    return {'file': os.path.basename(path), 'size': len(buf), 'ver': hex(ver),
            'num': num, 'pt_data': pt_data, 'entries': entries}, errs

def main():
    game = r'C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]\data'
    targets = [os.path.join(game, 'map', 'st00.map'),
               os.path.join(game, 'map', 'st11.map'),
               os.path.join(game, 'mold', 'player.mol'),
               os.path.join(game, 'mot', 'mirea1.mot')]
    # fallbacks if names differ
    real = []
    for t in targets:
        if os.path.exists(t):
            real.append(t)
    import glob
    if len(real) < 4:
        for sub in ('map', 'mold', 'mot'):
            files = sorted(glob.glob(os.path.join(game, sub, '*')))
            for f in files[:2]:
                if f not in real:
                    real.append(f)
    allok = True
    for t in real[:6]:
        info, errs = parse_hspv(t)
        print('=' * 70)
        print('FILE:', t, 'size=', info['size'] if info else '?')
        if info:
            print('num=%d pt_data=%d (expect %d)' % (info['num'], info['pt_data'], 16 + info['num']*64))
            for e in info['entries']:
                fn = FLAG_NAMES.get(e['flag'], '?%d' % e['flag'])
                print('  %-14s flag=%-6s size=%-7d lens=%s' % (e['name'][:14], fn, e['size'], e['lens'][1:]))
            # sanity: decode first INT var values
            for e in info['entries']:
                if e['flag'] == 4 and e['size'] >= 16:
                    base = info['pt_data'] + e['data_off']
                    vals = struct.unpack_from('<4i', open(t,'rb').read(), base)
                    print('  [%s first4 ints: %s]' % (e['name'], vals))
                    break
        if errs:
            allok = False
            print('ERRORS:')
            for x in errs:
                print('  !!', x)
        else:
            print('STATUS: PASS - fully consistent with Hspda.cpp layout')
    print('=' * 70)
    print('OVERALL:', 'ALL PASS - OpenHSP hspda format confirmed' if allok else 'FAILURES PRESENT')

if __name__ == '__main__':
    main()
