# Re-applies the fullscreen + no-popup patch to an unpatched EchidnaWarsDX libmain.so
# usage: python3 patch_libmain.py <path/to/lib>   (the folder with arm64-v8a/ and armeabi-v7a/)
# needs: pip install lief
import sys, lief
EDITS = {
  "arm64-v8a": [
    (0x3b43c, None, "1f2003d5"),          # SDL_ShowSimpleMessageBox("Previous run stopped...") -> nop
    (0x3b444, None, "1f2003d5"),          # SDL_Delay(10000) -> nop
    (0x2cfac, "48048052", "68048052"),    # window flags 0x22 -> 0x23 (adds SDL_WINDOW_FULLSCREEN)
  ],
  "armeabi-v7a": [
    (0x1f5f6, "35f09ceb", "00bf00bf"),    # message box -> nop
    (0x1f5fe, "35f0a0eb", "00bf00bf"),    # 10 s delay -> nop
    (0x14fe8, "2220", "2320"),            # window flags 0x22 -> 0x23
  ],
}
root = sys.argv[1]
for arch, edits in EDITS.items():
    p = f"{root}/{arch}/libmain.so"
    b = lief.parse(p); data = bytearray(open(p, "rb").read())
    for va, old, new in edits:
        off = b.virtual_address_to_offset(va); n = bytes.fromhex(new)
        if old: assert data[off:off+len(n)] == bytes.fromhex(old), (arch, hex(va))
        data[off:off+len(n)] = n
    open(p, "wb").write(data); print("patched", p)
