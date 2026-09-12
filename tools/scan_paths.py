import re
txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
# string literals containing path separators or known folder/file ext names
pat = re.compile(r'"([^"]*(?:/|\\\\|\.bmp|\.wav|\.ogg|\.mol|\.mot|\.map|\.dat|\.txt)[^"]*)"')
print('=== string literals w/ paths or asset exts ===')
seen = {}
for i, l in enumerate(lines, 1):
    for m in pat.finditer(l):
        s = m.group(1)
        seen.setdefault(s, []).append(i)
for s, occ in sorted(seen.items(), key=lambda x: (len(x[1]), x[0])):
    safe = s.encode('ascii', 'replace').decode()
    print(repr(safe) + '  x' + str(len(occ)) + '  e.g.line ' + str(occ[0]))
print()
print('=== bload/picload/celload/chdpm/memfile call sites ===')
for i, l in enumerate(lines, 1):
    s = l.strip()
    if re.match(r'(bload|picload|celload|chdpm|memfile|exist|delete|dirlist|chdir|mkdir)\b', s):
        safe = s.encode('ascii', 'replace').decode()
        print(str(i).rjust(5) + ': ' + safe[:160])
