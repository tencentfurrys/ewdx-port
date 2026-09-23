import struct, sys

FX = 0x55AA0000

def parse(path):
    img = open(path,'rb').read()
    magic, ver, num, pt_data = struct.unpack_from('<IIII', img, 0)
    ents = []
    for i in range(num):
        off = 16 + i*64
        name_off, data_off = struct.unpack_from('<II', img, off)
        flag, mode = struct.unpack_from('<hh', img, off+16)
        lens = struct.unpack_from('<IIIII', img, off+20)
        size = struct.unpack_from('<I', img, off+40)[0]
        nm = img[pt_data+name_off:img.index(b'\0', pt_data+name_off)].decode('sjis','replace')
        ents.append((nm, flag, lens, size, pt_data+data_off))
    return img, ents

def get_int(img, ent):
    nm, flag, lens, size, off = ent
    vals = struct.unpack_from('<%di'%(size//4), img, off)
    d1,d2,d3 = lens[1], lens[2], lens[3]
    def idx(i,c,f):
        return vals[((f*d2)+c)*d1+i]
    return idx, (d1,d2,d3)

def get_strs(img, ent):
    nm, flag, lens, size, off = ent
    out=[]
    p = off
    blocks = size//4
    for k in range(blocks):
        tag, bsz = struct.unpack_from('<II', img, p)
        assert tag==FX, hex(tag)
        s = img[p+8:p+8+bsz].decode('sjis','replace')
        out.append(s)
        p += 8+bsz
    return out

def dump(path):
    img, ents = parse(path)
    pics = get_strs(img, next(e for e in ents if e[0]=='pic'))
    print(path.split('/')[-1], 'pics:', pics)
    idx, dims = get_int(img, next(e for e in ents if e[0]=='t_part'))
    d1,d2,d3 = dims
    nparts = 0
    for c in range(d2):
        if idx(13, c, 0) == 0 and idx(5, c, 0) == 0: pass
    # count parts at frame 0 (blend==0 terminates per view_mot; but safer: nonzero rows)
    parts=[]
    for c in range(d2):
        rec = [idx(i,c,0) for i in range(24)]
        if rec[13]==0 and rec[4]==0 and rec[5]==0 and rec[0]==0 and rec[1]==0:
            continue
        parts.append(rec)
    print('parts at frame0:', len(parts))
    for c,rec in enumerate(parts):
        tag19 = rec[19]
        tex = pics[rec[18]] if 0 <= rec[18] < len(pics) else '?'
        print(f'  p{c}: pos=({rec[0]},{rec[1]}) rect=({rec[2]},{rec[3]},{rec[4]}x{rec[5]}) sc=({rec[6]},{rec[7]}) ang={rec[8]} col=({rec[9]},{rec[10]},{rec[11]},{rec[12]}) bl={rec[13]} tex={tex} wobble={tag19} amp={rec[20]} fq={rec[21]} ph={rec[22]} id={rec[23]}')
    # frame animation of the wobbled part(s): rect rows and pos over frames
    for c in range(min(len(parts), d2)):
        if parts[c][19]==1:
            print(f'  wobbled p{c} over frames 0..{min(10,d3-1)}:')
            for f in range(0, min(10,d3)):
                print(f'    f{f}: pos=({idx(0,c,f)},{idx(1,c,f)}) rect=({idx(2,c,f)},{idx(3,c,f)},{idx(4,c,f)}x{idx(5,c,f)}) amp={idx(20,c,f)}')

for p in sys.argv[1:]:
    dump(p)
    print()
