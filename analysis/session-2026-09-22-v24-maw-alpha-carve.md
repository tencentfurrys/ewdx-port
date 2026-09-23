# Session 2026-09-22 (F) — maw black box: root cause + v24 fix

Follows `session-2026-09-22-v22-pov-parity.md` (session E) and the v23-diag
build. The owner ran the v23-diag APK and delivered MuMu screenshots
(22:47–22:49) + 31 boot-log rotations.

## 1. What the v23-diag run proved

- The porthole interior DOES render: the prey body is visible inside the
  disc (owner screenshot 3). The session-C/E fear "interior never draws"
  is dead — `p_light`, buffer 5, `view_mot` all work.
- What remains is the **opaque black 80×80 square** around the disc,
  cutting across the scene (and the character's hand on screen 3).
- Journal smoking gun: `buf5 readback: nontransparent=100%` on **182/182**
  composites — the mask step (blend 3) never carved corner ALPHA to 0,
  while its RGB invert DID run (corners are black, and they'd be white if
  the invert had failed too).
- Also decisive: across the copied rotations there are **zero blend-3
  mask-tile draws and zero `DGGCOPY 5,1` copy-outs** — the rotations are
  snapshots of one long run; the porthole composite itself sits in the
  missing chunks. The readback + screenshot still pin the mechanism.

## 2. Root cause (proven, not guessed)

The composite (script L24143–24182, verbatim in session E's doc):

```
DGGSEL 5; DGCOLOR 255,255,255,255; DGCLEAR      (white clear)
view_mot stom_n, 5                              (prey interior)
DGBLENDMODE 3; DGRECT 242,72,40,40; DGGCOPY 3/8/16/24   (4 mirrored mask tiles)
DGGSEL 1; DGBLENDMODE 0; DGRECT 0,0,80,80; DGGCOPY 5,1  (copy out to scene)
```

Mask tile (v20 ground truth, `v21_tile_big.png`): **white outside the
disc, transparent inside** (the black disc pixels are colorkeyed to A=0).

D3D9 blends **alpha with the same factors as RGB** unless
D3DRS_SEPARATEALPHABLENDENABLE is set — and hmm.dll never sets it (GUIDE
§7: only SRCBLEND/DESTBLEND states in the draw path). So the original's
mode 3 = invert over all four channels:
- RGB: dst·(1−src) → white corners go black ✓ (the port did this too)
- Alpha: dst·(1−src.a) → corner A 255→0; disc-hole A=0 → dst untouched ✓

The port used `glBlendFunc(BLEND_SRC[3], BLEND_DST[3])` =
`GL_ZERO / GL_ONE_MINUS_SRC_COLOR`. **GLES2 forbids color factors on the
alpha unit** (GLES 2.0.25 §4.1.7: only ZERO/ONE/SRC_ALPHA/... legal).
The alpha half is silently invalid — on MuMu's GL-on-D3D shim the corners
stayed A=255. Exactly the observed RGB-works/alpha-doesn't split.

And the final copy's mode 0 (`ONE,ZERO`) is pure replace: it ignores
alpha entirely, so *even with* a perfect carve it would paint the corners
opaque. GUIDE.md's mode-0 recovery is corrected: D3D-verbatim 4-channel
math gives mode 0 the alpha row (ONE, INVSRCALPHA) — byte-identical to
replace for opaque sources (every script `DGBLENDMODE 0` draw uses
alpha-255, verified across all 8 call sites), and the only reading that
lets the reference video show the scene through the corners.

Why the v20 "not swapped / matches reference" verdict was still wrong:
it checked the RGB semantics of the mask (correct!) and a *web* capture
that showed a black-plate maw — but never checked corner ALPHA. The
triage doc's blend-3/4-swap hypothesis remains refuted; this is a third,
independent defect.

## 3. The fix (v24)

`ewdx_ndk/ewdx_gles.cpp`:
- Table split: `BLEND_RGB_SRC/DST` (unchanged verbatim D3D RGB rows) +
  new `BLEND_A_SRC/A_DST` rows mirroring D3D's 4-channel alpha factors:
  0:(ONE,INVSRCALPHA) 1:(SRCALPHA,INVSRCALPHA) 2:(SRCALPHA,ONE)
  3:(ZERO,INVSRCALPHA) 4:(ZERO,SRCALPHA) 5:(INVDESTALPHA,ZERO)
  6:(ONE,ONE) 7:(DSTALPHA,ONE) — all legal GLES2 alpha factors.
- `ewdx_apply_blend` now calls `glBlendFuncSeparate`.
- Mask uniform in the shader was NOT needed: the batcher's
  `ewdx_immediate_quad`/`ewdx_line`/prim paths only ever draw from
  white-1×1 or glyph textures (texel.r=1) and at blends ≠ 3/4, so
  red-as-alpha would be a no-op there anyway — dead idea dropped.
- Boot tag `v24-2026-09-22-maw-alpha-carve` (`ewdx_boot.cpp`);
  `EWDX_MAW_JOURNAL` off in CMakeLists (diag comment retained).

Build: `gradlew assembleDebug -x lint` → BUILD SUCCESSFUL (8 s warm).
Shipped `~/Downloads/ewdx-v24-maw-alpha-carve.apk` (57,411,457 B); tag
string verified inside `lib/arm64-v8a/libmain.so`.

## 4. Expected v24 behavior (owner test list)

1. Vore POV: **no black square** — scene (bushes/sky/floor) continues up
   to the white ring; interior unchanged from v23 (body visible).
2. Face-panel shade (L24671) / boss-name glow (L26470): unchanged RGB,
   and alpha behavior now D3D-correct (both draw to buffer 1 whose alpha
   is only consumed by mode-0 blits — none in between → no visible change
   expected; flag if anything looks off).
3. Head/door (v22 fix) still correct.

## 5. Corner-crop evidence (this repo)

`analysis/v22_pov/corners_big.jpg` — cloud-phone reference porthole
moments at 4× (scene visible through corners, no square).
`analysis/v22_pov/corners_ref_vs_mumu.jpg` — side-by-side: reference vs
v23 port state (opaque black square over the ring on the port side).
`analysis/v22_pov/mumu_full.jpg` — full MuMu v23 frames locating the
porthole + journal lines.
