# hmm_spy — PC-side draw spy for Echidna Wars DX

A drop-in replacement `hmm.dll` that **forwards every one of the real DLL's
114 exports** to `hmm_real.dll` while logging the complete DG* draw stream in
the same vocabulary as the Android port journals (`[pc-maw]`, `[pc-prim]`,
`[pc-dump]`). PC ground truth and Android `[maw]`/`[prim]` logs become
line-by-line diffable.

## Install (on the PC that runs the game)

1. Open the game folder (`Echidna Wars DX [ENG-JAP]/`).
2. Rename the real `hmm.dll` → `hmm_real.dll` (keep it!).
3. Copy this build's `hmm.dll` into the folder.
4. Run `EchidnaWarsDX.exe` normally.

Output: `hmm_spy.log` in the game folder (appends across runs).

## What gets logged

- Full stream into a 4 MB ring buffer: every DGCOLOR / DGGSEL / DGCLEAR /
  DGRECT / DGPOS / DGSCALEANDANGLE (with 8.8→float) / DGBLENDMODE / DGTEXTURE /
  DGLINE / DGBUFFER / DGREDRAW / DGADDPRIMITIVE / DGDRAWPRIMITIVE /
  DGCREATEPRIMITIVE, plus `DGGCOPY id flags`.
- **The ring flushes to `hmm_spy.log` whenever `DGGCOPY 5` fires** — the POV
  composite — so every squeeze moment produces a complete trace, marked
  `[pc-dump] FLUSH ep=N`. No gigabytes of menu spam.

## Diffing against the port

Port side (Android): the `[maw]`/`[prim]` lines in `ewdx-boot*.log` use the
same call sequence. Compare per POV episode:

```
grep "\[pc-" hmm_spy.log | sed -n '1,400p'
```

vs the port's `[maw]` lines around an `id=5` copy. Any divergence in draw
order, flags, scale, blend, or target is the port bug.

## Rebuild

```
python3 tools/hmm_exports.py | grep -oE "^_[A-Z0-9]+@16" | sort -u > tools/hmm_spy/hmm_names.txt
python3 tools/hmm_spy/gen_spy_c.py
python3 -m ziglang cc -target x86-windows-gnu -shared -O2 -o tools/hmm_spy/hmm.dll tools/hmm_spy/hmm_spy.c
```

(zig via `pip install ziglang`; `-target x86-windows-gnu` = 32-bit, the game
is x86. Regenerate `hmm_spy.c` after any edit to `gen_spy_c.py` only.)

## Uninstall

Delete the spy `hmm.dll`, rename `hmm_real.dll` back to `hmm.dll`.

## Files

- `hmm_spy.zig` — first draft (kept for reference; Zig 0.16 churn killed it)
- `gen_spy_c.py` — generates `hmm_spy.c` from `hmm_names.txt`
- `hmm_names.txt` — the 114 real export names (from `tools/hmm_exports.py`)
- `hmm_spy.c` → `hmm.dll` — the built shim (x86)
