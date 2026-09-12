import re
txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
fns = ['DGCOLOR','DGCLEAR','DGREDRAW','DGGSEL','DGBUFFER','DGLOADMEMORY','DGPOS','DGRECT',
       'DGSCALEANDANGLE','DGBLENDMODE','DGGCOPY','DGFONT','DGDRAWTEXT','DGTEXTURE',
       'DGADDPRIMITIVE','DGDRAWPRIMITIVE','DGCREATEPRIMITIVE','DGLINE','DGSCREEN','DGINIT','DGEND',
       'DIGETJOYSTATE','DIGETJOYNUM','DIINIT','DIEND','HMMHITCHECK','HMMGETFPS','HMMSLEEP',
       'dmmload','dmmplay','dmmstop','dmmvol','dmmpan','dmmini','dmmbye',
       'vsave_start','vsave_put','vsave_end','vload_start','vload_get','vload_end']
print('=== first call-site per runtime fn ===')
for fn in fns:
    rx = re.compile(r'(?<![\w#])' + re.escape(fn) + r'(?![\w])')
    for i, l in enumerate(lines, 1):
        s = l.strip()
        if s.startswith('#') or s.startswith('*'):
            continue
        if rx.search(s):
            print(fn.ljust(18) + ' L' + str(i).rjust(5) + ': ' + s.encode('ascii','replace').decode()[:150])
            break
print()
print('=== map loader area 1820-1900 ===')
for i in range(1819, 1900):
    safe = lines[i].encode('ascii', 'replace').decode()
    print(str(i+1).rjust(5) + ': ' + safe[:150])
