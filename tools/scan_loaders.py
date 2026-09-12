txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
def show(a, b, title):
    print('=== %s (lines %d-%d) ===' % (title, a, b))
    for i in range(a-1, min(b, len(lines))):
        safe = lines[i].encode('ascii', 'replace').decode()
        print(str(i+1).rjust(5) + ': ' + safe)
show(1600, 1700, 'mot/mold loaders')
show(2000, 2060, 'pic loader via bload+DGLOADMEMORY')
