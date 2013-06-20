#include <mooshika.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "9p.h"
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
