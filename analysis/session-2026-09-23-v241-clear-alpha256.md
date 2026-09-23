# Session 2026-09-23 (F.1) — the REAL black-box root cause: DGCLEAR alpha 256

Follows `session-2026-09-22-v24-maw-alpha-carve.md`. The owner tested v24:
**the black square survived** (screenshot MuMu-20260923-001501-147.png,
00:15). Logs confirmed the device really ran v24 (61 logs on tag
`v24-2026-09-22-maw-alpha-carve`, journal off — the earlier "still v23"
read was wrong; the `[maw]`-filled logs at 22:45 were the v23-diag run).

## 1. Why v24 failed, and what the screenshot proved

The v24 screenshot's black square + dithered ring measures **~300 game
pixels** across. The buffer-5 mask composite is **80×80**. Size mismatch
= the v24 theory (mask corner alpha-carve + mode-0 replace) was at best
half the story: even with perfect mask corners the v24 build could not
produce a 300px square from an 80px composite.

Also: the HUD showed **ZOOM-ON** (`v_zoom_f=1`) — the POV porthole is
rendered zoomed by a staging chain, not the raw 80×80 blit.

## 2. Journal forensics: the zoomed staging chain

Scanning every ring-sized draw in the v23-diag journal (`[maw]` lines):

- `id=6 f=0x5 dst=(160,120 256x256) sc≈(1.1..1.5)` **tgt=5** — buffer 6's
  texture drawn INTO buffer 5, scaled 287→394 px over ~50 samples (the
  zoom animation; 287 px matches the screenshot square exactly).
- `id=9/10/12/13 f=0x7 ... tgt=1` — the mouth art composition blocks.

The script's staging blocks (L2522/2617/2710/2791/3685, and every menu):

```
DGGSEL 6
DGCOLOR 0, 0, 0, 256     ← alpha 256 (!)
DGCLEAR
<draw mouth art with blend 1>
```

All **15** `DGCOLOR …, 256` sites in the 31k-line script are staging or
menu-buffer clears.

## 3. Root cause

D3D9 packs DGCOLOR's four args into a 32-bit D3DCOLOR (RGBA bytes): an
alpha of **256 truncates to 0**. On the original, every staging/menu
clear is therefore **born-transparent black** — the mouth art drawn on
top stays visible, and the black background never survives any later
composite. (GSL_Alpha maxes at 255, so 256 is "just over" — likely an
original-script quirk that happened to be load-bearing.)

The port stored `a=256` and `glClearColor` clamped 256/255 → 1.0
**opaque**: every staging buffer shipped an opaque black square through
the zoom chain (buffer 6 → buffer 5 → screen). That is the vore POV
black box — and it also explains why the square behaved oddly across
v20–v23 (any path touching staging buffers 5/6 was contaminated).

Why menus never showed it: menu frames copy an opaque full-screen scene
frame over the cleared buffer 5 before display, hiding the clear; the
vore zoom chain composites the clear directly.

Asset sanity (for the record): `stom_s*.bmp` (270×284, the ring art)
has a transparent surround under the port's black-colorkey rule, and
`stom1/stom2.bmp` are the prey-part atlases — the art was never the
problem. Previews: `analysis/v24_blackbox/keyed_*.png`.

## 4. Fix (v24.1) and relationship to v24

`ewdx_gles.cpp ewdx_color`: mask all four channels `& 0xff` — hardware
truncation parity. One line, covers all 15 sites.

Both fixes are required and independent:
- **v24** (`glBlendFuncSeparate`, mode 0 alpha-respecting): makes the
  80×80 mask composite carve its corners. Without it the mask corners
  would still paint over the scene.
- **v24.1** (`&0xff` truncation): makes the staging clears transparent
  so no black square rides the zoom chain.

Build: BUILD SUCCESSFUL (2 s warm). Shipped
`~/Downloads/ewdx-v241-clear-alpha256.apk` (57,411,457 B), tag
`v24.1-2026-09-23-clear-alpha256` verified inside libmain.so.

## 5. Hygiene

All 126 pre-v24.1 boot logs moved to
`MuMuSharedFolder/Download/archive_20260922_pre_v241/` — the Download
root is now empty, so the next test's logs are unambiguous (fresh logs
on tag `v24.1-…` only).

## 6. Owner test list (v24.1)

1. Vore POV with ZOOM-ON: **no black square** — scene visible up to the
   ring, prey body inside, mask corners scene-through.
2. Menus/title/pause: unchanged (they overpaint the clear anyway).
3. Head/door (v22) still correct.
