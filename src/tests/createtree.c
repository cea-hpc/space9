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


#define DEFAULT_THRNUM 3
#define DEFAULT_PREFIX "bigtree"
#define DEFAULT_DWIDTH 24
#define DEFAULT_FWIDTH 64
#define DEFAULT_DEPTH 2
#define DEFAULT_CONFFILE "../sample.conf"

struct thrarg {
	struct p9_handle *p9_handle;
	pthread_mutex_t lock;
	pthread_barrier_t barrier;
	struct timeval create;
	struct timeval unlink;
	uint64_t numcreate;
	uint64_t numunlink;
	uint32_t depth;
	uint32_t dwidth;
	uint32_t fwidth;
	uint32_t no_unlink;
};

static void *createtreethr(void* arg) {
	struct thrarg *thrarg = arg;
	struct p9_handle *p9_handle = thrarg->p9_handle;
	struct p9_fid *fid;
	struct timeval start, create, unlink;
	int rc = 0;
	ssize_t numcreate = 0, numunlink = 0;
	char dirname[MAXNAMLEN];

	do {
		/* prepare, etc */
		snprintf(dirname, MAXNAMLEN, "%lx", pthread_self());

		/* start creating! */
		pthread_barrier_wait(&thrarg->barrier);
		gettimeofday(&start, NULL);

		numcreate = p9l_createtree(p9_handle, dirname, thrarg->depth, thrarg->dwidth, thrarg->fwidth);

		if (numcreate < 0) {
			rc = -numcreate;
			break;
		}

		gettimeofday(&create, NULL);
		create.tv_sec = create.tv_sec - start.tv_sec - (start.tv_usec > create.tv_usec ? 1 : 0);
		create.tv_usec = (start.tv_usec > create.tv_usec ? 1000000 : 0 ) +  create.tv_usec - start.tv_usec;

		if (thrarg->no_unlink)
			break;

		/* start removing! */
		pthread_barrier_wait(&thrarg->barrier);
		gettimeofday(&start, NULL);

		numunlink = p9l_rmrf(p9_handle, dirname);

		if (numunlink < 0) {
			rc = -numunlink;
			break;
		}

		gettimeofday(&unlink, NULL);
		unlink.tv_sec = unlink.tv_sec - start.tv_sec - (start.tv_usec > unlink.tv_usec ? 1 : 0);
		unlink.tv_usec = (start.tv_usec > unlink.tv_usec ? 1000000 : 0 ) +  unlink.tv_usec - start.tv_usec;
	} while (0);

	if (fid) {
		rc = p9l_clunk(&fid);
		if (rc) {
			printf("clunk failed, rc: %s (%d)\n", strerror(rc), rc);
		}
	}

	if (rc == 0) {
		if (create.tv_usec || create.tv_sec)
			printf("Created %zd files/dirs in %lu.%06lus - estimate speed: %lu entries/s\n" ,numcreate, create.tv_sec, create.tv_usec, (numcreate*1000000)/(create.tv_sec*1000000+create.tv_usec));
		if (unlink.tv_usec || unlink.tv_sec)
			printf("Unlinked %zd files/dirs in %lu.%06lus - estimate speed: %lu entries/s\n" ,numunlink, unlink.tv_sec, unlink.tv_usec, (numunlink*1000000)/(unlink.tv_sec*1000000+unlink.tv_usec));

		pthread_mutex_lock(&thrarg->lock);
		thrarg->numcreate += numcreate;
		thrarg->create.tv_sec += create.tv_sec;
		thrarg->create.tv_usec += create.tv_usec;
		thrarg->numunlink += numunlink;
		thrarg->unlink.tv_sec += unlink.tv_sec;
		thrarg->unlink.tv_usec += unlink.tv_usec;
		pthread_mutex_unlock(&thrarg->lock);
	} else {
		printf("thread failed, rc=%d\n", rc);
	}

	pthread_exit(NULL);	
}

