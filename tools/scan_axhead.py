import struct
p = r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.bin'
buf = open(p, 'rb').read()
print('file size:', len(buf))
magic = buf[0:4]
print('magic:', magic)
fields = ['version','max_val','allsize','pt_cs','max_cs','pt_ds','max_ds','pt_ot','max_ot',
          'pt_dinfo','max_dinfo','pt_linfo','max_linfo','pt_finfo','max_finfo','pt_minfo','max_minfo']
vals = struct.unpack_from('<17I', buf, 4)
for name, v in zip(fields, vals):
    print(name.rjust(10), '=', v, hex(v))
# LIBDAT entries: each is (flag:short, option:short, libname:int(ds off), funcname:int(ds off), ...) - find dll names in DS
pt_ds, max_ds = vals[5], vals[6]
ds = buf[pt_ds:pt_ds+max_ds]
print('--- .dll/.hrt strings in DS ---')
import re
for m in re.finditer(rb'[\x20-\x7e]{2,}\.(?:dll|hrt|dpm|ax)\x00', ds):
    print('ds+0x%X: %s' % (m.start(), m.group(0).decode()))
print('--- all nul-terminated strings in DS containing backslash or dot (sample) ---')
count = 0
for m in re.finditer(rb'[A-Za-z0-9_][\x20-\x7e]{1,40}\x00', ds):
    s = m.group(0)[:-1].decode()
    if ('\\' in s or s.endswith('.dll')) and len(s) < 48:
        print('ds+0x%X: %s' % (m.start(), s))
        count += 1
        if count > 40:
            break
