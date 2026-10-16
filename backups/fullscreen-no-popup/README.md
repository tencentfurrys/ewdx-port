# EchidnaWarsDX Android - fullscreen / no popup build (Oct 8 2026)

What changed vs the original APK:
- Game starts fullscreen (status bar hidden), so the top of the phone doesn't cover anything
- Removed the "Previous run stopped at..." popup and its 10 s wait (boot log still gets written to Downloads)
- Real crash/error popups are untouched
- WAV sounds are zip-compressed inside the APK so it's under 50 MB

Files:
- `EchidnaWarsDX_fullscreen.apk` - the fixed build (signed with a debug key, uninstall the old one first)
- `original/ewdx_original.apk` - the APK before my changes
- `patched-libs/` - the two patched libmain.so files
- `patch_libmain.py` - redoes the patch on a fresh libmain.so if you rebuild from source

No C++ source was touched - the fix was done straight on the compiled libmain.so.
If you rebuild from source, make the same changes there: create the window with
SDL_WINDOW_FULLSCREEN, and drop the boot "previous run" message box + SDL_Delay(10000).

Rebuild steps: swap the libmain.so files into the APK (stored, not compressed),
then sign with uber-apk-signer: `java -jar uber-apk-signer.jar -a your.apk --allowResign`