static void print_help(char **argv) {
	printf("Usage: %s [-c conf] [-W dir-width] [-w file-width] [-d depth] [-b base-dir] [-t thread-num]\n", argv[0]);
	printf(	"Optional arguments:\n"
		"	-t, --threads num: number of operating threads\n"
		"	-c, --conf file: conf file to use\n"
		"	-W, --dir-width width: number of dirs per level\n"
		"	-w, --file-width width: number of entries per level\n"
		"	-d, --depth depth: depth of created tree (width dir per)\n"
		"	-n, --no-unlink: don't remove the tree after creating it\n"
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
	thrarg.dwidth = DEFAULT_DWIDTH;
	thrarg.fwidth = DEFAULT_FWIDTH;
	thrarg.no_unlink = 0;
	basename = DEFAULT_PREFIX;
	conffile = DEFAULT_CONFFILE;

	static struct option long_options[] = {
		{ "conf",	required_argument,	0,		'c' },
		{ "help",	no_argument,		0,		'h' },
		{ "threads",	required_argument,	0,		't' },
		{ "dir-width",	required_argument,	0,		'W' },
		{ "file-width",	required_argument,	0,		'w' },
		{ "depth",	required_argument,	0,		'd' },
		{ "base",	required_argument,	0,		'b' },
		{ "no-unlink",	required_argument,	0,		'n' },
		{ 0,		0,			0,		 0  }
	};

	int option_index = 0;
	int op;

	while ((op = getopt_long(argc, argv, "@c:ht:W:w:d:b:n", long_options, &option_index)) != -1) {
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
			case 'W':
				thrarg.dwidth = atoi(optarg);
				if (thrarg.dwidth == 0) {
					printf("invalid dirwidth number %s, using default\n", optarg);
					thrarg.dwidth = DEFAULT_DWIDTH;
				}
				break;
			case 'w':
				thrarg.fwidth = atoi(optarg);
				if (thrarg.fwidth == 0) {
					printf("invalid filewidth number %s, using default\n", optarg);
					thrarg.fwidth = DEFAULT_FWIDTH;
				}
				break;
			case 'n':
				thrarg.no_unlink = 1;
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

	pthread_mutex_init(&thrarg.lock, NULL);
	pthread_barrier_init(&thrarg.barrier, NULL, thrnum + 1);
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

	rc = p9l_cd(thrarg.p9_handle, basename);
	if (rc) {
		printf("couldn't go to base directory %s in /, error: %s (%d)\n", basename, strerror(rc), rc);
		return rc;
	}

        INFO_LOG(1, "Init success");

	for (i=0; i<thrnum; i++)
		pthread_create(&thrid[i], NULL, createtreethr, &thrarg);

	pthread_barrier_wait(&thrarg.barrier);

	printf("Starting %d create_trees with depth %d, dwidth %d, fwidth %d\n", thrnum, thrarg.depth, thrarg.dwidth, thrarg.fwidth);

	pthread_barrier_wait(&thrarg.barrier);

	printf("Starting unlinks\n");

	for (i=0; i<thrnum; i++)
		pthread_join(thrid[i], NULL);

	if (thrarg.create.tv_sec || thrarg.create.tv_usec) {
		thrarg.create.tv_usec = (thrarg.create.tv_usec + (thrarg.create.tv_sec%thrnum)*1000000)/thrnum;
		thrarg.create.tv_sec = thrarg.create.tv_sec/thrnum;
		if (thrarg.create.tv_usec > 1000000) {
			thrarg.create.tv_usec -= 1000000;
			thrarg.create.tv_sec += 1;
		}

		printf("Total stats:\n");
		printf("Created %zd files/dirs in %lu.%06lus - estimate speed: %lu entries/s\n", thrarg.numcreate, thrarg.create.tv_sec, thrarg.create.tv_usec, (thrarg.numcreate*1000000)/((thrarg.create.tv_sec*1000000+thrarg.create.tv_usec)));
	}

	if (thrarg.unlink.tv_sec || thrarg.unlink.tv_usec) {
		thrarg.unlink.tv_usec = (thrarg.unlink.tv_usec + (thrarg.unlink.tv_sec%thrnum)*1000000)/thrnum;
		thrarg.unlink.tv_sec = thrarg.unlink.tv_sec/thrnum;
		if (thrarg.unlink.tv_usec > 1000000) {
			thrarg.unlink.tv_usec -= 1000000;
			thrarg.unlink.tv_sec += 1;
		}

		printf("Unlinked %zd files/dirs in %lu.%06lus - estimate speed: %lu entries/s\n", thrarg.numunlink, thrarg.unlink.tv_sec, thrarg.unlink.tv_usec, (thrarg.numunlink*1000000)/((thrarg.unlink.tv_sec*1000000+thrarg.unlink.tv_usec)));
	}

	pthread_mutex_destroy(&thrarg.lock);
	pthread_barrier_destroy(&thrarg.barrier);
        p9_destroy(&thrarg.p9_handle);

        return rc;
}
