import re
txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
def show(a, b, title):
    print('=== %s (lines %d-%d) ===' % (title, a, b))
    for i in range(a-1, min(b, len(lines))):
        safe = lines[i].encode('ascii', 'replace').decode()
        print(str(i+1).rjust(5) + ': ' + safe[:170])
show(3820, 3960, 'se + music loaders')
print()
print('=== DGINIT/DGSCREEN/DIINIT call sites ===')
for i, l in enumerate(lines, 1):
    s = l.strip()
    if re.match(r'(DGINIT|DGSCREEN|DGBUFFER|DIINIT|DGEND|dmmini)\b', s):
        print(str(i).rjust(5) + ': ' + s.encode('ascii','replace').decode()[:170])
print()
print('=== DSLOADFNAME/DS* call sites (sample) ===')
n=0
for i, l in enumerate(lines, 1):
    s = l.strip()
    if re.match(r'DS\w*\b', s):
        print(str(i).rjust(5) + ': ' + s.encode('ascii','replace').decode()[:170])
        n+=1
        if n>15: break
print('=== DMM call sites (sample) ===')
n=0
for i, l in enumerate(lines, 1):
    s = l.strip()
    if re.match(r'dmm\w*\b', s):
        print(str(i).rjust(5) + ': ' + s.encode('ascii','replace').decode()[:170])
        n+=1
        if n>15: break
