# Dump the export names of the real hmm.dll (manual PE parse, no deps).
import struct, os

p = os.path.expanduser("~/Downloads/Echidna_Wars_DX_V1.11_ENG-JAP/Echidna Wars DX [ENG-JAP]/hmm.dll")
b = open(p, "rb").read()
b += b"\x00" * 65536  # the export table runs to EOF on disk; pad so the tail names read
e_lfanew = struct.unpack_from("<I", b, 0x3C)[0]
assert b[e_lfanew:e_lfanew+4] == b"PE\x00\x00", "not PE"
coff = e_lfanew + 4
machine, nsec = struct.unpack_from("<HH", b, coff)
opt = coff + 20
magic = struct.unpack_from("<H", b, opt)[0]
optsize = struct.unpack_from("<H", b, coff + 16)[0]
assert magic == 0x10B, "not PE32"
ddir = opt + 96  # data directories, export = 0
exp_rva, exp_size = struct.unpack_from("<II", b, ddir)
# sections
sec_off = opt + optsize
secs = []
for i in range(nsec):
    o = sec_off + 40 * i
    name = b[o:o+8].rstrip(b"\x00").decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", b, o+8)
    secs.append((name, vaddr, vsize, raddr, rsize))

def rva2off(rva):
    for name, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None

eo = rva2off(exp_rva)
assert eo is not None
_, _, _, _, nnames, _, _, nfuncs, nnamesp = struct.unpack_from("<IIHHIIIII", b, eo)
names_rva = struct.unpack_from("<I", b, eo + 32)[0]
no = rva2off(names_rva)
names = []
for i in range(nnames):
    if no + 4 * i + 4 > len(b):
        names.extend(["<eof-truncated>"] * (nnames - i))
        break
    nrva = struct.unpack_from("<I", b, no + 4 * i)[0]
    o = rva2off(nrva)
    if o is None or o >= len(b):
        names.append("<bad-rva:%08x>" % nrva)
        continue
    end = b.find(b"\x00", o, len(b))
    if end < 0:
        end = min(o + 64, len(b))
    names.append(b[o:end].decode("latin-1", "replace"))
print("machine=0x%x sections=%d exports=%d" % (machine, nsec, nnames))
import sys
for n in names:
    try:
        print(n)
    except UnicodeEncodeError:
        print(n.encode("ascii", "backslashreplace").decode())
