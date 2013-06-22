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

#include <mooshika.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "space9.h"
#include "utils.h"

int main(int argc, char **argv) {

	char path[MAXPATHLEN], *dirname, *basename;
	int i;

	if (argc > 1) {
		strncpy(path, argv[1], MAXPATHLEN);
	} else {
		strcpy(path, "/foo/bar");
	}
	i = path_canonicalizer(path);
	printf("i: %i, path: %s\n", i, path);
	i = path_split(path, &dirname, &basename);
	printf("relative: %i, dirname: %s, basename: %s\n", i, dirname, basename);

	return 0;
}
