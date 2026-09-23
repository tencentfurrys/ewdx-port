#!/usr/bin/env python3
# Parse a game .mot (hspv) file per ewdx_hspv.h layout and dump the stomach
# unit's part records: which parts are wobbled (idx19==1), their geometry
# (idx0..23) and the wall part's staging params (idx2..7, 20..22).
import struct, sys, os

FX = 0x55AA0000

def parse(path):
    img = open(path,'rb').read()
    magic, ver, num, pt_data = struct.unpack_from('<IIII', img, 0)
    assert magic == 0x76707368, hex(magic)
    ents = []
    for i in range(num):
        off = 16 + i*64
        name_off, data_off, opt, enc = struct.unpack_from('<IIII', img, off)
        flag, mode = struct.unpack_from('<hh', img, off+16)
        lens = struct.unpack_from('<IIIII', img, off+20)
        size, pt, master = struct.unpack_from('<III', img, off+40)
        support, arraycnt = struct.unpack_from('<hhi', img, off+52)[0:2] if False else struct.unpack_from('<hh', img, off+52)
        ents.append(dict(name=name_off, data=data_off, flag=flag, mode=mode,
                         lens=lens, size=size))
    for e in ents:
        e['name_s'] = img[pt_data+e['name']:img.index(b'\0', pt_data+e['name'])].decode('sjis', 'replace')
    return img, pt_data, ents

def dims(lens):
    n = 1
    for L in lens[1:]:
        if L: n *= L
    return n

def main(path, varname):
    img, pt_data, ents = parse(path)
    print(f'{os.path.basename(path)}: {len(ents)} vars')
    for e in ents:
        print(f"  {e['name_s']}: flag={e['flag']} dims={list(e['lens'][1:])} size={e['size']}")
    e = next((x for x in ents if x['name_s']==varname), None)
    if e is None:
        print('var not found'); return
    payload = img[pt_data+e['data']: pt_data+e['data']+e['size']]
    if e['flag'] == 4:  # INT
        n = e['size']//4
        vals = struct.unpack_from('<%di'%n, payload, 0)
        L = e['lens']
        # part is [24][59][77]-ish: index as part(idx, frame, motion) per script usage
        d1, d2, d3 = L[1], L[2], L[3]
        print(f'INT array {d1}x{d2}x{d3} total {n}')
        import json
        return vals, (d1,d2,d3)
    print('not INT'); return None

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv)>2 else 'part')
