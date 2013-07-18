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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h> //PRIu64
#include <sys/time.h>
#include <getopt.h>
#include <math.h>

#include "9p_internals.h"
#include "utils.h"
#include "settings.h"


#define DEFAULT_THRNUM 1
#define DEFAULT_PREFIX "bigtree"
#define DEFAULT_WIDTH 64
#define DEFAULT_DEPTH 3
#define DEFAULT_CONFFILE "../sample.conf"

struct thrarg {
	struct p9_handle *p9_handle;
	pthread_mutex_t lock;
	pthread_barrier_t barrier;
	struct timeval end;
	uint32_t numdirs;
	uint32_t numfiles;
	uint32_t depth;
	uint32_t width;
	struct p9_fid *basefid;
};

static int create_tree(struct p9_fid *fid, int depth, int width) {
	int rc = 0, i;
	struct p9_handle *p9_handle = fid->p9_handle;
	struct p9_fid *newfid;
	char name[MAXNAMLEN];

	if (depth > 0) {
		for (i = 0; i < width; i++) {
			snprintf(name, MAXNAMLEN, "dir.%i", i);
			rc = p9l_mkdir(fid, name, 0);
			if (rc && rc != EEXIST) {
				printf("couldn't create directory %s in %s, error %s (%d)\n", name, fid->path, strerror(rc), rc);
				break;
			}

			rc = p9l_walk(p9_handle, fid, name, &newfid, 0);
			if (rc) {
				printf("couldn't go to directory %s in %s, error: %s (%d)\n", name, fid->path, strerror(rc), rc);
				break;
			}
			rc = create_tree(newfid, depth-1, width);
			p9l_clunk(&newfid);
			if (rc)
				break;
		}
	} else {
		for (i = 0; i < width; i++) {
			snprintf(name, MAXNAMLEN, "file.%i", i);
			rc = p9l_openat(p9_handle, fid, name, &newfid, O_CREAT | O_WRONLY, 0, 0);
			p9l_clunk(&newfid);
			if (rc) {
				printf("couldn't create file %s in %s, error: %s (%d)\n", name, fid->path, strerror(rc), rc);
				break;
			}
		}
	}
	
	return rc;
}

static void *createtreethr(void* arg) {
	struct thrarg *thrarg = arg;
	struct p9_handle *p9_handle = thrarg->p9_handle;
	struct p9_fid *fid;
	struct timeval start, end;
	int rc, tmprc;
	char dirname[MAXNAMLEN];

	do {
		/* prepare, etc */
		snprintf(dirname, MAXNAMLEN, "%lx", pthread_self());

		rc = p9l_mkdir(thrarg->basefid, dirname, 0);
		if (rc && rc != EEXIST) {
			printf("couldn't create thread base directory %s in %s, error %s (%d)\n", dirname, thrarg->basefid->path, strerror(rc), rc);
			break;
		}

		rc = p9l_walk(p9_handle, thrarg->basefid, dirname, &fid, 0);
		if (rc) {
			printf("couldn't go to thread base directory %s in %s, error: %s (%d)\n", dirname, thrarg->basefid->path, strerror(rc), rc);
			break;
		}

		/* start creating! */
		pthread_barrier_wait(&thrarg->barrier);
		gettimeofday(&start, NULL);

		create_tree(fid, thrarg->depth, thrarg->width);

		gettimeofday(&end, NULL);
		end.tv_sec = end.tv_sec - start.tv_sec - (start.tv_usec > end.tv_usec ? 1 : 0);
		end.tv_usec = (start.tv_usec > end.tv_usec ? 1000000 : 0 ) +  end.tv_usec - start.tv_usec;	} while (0);

	if (fid) {
		tmprc = p9l_clunk(&fid);
		if (tmprc) {
			printf("clunk failed, rc: %s (%d)\n", strerror(tmprc), tmprc);
		}
	}

	if (end.tv_usec || end.tv_sec)
		printf("Created %d directories and %d files in %lu.%06lus - estimate speed: %lu entries/s\n", thrarg->numdirs, thrarg->numfiles, end.tv_sec, end.tv_usec, ((uint64_t)(thrarg->numdirs+thrarg->numfiles)*1000000)/(end.tv_sec*1000000+end.tv_usec));

	pthread_mutex_lock(&thrarg->lock);
	thrarg->end.tv_sec += end.tv_sec;
	thrarg->end.tv_usec += end.tv_usec;
	pthread_mutex_unlock(&thrarg->lock);

	if (rc)
		printf("thread ended, rc=%d\n", rc);

	pthread_exit(NULL);	
}

static void print_help(char **argv) {
	printf("Usage: %s [-c conf] [-w width] [-d depth] [-b base-dir] [-t thread-num]\n", argv[0]);
	printf(	"Optional arguments:\n"
		"	-t, --threads num: number of operating threads\n"
		"	-c, --conf file: conf file to use\n"
		"	-w, --width width: number of files/dirs per directory\n"
		"	-d, --depth depth: depth of created tree (width dir per)\n"
		"	-b, --base basedir: base dir to use\n");
}

