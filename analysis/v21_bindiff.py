#!/usr/bin/env python3
# Align two stripped AArch64 disassemblies function-by-function: normalize
# addresses (branch targets -> function-relative, data refs -> 'D', PLT/other
# symbols -> name only), then walk both function lists in parallel and report
# where the normalized bodies diverge (the v20 source delta).
import re, sys

PA = sys.argv[1] if len(sys.argv) > 1 else 'dis_HEAD.txt'
PB = sys.argv[2] if len(sys.argv) > 2 else 'dis_v20.txt'

def parse(path):
    funcs = []  # (name, [(addr, text)])
    cur = None
    hdr = re.compile(r'^([0-9a-f]+) <(.+)>:$')
    ins = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = hdr.match(line.rstrip())
            if m:
                cur = (m.group(2), [])
                funcs.append(cur)
                continue
            m = ins.match(line)
            if m and cur is not None:
                cur[1].append((int(m.group(1), 16), m.group(2).strip()))
    return funcs

def normalize(func):
    name, body = func
    if not body:
        return name, []
    start = body[0][0]
    # function extent: up to the next function's start (callers pass neighbors)
    out = []
    for addr, text in body:
        # strip symbolic suffix for PLT-ish refs, keep name
        text = re.sub(r'<([^+>]*)\+0x[0-9a-f]+>', r'<\1>', text)
        # relative-ize addresses in operands
        def rel(m):
            v = int(m.group(1), 16)
            if start <= v < start + 0x40000:
                # inside .text: make function-relative if within this func
                if v >= start:
                    return 'f+%x' % (v - start) if v - start < 0x2000 else 'T'
                return 'T'
            return 'D'
        text = re.sub(r'0x([0-9a-f]+)', rel, text)
        out.append(text)
    return name, out

A = parse(PA)
B = parse(PB)
print("HEAD funcs: %d  v20 funcs: %d" % (len(A), len(B)))

NA = [normalize(f) for f in A]
NB = [normalize(f) for f in B]

i = j = 0
mismatches = []
while i < len(NA) and j < len(NB):
    na, ba = NA[i]
    nb, bb = NB[j]
    if ba == bb:
        i += 1; j += 1
        continue
    # try resync: look ahead up to 6 funcs for an identical pair
    sync = None
    for di in range(0, 7):
        for dj in range(0, 7):
            if i+di < len(NA) and j+dj < len(NB) and NA[i+di][1] == NB[j+dj][1] and NA[i+di][1]:
                sync = (di, dj)
                break
        if sync: break
    if sync:
        mismatches.append((i, j, sync))
        i += sync[0]; j += sync[1]
    else:
        mismatches.append((i, j, None))
        i += 1; j += 1

print("mismatch groups: %d" % len(mismatches))
for (mi, mj, s) in mismatches[:12]:
    na, ba = NA[mi]
    nb, bb = NB[mj]
    print("\n=== HEAD[%d] %s (%d ins)  vs  v20[%d] %s (%d ins)  resync=%s" % (
        mi, na, len(ba), mj, nb, len(bb), s))
    # print first differing lines
    for k in range(min(len(ba), len(bb))):
        if ba[k] != bb[k]:
            print("  first diff @+%d:" % k)
            for d in range(max(0,k-3), min(len(ba), k+6)):
                mark = ' ' if d < len(ba) and d < len(bb) and ba[d]==bb[d] else '*'
                la = ba[d] if d < len(ba) else '-'
                lb = bb[d] if d < len(bb) else '-'
                print("  %s H: %s" % (mark, la))
                print("  %s V: %s" % (mark, lb))
            break
