# Session 2026-09-23 (F.3) — v25: primitive wobble path (stomach squirm/flash)

Follows `session-2026-09-23-v241-owner-test.md`. Owner: "still has some
black… the red stomach flash, intestines is suppose to move".

## Root cause (port gap, one subsystem)

The squirming stomach wall / intestines are the `part(19)==1` parts, drawn
by `draw_mot_b` (L2610-2640) via the **primitive path**:

```
DGGSEL 6; DGCOLOR 0,0,0,256; DGCLEAR          (v24.1 fixed the clear)
DGTEXTURE <mouth atlas>; DGBLENDMODE 1
repeat part(5,…):                              (one PER 1px SCANLINE)
    DGRECT <src row>; DGSCALEANDANGLE <dest width px>, 1, 0
    DGPOS <row pos>; DGADDPRIMITIVE 7
DGDRAWPRIMITIVE
```

The port's stub appended ONE degenerate vertex per DGADDPRIMITIVE
(uv=0,0, no size) and fanned them — effectively nothing was drawn, so
buffer 6 (the wobbled wall) stayed empty and every part drawn FROM it
was the residual black. Same stub also silently ate the shadow pass
(draw_grav L3020-3040) and view_blur — all primitive consumers.

## The rewrite (ewdx_batch.cpp)

- `DGADDPRIMITIVE` now snapshots the full DG state per call
  (pos/rect/scale/angle/color; `EwdxPrim`, cap 512 ≥ 256 scanline rows).
- `DGDRAWPRIMITIVE` expands each snapshot to a real textured quad via
  `emit_quad` with **flag&2 dest semantics** (dest dims = raw
  DGSCALEANDANGLE px — matches `draw_mot_b`, `view_blur`, `draw_sub`
  which all pre-scale by part dims), fanned 4-vert D3D semantics kept
  by emit_quad's 6-vert expansion.
- Blend is read at DRAW time (shadow pass sets `DGBLENDMODE 4` after
  adding), color per-snapshot (the red flash block recolors
  `DGCOLOR 80,10,10,…` inside the loop — the flash now rides the same
  quads automatically).
- `DGCOLOR 0,0,0,256` inside those loops now truncates to a=0 via
  v24.1's `&0xff` (harmless here: blend-1 src factor multiplies by src.a).

Tag `v25-2026-09-23-prim-wobble`; shipped
`~/Downloads/ewdx-v25-prim-wobble.apk` (57,411,841 B), tag verified in
libmain.so.

## Owner test list (v25)

1. Vore POV: stomach wall + intestines **squirm** (wobble animates),
   red **flash** pulses during the hold; interior black gone.
2. Enemy drop-shadows (primitive consumers) may reappear — flag if any
   shadow looks wrong (they were invisible since v1).
3. Regression watch: `DGCREATEPRIMITIVE` sites (title/gallery `infdraw_op`
   blocks) — any new garbage there is the prim path over-drawing.

## 25.1 follow-up (owner test of v25, same day)

Owner: POV improving ("one intestine moving" — wall now renders), but a
**wall of white rows** appeared on a menu screen (video 4 ~14 s). Cause:
v25 discarded `DGADDPRIMITIVE`'s int argument. The script declares it and
always passes the **DGGCOPY flag word**: `7` (centered + rect-scale +
ctr-anchor) in the wobble scanline loops, `7 + 8*flip` for mirrored menu
tiles, `7 + 16*flip` variants, and `infdraw_op = 7 + 8*prm_406(6,sd)` in
the `infdraw` infinite-scroll tiling (L2875-2967) — whose per-tile
bounds-guards (break/continue on x/y) only make sense for CENTERED
tiles. v25 drew everything as uncentered flag-0: scanlines shifted half
a row; menu tiles shifted and spilled past the guards = the white rows.

Fix (v25.1): thread the flag word through register glue →
`dg_addprim(unsigned)` → `EwdxPrim.flags`, honored in `ewdx_drawprim`
(same flag semantics as `ewdx_copy_flags`: &1 center, &2 rect-scale,
&4 ctr_anchor, &8 uflip, &0x10 vflip). Cap stays 512.

Shipped `~/Downloads/ewdx-v251-prim-flags.apk` (57,412,113 B), tag
`v25.1-2026-09-23-prim-flags` verified in libmain.so.
Evidence: `analysis/v25_evidence/` (contact sheets of videos 3+4,
screenshot 01:06).
