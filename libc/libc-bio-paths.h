#pragma once

/*
 * Translate an Android /system path to $ANDROID_ROOT.  Returns a malloc'd
 * string when translation is needed, or NULL when the caller should use the
 * original path.  The caller frees the returned string.
 */
char *libc_bio_translate_path(const char *path);
