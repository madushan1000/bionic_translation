#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OPEN_NEEDS_MODE(flags) ((flags & O_CREAT) || (flags & O_TMPFILE))

/*
 * droidlet runs from an install tree, not from /system.  Bionic libraries
 * (Gecko, Flutter/Skia, fontconfig) still open hardcoded /system paths for
 * fonts and configuration.  Redirect them to $ANDROID_ROOT when that points
 * anywhere other than /system.
 */
static bool apply_path_overrides(char **path) {
	const char *root = getenv("ANDROID_ROOT");
	const char *requested = path ? *path : NULL;
	if (!root || !*root || !strcmp(root, "/system") || !requested) {
		return false;
	}
	if (strncmp(requested, "/system", 7) != 0) {
		return false;
	}
	if (requested[7] != '\0' && requested[7] != '/') {
		return false;
	}

	const char *tail = requested + 7; /* "" or "/..." */
	const size_t root_len = strlen(root);
	const size_t tail_len = strlen(tail);
	char *replacement = (char *)malloc(root_len + tail_len + 1);
	if (!replacement) {
		return false;
	}
	memcpy(replacement, root, root_len);
	memcpy(replacement + root_len, tail, tail_len + 1);
	*path = replacement;
	return true;
}

typedef bool (apply_path_overrides_func_type)(char **);
static apply_path_overrides_func_type *apply_path_overrides_func = apply_path_overrides;

/* call this to set an overrides function */
void libc_bio_set_apply_path_overrides_func(apply_path_overrides_func_type *func)
{
	apply_path_overrides_func = func;
}

/* open and fopen overrides */
int bionic_open(char *path, int oflag, ...)
{
	int fd;
	int mode;

	bool free_path = (*apply_path_overrides_func)(&path);

	// Hide TracerPid from /proc/self/status for hideous apps that check for debugger.
	// Note, since /proc/self/status doesn't get updated anymore, this may break some stuff.
	// XXX: Turn this ON/OFF with env var maybe?
	if (!strcmp(path, "/proc/self/status")) {
		static FILE *faked = NULL;

		if (!faked) {
			static char status[4096];

			{
				FILE *f = fopen(path, "rb");
				assert(f && "/proc/self/status failed to open :/");
				const size_t ret = fread(status, 1, sizeof(status), f);
				assert(ret <= sizeof(status) && "/proc/self/status doesn't fit in 4096 bytes :/");
				fclose(f);
			}

			for (char *s, *e; (s = strstr(status, "TracerPid:\t"));) {
				for (e = s; (size_t)(e - status) < sizeof(status) && *e && *e != '\n'; ++e)
					;
				memmove(s, e, sizeof(status) - (e - status));
				break;
			}

			faked = fmemopen(status, sizeof(status), "rb");
			assert(faked && "fmemopen failed :/");
		}

		return fileno(faked);
	}

	if(OPEN_NEEDS_MODE(oflag)) {
		va_list arg;
		va_start(arg, oflag);
		mode = va_arg(arg, int);
		va_end(arg);

		fd = open(path, oflag, mode);
	} else {
		fd = open(path, oflag);
	}

	if(free_path)
		free(path);

	return fd;
}

FILE *bionic_fopen(char *path, const char *restrict mode)
{
	FILE *file;
	bool free_path = (*apply_path_overrides_func)(&path);

	file = fopen(path, mode);
	if(free_path)
		free(path);
	return file;
}
