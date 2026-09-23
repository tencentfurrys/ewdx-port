# Session 2026-09-23 (F.2) — v24.1 owner test: black square GONE

Owner screenshot `MuMu-20260923-003837-651.png` (00:38, ZOOM-ON) + 41 fresh
boot logs, all on tag `v24.1-2026-09-23-clear-alpha256`, zero error lines,
GPU = Adreno 640 / GLES 3.2.

## Confirmed fixed

- **The big black square is GONE.** The zoomed POV porthole now shows the
  red stomach interior with the prey body inside; the scene (trees,
  fence, bushes, ground) continues around the ring. Both the v24 blend
  fix (mask alpha carve + alpha-respecting mode 0) and the v24.1 clear
  truncation (DGCOLOR alpha 256 → 0) were required.
- Full fix chain for the "black box" era: v22 (flag&4 anchor) →
  v24 (glBlendFuncSeparate + mode-0 alpha row) → v24.1 (&0xff truncation).

## Newly reported (next work)

1. **Residual black inside the disc** — the region of stomach wall around
   the prey reads near-black on the port; the cloud-phone reference keeps
   it visibly red.
2. **The red stomach flash is missing** — the wall is supposed to pulse.
3. **The intestines are supposed to MOVE** — the interior parts are
   animated in the original; on the port they hold still.

### Leads (from the script, for the next session)

- `view_mot` (L2411) iterates the unit's 100 `part(...)` records and calls
  `draw_mot`/`draw_sub`; per-part animation lives in the .mot data
  (wobble amp/freq/phase at `part(20/21/22)` is applied in the staging
  blocks as `l_amp * sin(l_freq * cnt + l_phase)` — the stomach unit's
  mot should drive its wall/intestine parts the same way).
- The stomach unit `stom_n` is spawned at L4962 (`puteff2 "stom"…` /
  `seteff`) with `unit(19, stom_n)=0` then `=100` in the composite block
  (L24142/24183) — `unit(19,…)` scales `vmx/vmy` (part drift offset in
  view_mot: `(-0.01)*unit(19)*fcx`). If it pins at 0 or 100 without
  ticking, interior parts freeze — check what advances it.
- `p_light` gates interior visibility (DGCOLOR alpha, blend 2/1) and
  decays −40/tick; the "flash" is likely a periodic re-set or the mot's
  own color modulation. Near-black wall = p_light bottomed out while the
  animation that should refresh it isn't running.
- Frame source: `fr = unit(12, pn)` in view_mot — verify it advances for
  stom_n (v23 journal: interior draws all carried static coords).

Evidence: `analysis/v24_blackbox/` (v24.1 screenshot + keyed asset
previews + v24 shot), logs in `MuMuSharedFolder/Download/`
(41× v24.1 — current era; pre-v24.1 logs in
`archive_20260922_pre_v241/`).
