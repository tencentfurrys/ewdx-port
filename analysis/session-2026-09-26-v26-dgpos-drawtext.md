# Session 2026-09-26 (II) — v26: DGDRAWTEXT draws at DGPOS (floating words +
# missing difficulty description fixed)
Owner report (v25.6): "black corners are gone on alpha test apk yay finally.
Next: fix the words, they are floating — they are supposed to be at the
bottom — and when I select a character there's supposed to be the words
describing the game mode, 'normal — standard level for gamers' — it doesn't
show that."

## Both symptoms, ONE root cause
The port's EWDX_DGDRAWTEXT handler read its two int args as x/y
(ewdx_register.cpp): `int x = code_getdi(0), y = code_getdi(0);`.

But in the script, those args are HSP PADDING. Every single DGDRAWTEXT call
site passes "320, 240" (artifacts/start_ax_dump.hsp):
- draw_guide (line 2401): `DGPOS prm_388/389, prm_390` THEN
  `DGDRAWTEXT prm_391, 320, 240` (lines 2404/2408)
- gallery BGM rows 28955-28983: `DGPOS glgdx, glgdy` THEN
  `DGDRAWTEXT "...", 320, 240` (8x)
- title (C) line 29624: `DGPOS 490, 480-14` THEN
  `DGDRAWTEXT "(C) 2016 D-Gate/ASIMOFU", 320, 60` — note 60 ≠ DGPOS y=466:
  the script itself sends MEANINGLESS y args, proving args are padding and
  the retained DGPOS owns the position.

The real hmm.dll DGDRAWTEXT therefore draws at the retained DGPOS (same
retained-state model as every DG draw: pos/rect/scale/color persist in the
ctx at +0x13ec..). The port instead drew at literal (320,240):

1. "Floating words" (title guide, screenshot 1): draw_guide x=524 y=448
   (bottom-right, under the menu) — port drew at (320,240) = screen center,
   ON TOP of the menu items ("2. Enter / X: back" over STAGE SELECT).
2. Missing difficulty description (screenshot 2): *label_241 renders into
   buffer 5 = 320x240, then scales to the screen. draw_guide asked for
   x=160-80-22=58, y=180 (under the difficulty row). Port drew at
   (320,240) — ONE PIXEL OFF the bottom-right corner of a 320x240 target:
   fully clipped, nothing drawn. After the buffer upscale the (missing)
   text would have landed at bottom-center — "Standard level for Gamer"
   was never in the framebuffer at all.

The (C) line also proves the old port could not be right even for its own
reading: args (320,60) with DGPOS (490,466) — D3D9 shows the (C) at the
BOTTOM-right (DGPOS), not mid-screen (args).

## Fix (v26, tag v26-2026-09-26-dgpos-drawtext)
- ewdx_batch.cpp ewdx_drawtext: ignore x/y args (documented padding),
  draw at m_posx/m_posy (the DGPOS state) — matches the retained-state
  model of DGGCOPY/DGRECT.
- ewdx_register.cpp EWDX_DGDRAWTEXT: args consumed and discarded.
- Header comments updated (ewdx_batch.h, ewdx_dg.h).

## Verification
- All 10 DGDRAWTEXT call sites audited: 100% pass padding args; DGPOS is
  always set immediately before every call (draw_guide internally, BGM
  rows, (C) line). Zero call sites rely on arg semantics.
- Position math checks: title guide (524,448) lands bottom-right on the
  640x480 screen (matches the web reference layout); difficulty guide
  (58,180) is inside buffer 5 (320x240) → visible after the x2 upscale.
- Syntax gate green (aarch64-linux-android21-clang++ -fsyntax-only
  -DHSP64 -Wall -Wextra: ewdx_batch.cpp, ewdx_register.cpp + the touched
  headers' TUs; zero warnings).
- assembleDebug green (1m53s, fresh-box env rebuilt this session: sibling
  checkouts at refs.md pins ~/Documents/{SDL2 b90ac95, SDL_ttf 7f16032
  (+freetype 535d299, harfbuzz 950d232), OpenHSP 3dbb872}; 593 assets
  re-staged from the shipped v256 APK; local.properties forward-slash;
  debug.keystore regenerated).
- Build tag verified in libmain.so: "build v26-2026-09-26-dgpos-drawtext".
- APK content: 590 data files + start.ax 764,226 B + save.dat verified.

## Shipped
- ~/Downloads/ewdx-v26-dgpos-drawtext.apk (56,813,745 B)
- apks repo: committed locally (8d2a930) but NOT pushed — this fresh box
  has no ~/.git-credentials (owner token needed, same as session H:
  "no token on this box"). Push = paste a fresh token into
  ~/.git-credentials (https://<user>:<token>@github.com), then
  `git -C ~/Documents/ewdx-port push origin master` and
  `git -C /tmp/apks push origin main` (or re-clone apks elsewhere and
  apply the same commit).

## Owner test protocol
1. Title: "Z - Enter / X - Back" (englishmode) should sit at the BOTTOM
   (under EXIT), no longer floating over the menu.
2. Difficulty screen: the selected level's description should show under
   the icons ("Easiest level for Puki" / "Easy level for Beginner" /
   "Standard level for Gamer" / "Hard level for Challenger" /
   "Welcome to HELL!"), above "HIGHSCORE - n".
3. Gallery toolbar: BGM row shows the selected track name; each toolbar
   icon shows its label bottom-right.
4. In-game: chara-select guide blocks ([Z](ground): ... etc.) should
   render at their draw_guide positions (these were also arg-positioned
   at 320,240 = center — expect them to MOVE to their correct spots).

## Note for future sessions
DGDRAWTEXT is the ONLY DG draw that took a position from args; now every
DG draw is retained-state. If a future symptom shows text at the wrong
place, suspect the DGPOS state (who set it last), not DGDRAWTEXT args.
The (C) line at (490,466) with font size 12 renders ~176px wide: 490+176
= 666 > 640, so the PC original CLIPS the right edge of "(C) 2016
D-Gate/ASIMOFU" — expected; the port's text path must clip identically
(it renders into the same 640x480 target with the same coords).
