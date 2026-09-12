# APK asset staging (step 3).
#
# At assemble time, populate this directory EXACTLY as:
#   assets/start.ax     <- artifacts/start_ax_dump.bin (764,226 B, HSP3 v3.01)
#   assets/data/map/    <- game data/map/*.map    (21 files)
#   assets/data/mold/   <- game data/mold/*.mol   (62 files)
#   assets/data/mot/    <- game data/mot/*.mot    (138 files)
#   assets/data/music/  <- game data/music/*.ogg  (10 files)
#   assets/data/pic/    <- game data/pic/*.bmp    (106 files)
#   assets/data/se/     <- game data/se/*.wav     (253 files)
#   assets/save.dat     <- game save.dat
#
# Total ~100.4 MB. First launch copies assets -> filesDir so the script's
# relative paths (data\pic\..., save.dat, dir_c==0 branch) resolve unchanged.
#
# VERIFIED (verify_refs.py + verify_picpath.py):
#   - all 33 distinct map dio pic refs resolve to data/pic/*.bmp (0 missing)
#   - all 44 mot pic-var refs resolve to data/pic/*.bmp (0 missing)
#   - 78 pic / 62 mold / 32 mot / 115 se cross-refs resolve to on-disk pools
#   - 29 t_obj_w orphans are internal mold slot labels, never file paths
#
# Source tree (do NOT duplicate 100MB here during development):
#   C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]\
