import re
txt = open(r'C:\Users\runneradmin\AppData\Local\Temp\2\opencode\start_ax_dump.hsp','rb').read().decode('shift-jis', errors='replace')
lines = txt.splitlines()
# 1. init section: first 250 lines
print('=== LINES 1-120 (header + init) ===')
for i in range(0, 120):
    safe = lines[i].encode('ascii', 'replace').decode()
    print(str(i+1).rjust(5) + ': ' + safe)
