txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
print('total lines:', len(lines))
print('=== ALL DLL-bind directives ===')
n = 0
for i, l in enumerate(lines, 1):
    s = l.strip()
    if s.startswith('#func') or s.startswith('#regfunc') or s.startswith('#uselib') or s.startswith('#funcdef') or s.startswith('#cfunc'):
        n += 1
        safe = s.encode('ascii', 'replace').decode()
        print(str(i).rjust(5) + ': ' + safe)
print('count:', n)