int main(int argc, char **argv) {
	int rc, i;
	char *conffile, *basename;
	pthread_t *thrid;
	int thrnum = 0;
	struct thrarg thrarg;

	thrnum = DEFAULT_THRNUM;
	memset(&thrarg, 0, sizeof(struct thrarg));
	thrarg.depth = DEFAULT_DEPTH;
	thrarg.width = DEFAULT_WIDTH;
	basename = DEFAULT_PREFIX;
	conffile = DEFAULT_CONFFILE;
	pthread_mutex_init(&thrarg.lock, NULL);
	pthread_barrier_init(&thrarg.barrier, NULL, thrnum);

	static struct option long_options[] = {
		{ "conf",	required_argument,	0,		'c' },
		{ "help",	no_argument,		0,		'h' },
		{ "threads",	required_argument,	0,		't' },
		{ "width",	required_argument,	0,		'w' },
		{ "depth",	required_argument,	0,		'd' },
		{ "base",	required_argument,	0,		'b' },
		{ 0,		0,			0,		 0  }
	};

	int option_index = 0;
	int op;

	while ((op = getopt_long(argc, argv, "@c:ht:w:d:b:", long_options, &option_index)) != -1) {
		switch(op) {
			case '@':
				printf("%s compiled on %s at %s\n", argv[0], __DATE__, __TIME__);
				printf("Release = %s\n", VERSION);
				printf("Release comment = %s\n", VERSION_COMMENT);
				printf("Git HEAD = %s\n", _GIT_HEAD_COMMIT ) ;
				printf("Git Describe = %s\n", _GIT_DESCRIBE ) ;
				exit(0);
			case 'h':
				print_help(argv);
				exit(0);
			case 'c':
				conffile = optarg;
				break;
			case 'b':
				basename = optarg;
				break;
			case 't':
				thrnum = atoi(optarg);
				if (thrnum == 0) {
					printf("invalid thread number %s, using default\n", optarg);
					thrnum = DEFAULT_THRNUM;
				}
				break;
			case 'd':
				thrarg.depth = atoi(optarg);
				if (thrarg.depth == 0) {
					printf("invalid depth number %s, using default\n", optarg);
					thrarg.depth = DEFAULT_DEPTH;
				}
				break;
			case 'w':
				thrarg.width = atoi(optarg);
				if (thrarg.width == 0) {
					printf("invalid width number %s, using default\n", optarg);
					thrarg.width = DEFAULT_WIDTH;
				}
				break;
			default:
				ERROR_LOG("Failed to parse arguments");
				print_help(argv);
				exit(EINVAL);
		}
	}

	if (optind < argc) {
		for (i = optind; i < argc; i++)
			printf ("Leftover argument %s\n", argv[i]);
		print_help(argv);
		exit(EINVAL);
	}

	thrid = malloc(sizeof(pthread_t)*thrnum);

        rc = p9_init(&thrarg.p9_handle, conffile);
        if (rc) {
                ERROR_LOG("Init failure: %s (%d)", strerror(rc), rc);
                return rc;
        }

	rc = p9l_mkdir(thrarg.p9_handle->root_fid, basename, 0);
	if (rc && rc != EEXIST) {
		printf("couldn't create base directory %s in /, error %s (%d)\n", basename, strerror(rc), rc);
		return rc;
	}

	rc = p9l_walk(thrarg.p9_handle, thrarg.p9_handle->root_fid, basename, &thrarg.basefid, 0);
	if (rc) {
		printf("couldn't go to base directory %s in /, error: %s (%d)\n", basename, strerror(rc), rc);
		return rc;
	}

	thrarg.numfiles = pow(thrarg.width, thrarg.depth+1);
	thrarg.numdirs = 0;
	for (i = 1; i <= thrarg.depth; i++)
		thrarg.numdirs += pow(thrarg.width, i);

        INFO_LOG(1, "Init success");

	for (i=0; i<thrnum; i++)
		pthread_create(&thrid[i], NULL, createtreethr, &thrarg);

	for (i=0; i<thrnum; i++)
		pthread_join(thrid[i], NULL);

	printf("Total stats:\n");


	if (thrarg.end.tv_sec || thrarg.end.tv_usec)
		printf("Created %d directories and %d files in %lu.%06lus - estimate speed: %lu entries/s\n", thrarg.numdirs*thrnum, thrarg.numfiles*thrnum, thrarg.end.tv_sec/thrnum, (thrarg.end.tv_sec % thrnum)*1000000/thrnum + thrarg.end.tv_usec, ((uint64_t)(thrarg.numdirs+thrarg.numfiles)*thrnum*1000000)/((thrarg.end.tv_sec*1000000+thrarg.end.tv_usec)/thrnum));


	pthread_mutex_destroy(&thrarg.lock);
	pthread_barrier_destroy(&thrarg.barrier);
        p9_destroy(&thrarg.p9_handle);

        return rc;
}
