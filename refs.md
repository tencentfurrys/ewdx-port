# Reference pins (re-clone to reproduce this work)

## Source checkouts (working copies NOT backed up — clone + checkout these SHAs)

| Repo | URL | SHA / branch | Date |
|---|---|---|---|
| OpenHSP | https://github.com/onitama/OpenHSP | `3dbb8729dad53ee876fc681696c8fe542698860b` (master) | 2026-04-21 |
| SDL2 | https://github.com/libsdl-org/SDL | `b90ac95029d801c5abc59472ba8e2200dff31e1e` (branch `SDL2`, `--depth 1`) | 2026-09-12 |
| SDL_ttf | https://github.com/libsdl-org/SDL_ttf | `7f16032e523f037d700c1e3eb545fb6c53436a9e` (branch `SDL2`, `--depth 1`) + submodules `external/freetype@535d299`, `external/harfbuzz@950d232` (`submodule update --init`) | 2026-09-12 |
| deHSP | https://github.com/SoulMelody/deHSP | `3e6a2111598f6f9caeaf423531ccc7bcd9b89693` (master) | 2026-09-12 |

Key in-tree paths used:
- `OpenHSP/src/hsp3/` — VM core (`hsp3code.cpp`, `hsp3int.cpp`, `hsp3struct.h`)
- `OpenHSP/src/plugins/win32/hspda/Hspda.cpp` — `hspv` serializer (the format bible)
- `OpenHSP/src/hsp3dish/` — cross-platform runtime base (linux/ndk subdirs)
- `OpenHSP/src/hsp3/linux/hsp3extlib_ffi.cpp` — `Hsp3ExtAddPlugin` TYPEFUNC model for the ovplay hook
- `SDL2/include/` — headers used for the NDK syntax gate
- `SDL_ttf/SDL_ttf.h` (+ `external/{freetype,harfbuzz}` submodules) — text build

## SDK / NDK pins

- `ANDROID_SDK_ROOT = C:\Android\android-sdk`, NDK `27.3.13750724`
- Platforms 34–37.x, build-tools ≤37.0.0, JDK 21, Ghidra 11.3.2, .NET 8.0.425

## Game source (NOT redistributed)

- `Echidna_Wars_DX_V1.11_ENG-JAP.zip` (49,949,106 B) + extracted tree —
  keep the original zip; this repo holds only RE work products.
