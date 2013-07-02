/*
 * Copyright CEA/DAM/DIF (2013)
 * Contributor: Dominique Martinet <dominique.martinet@cea.fr>
 *
 * This file is part of the space9 9P userspace library.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with space9.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <assert.h>
#include "9p_internals.h"
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


void path_basename(char *path, char **basename) {
	char *slash;

	slash = strrchr(path, '/');
	if (slash == NULL) {
		*basename = path;
	} else {
		*basename = slash + 1;
	}
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

int path_split(char *path, char **dirname, char **basename) {
	char *slash;
	int relative;

	relative = (path[0] != '/');

	slash = strrchr(path, '/');
	if (slash == NULL) {
		*basename = path;
		*dirname = path + strlen(path); /* the final \0 */
	} else {
		slash[0] = '\0';
		*dirname = path;
		*basename = slash+1;
	}

	return relative;
}
