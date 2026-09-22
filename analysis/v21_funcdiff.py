#!/usr/bin/env python3
# Side-by-side diff of the ONE divergent function (ewdx_copy_flags+inlined
# emit_quad) between HEAD and the shipped v20 binary, with float-constant
# annotation so the v20 source change can be reconstructed exactly.
import re, subprocess, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from v21_bindiff import parse, normalize  # noqa

A = parse('dis_HEAD.txt')
B = parse('dis_v20.txt')
fa = next(f for f in A if f[0] == '_Z15ewdx_copy_flagsii')
fb = next(f for f in B if f[0] == '_Z15ewdx_copy_flagsii')
na, ba = normalize(fa)
nb, bb = normalize(fb)
open('func_HEAD.txt', 'w').write('\n'.join(ba))
open('func_v20.txt', 'w').write('\n'.join(bb))

# normalized-bodies diff with context
r = subprocess.run(['diff', '-U6', 'func_HEAD.txt', 'func_v20.txt'],
                   capture_output=True, text=True)
print("normalized diff lines:", len(r.stdout.splitlines()))
print(r.stdout[:12000])
