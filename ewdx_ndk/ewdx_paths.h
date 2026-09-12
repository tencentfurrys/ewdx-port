// ewdx_paths.h - filesDir-anchored storage + APK-asset fallback reads.
//
// The script uses Win32-relative paths ("save.dat", "data\se\x.wav").
// On Android those resolve against the app filesDir (<app filesDir>/save.dat
// for progress persistence; first launch bootstraps data/ there from APK
// assets). Reads additionally fall back to APK assets via SDL_RWops so
// bundled content works before/after bootstrap. Host builds resolve
// relative to the working directory.
#ifndef __EWDX_PATHS_H
#define __EWDX_PATHS_H

#include <stddef.h>
#include <stdint.h>

// Call once after SDL_Init (caches filesDir; harmless to repeat).
void ewdx_paths_init(void);
// App filesDir, or "" on host.
const char *ewdx_files_dir(void);
// Resolve an HSP-style path into out (backslash->slash; filesDir-joined
// unless already absolute). Always NUL-terminates.
void ewdx_resolve(const char *hsp_path, char *out, size_t cap);
// Read a whole HSP-style file (filesDir fopen, then APK-asset SDL_RWops).
// Returns malloc'd bytes or NULL. 32 MB cap.
uint8_t *ewdx_read_file(const char *hsp_path, long *len_out);

#endif
