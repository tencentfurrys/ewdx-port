import struct, re
p = r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.bin'
buf = open(p, 'rb').read()
vals = struct.unpack_from('<17I', buf, 4)
names = ['version','max_val','allsize','pt_cs','max_cs','pt_ds','max_ds','pt_ot','max_ot',
         'pt_dinfo','max_dinfo','pt_linfo','max_linfo','pt_finfo','max_finfo','pt_minfo','max_minfo']
H = dict(zip(names, vals))
pt_li, max_li = H['pt_linfo'], H['max_linfo']
print('LINFO at', hex(pt_li), 'size', max_li)
# LIBDAT guess: 16 bytes each -> flag(4), libname ds-off(4), opt(4), reserved(4)? try dump as u32 words
n = max_li // 4
words = struct.unpack_from('<%dI' % n, buf, pt_li)
for i in range(0, n, 4):
    print('entry%d:' % (i//4), [hex(w) for w in words[i:i+4]])
pt_ds = H['pt_ds']
def ds_str(off):
    end = buf.index(b'\x00', pt_ds + off)
    return buf[pt_ds+off:end].decode('ascii', 'replace')
print('--- resolve libname fields as DS offsets ---')
for i in range(0, n, 4):
    for w in words[i:i+4]:
        if 0 < w < H['max_ds']:
            try:
                print(hex(w), '->', ds_str(w))
            except Exception:
                pass
    print('---')
