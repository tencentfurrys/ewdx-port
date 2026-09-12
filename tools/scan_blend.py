import re
txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
print('=== every DGBLENDMODE call site (mode values actually used) ===')
vals = set()
for i, l in enumerate(lines, 1):
    s = l.strip()
    m = re.match(r'DGBLENDMODE\s+(.*)$', s)
    if m:
        arg = m.group(1).strip()
        print(str(i).rjust(5) + ': DGBLENDMODE ' + arg.encode('ascii','replace').decode()[:120])
        if re.fullmatch(r'-?\d+', arg):
            vals.add(int(arg))
print('literal modes used:', sorted(vals))
