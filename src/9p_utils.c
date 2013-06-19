#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <assert.h>
#include <mooshika.h>
#include "9p.h"
#include "utils.h"
#include "settings.h"

static inline void strmove(char *dest, const char *src, size_t n) {
	if (dest != src)
		/* strncpy doesn't allow overlap, so use memmove */
		memmove(dest, src, n);
}

/**
 * @brief canonicalize path
 * 
 * @param path to canonicalize, must be null terminated
 * @return length of new path if >= 0, -errno on error
 */
int path_canonicalizer(char *path) {

	char *cur, *new, *slash;
	size_t n;

	/* absolute path? */
	if (path[0] == '/') {
		cur = path + 1;
		new = path + 1;
	} else {
		cur = path;
		new = path;
	}

	while (cur[0] != '\0') {
		if (cur[0] == '/') {
			/* foo//bar -> foo/bar */
			cur += 1;
		} else if (cur[0] == '.') {
			if (cur[1] == '/') {
				/* foo/./bar -> foo/bar */
				cur += 2;
			} else if (cur[1] == '.' && (cur[2] == '/' || cur[2] == '\0')) {
				/* ../, need to check what we have so far */
				n = 3;
				if (new == path) {
					/* starts with ../, keep it */
					strmove(new, cur, n);
					new += n;
				} else if (new == path + 1) {
					/* /../ -> / */
					/* nothing to do */
				} else {
					new[-1] = '\0'; /* for strrchr.. recode it? */
					slash = strrchr(path, '/');
					new[-1] = '/';
					if (slash == NULL) {
						/* starts with foo/../ -> empty new */
						new = path;
					} else if (slash == path) {
						/* /foo/../ -> / */
						new = path + 1;
					} else if (slash[-1] == '.' && slash[-2] == '.' && (slash-2 == path || slash[-3] == '/')) {
						/* Since / isn't path, the smallest possible position for it would be path[2] in "/a/foo", so slash[-1] and slash[-2] are ok to use */
						/* /../../ there's already a ../, so keep this one as well */
						strmove(new, cur, n);
						new += n;
					} else {
						/* *foo/bar/../foo -> *foo/foo */
						new = slash + 1;
					}
				}
				cur += n;
			} else {
				/* ... or .foo or whatever that isn't special */
				slash = strchr(cur, '/');
				if (slash == NULL) {
					/* trail of path, also copy final \0 */
					n = strlen(cur) + 1;
				} else {
					/* copy up to slash included */
					n = slash - cur + 1;
				}
				strmove(new, cur, n);
				new += n;
				cur += n;
			}
		} else {
			/* ... or .foo or whatever that isn't special */
			slash = strchr(cur, '/');
			if (slash == NULL) {
				/* trail of path, without final \0 */
				n = strlen(cur);
			} else {
				/* copy up to slash included */
				n = slash - cur + 1;
			}
			strmove(new, cur, n);
			new += n;
			cur += n;
		}
	}
	new[0] = '\0';

	return (int)(new - path);
}


int path_basename(char *path, char *dst, size_t dst_len) {
	char *slash;

	slash = strrchr(path, '/');
	if (slash == NULL) {
		slash = path;
	} else {
		slash += 1;
	}

	strncpy(dst, slash, dst_len);

	return strlen(dst);
}


int path_dirname(char *path, char *dst, size_t dst_len) {
	char *slash;

	slash = strrchr(path, '/');
	if (slash == NULL) {
		strncpy(dst, ".", dst_len);
	} else {
		slash[0] = '\0';
		strncpy(dst, path, dst_len);
		slash[0] = '/';
	}

	return strlen(dst);
}

void path_split(char *path, char **dirname, char **basename) {
	char *slash;

	slash = strrchr(path, '/');
	if (slash == NULL) {
		*basename = path;
		*dirname = path + strlen(path); /* the final \0 */
	} else {
		slash[0] = '\0';
		*dirname = path;
		*basename = slash+1;
	}
}
