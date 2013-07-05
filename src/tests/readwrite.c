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

#include "9p_internals.h"
#include "utils.h"
#include "settings.h"


#define DEFAULT_THRNUM 1
#define DEFAULT_CHUNKSIZE (8*(1024*1024-P9_ROOM_TWRITE))
#define DEFAULT_TOTALSIZE 2*1024*1024*1024L
#define DEFAULT_FILENAME "readwrite"
#define DEFAULT_CONFFILE "../sample.conf"

struct thrarg {
	struct p9_handle *p9_handle;
	pthread_mutex_t lock;
	pthread_barrier_t barrier;
	struct timeval write;
	struct timeval read;
	uint32_t chunksize;
	uint64_t totalsize;
	char *basename;
};

static void *readwritethr(void* arg) {
	struct thrarg *thrarg = arg;
	struct p9_handle *p9_handle = thrarg->p9_handle;
	struct p9_fid *fid;
	struct timeval start, write, read;
	int rc, tmprc;
	char *buffer;

	buffer = malloc(thrarg->chunksize);

	if (!buffer) {
		printf("could not allocate buffer\n");
		exit(1);
	}

	memset(buffer, 0x61626364, thrarg->chunksize);
	char filename[MAXNAMLEN];
	snprintf(filename, MAXNAMLEN, "%s_%lx", thrarg->basename, pthread_self());


	do {
		/* get a fid to write in */
		rc = p9l_open(p9_handle, filename, &fid, O_CREAT|O_TRUNC|O_RDWR, 0640, 0);
		if (rc) {
			printf("couldn't open file %s, error: %s (%d)\n", filename, strerror(rc), rc);
			break;
		}

		/* write */

		pthread_barrier_wait(&thrarg->barrier);
		gettimeofday(&start, NULL);
		p9l_fseek(fid, 0, SEEK_SET);
		do {
			rc = p9l_write(fid, buffer, thrarg->chunksize);
		} while (rc > 0 && thrarg->totalsize > fid->offset);
		if (rc < 0) {
			rc = -rc;
			printf("write failed at offset %"PRIu64", error: %s (%d)\n", fid->offset, strerror(rc), rc);
			break;
		}
		rc = p9p_fsync(p9_handle, fid);
		if (rc) {
			printf("couldn't fsync file %s. error: %s (%d)\n", fid->path, strerror(rc), rc);
			break;
		}
	
		gettimeofday(&write, NULL);
		write.tv_sec = write.tv_sec - start.tv_sec - (start.tv_usec > write.tv_usec ? 1 : 0);
		write.tv_usec = (start.tv_usec > write.tv_usec ? 1000000 : 0 ) +  write.tv_usec - start.tv_usec;

		/* read */
		pthread_barrier_wait(&thrarg->barrier);
		gettimeofday(&start, NULL);
		p9l_fseek(fid, 0, SEEK_SET);
		do {
			rc = p9l_read(fid, buffer, thrarg->chunksize);
		} while (rc > 0 && thrarg->totalsize > fid->offset);
		if (rc < 0) {
			rc = -rc;
			printf("read failed at offset %"PRIu64", error: %s (%d)\n", fid->offset, strerror(rc), rc);
			break;
		} else
			rc = 0;
		gettimeofday(&read, NULL);
		read.tv_sec = read.tv_sec - start.tv_sec - (start.tv_usec > read.tv_usec ? 1 : 0);
		read.tv_usec = (start.tv_usec > read.tv_usec ? 1000000 : 0 ) +  read.tv_usec - start.tv_usec;
	} while (0);

	if (fid) {
		tmprc = p9p_clunk(p9_handle, &fid);
		if (tmprc) {
			printf("clunk failed, rc: %s (%d)\n", strerror(tmprc), tmprc);
		}
		tmprc = p9p_unlinkat(p9_handle, p9_handle->root_fid, filename, 0);
		if (tmprc) {
			printf("unlinkat failed, rc: %s (%d)\n", strerror(tmprc), tmprc);
		}
	}

	free(buffer);

	if (write.tv_usec || write.tv_sec)
		printf("Wrote %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", thrarg->totalsize/1024/1024, write.tv_sec, write.tv_usec, thrarg->totalsize/(write.tv_sec*1000000+write.tv_usec));
	if (read.tv_usec || read.tv_sec)
		printf("Read  %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", thrarg->totalsize/1024/1024, read.tv_sec, read.tv_usec, thrarg->totalsize/(read.tv_sec*1000000+read.tv_usec)*1000*1000/1024/1024);

	pthread_mutex_lock(&thrarg->lock);
	thrarg->write.tv_sec += write.tv_sec;
	thrarg->write.tv_usec += write.tv_usec;
	thrarg->read.tv_sec += read.tv_sec;
	thrarg->read.tv_usec += read.tv_usec;
	pthread_mutex_unlock(&thrarg->lock);

	if (rc)
		printf("thread ended, rc=%d\n", rc);

	pthread_exit(NULL);	
}

static void print_help(char **argv) {
	printf("Usage: %s [-c conf] [-s chunk-size] [-S file-size] [-f filename] [-t thread-num]\n", argv[0]);
	printf(	"Optional arguments:\n"
		"	-t, --threads num: number of operating threads\n"
		"	-p, --pipeline num: number of simultaneous requests on a single file (overwrites conf)\n"
		"	-c, --conf file: conf file to use\n"
		"	-s, --chunk[-size] size: chunk size to use, default is optimal based on msize\n"
		"	-S, --filesize size: size of the created files\n"
		"	-f, --filename name: prefix to use for files\n");
}

int main(int argc, char **argv) {
	int rc, i;
	char *conffile;
	pthread_t *thrid;
	int thrnum = 0;
	int pipeline;
	struct thrarg thrarg;

	thrnum = DEFAULT_THRNUM;
	memset(&thrarg, 0, sizeof(struct thrarg));
	thrarg.chunksize = DEFAULT_CHUNKSIZE;
	thrarg.totalsize = DEFAULT_TOTALSIZE;
	thrarg.basename = DEFAULT_FILENAME;
	pipeline = 0;
	conffile = DEFAULT_CONFFILE;
	pthread_mutex_init(&thrarg.lock, NULL);
	pthread_barrier_init(&thrarg.barrier, NULL, thrnum);

	static struct option long_options[] = {
		{ "conf",	required_argument,	0,		'c' },
		{ "chunk-size",	required_argument,	0,		's' },
		{ "chunk",	required_argument,	0,		's' },
		{ "filesize",	required_argument,	0,		'S' },
		{ "filename",	required_argument,	0,		'f' },
		{ "help",	no_argument,		0,		'h' },
		{ "threads",	required_argument,	0,		't' },
		{ "pipeline",	required_argument,	0,		'p' },
		{ 0,		0,			0,		 0  }
	};

	int option_index = 0;
	int op;

	while ((op = getopt_long(argc, argv, "@c:s:S:f:hp:t:", long_options, &option_index)) != -1) {
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
			case 's':
				thrarg.chunksize = strtol(optarg, &optarg, 10);
				if (set_size(&thrarg.chunksize, optarg) || thrarg.chunksize == 0) {
					printf("invalid chunksize %s, using default\n", optarg);
					thrarg.chunksize = DEFAULT_CHUNKSIZE;
				}
				break;
			case 'S':
				thrarg.totalsize = strtol(optarg, &optarg, 10);
				if (set_size64(&thrarg.totalsize, optarg) || thrarg.totalsize == 0) {
					printf("invalid totalsize %s, using default\n", optarg);
					thrarg.totalsize = DEFAULT_TOTALSIZE;
				}
				break;
			case 'c':
				conffile = optarg;
				break;
			case 'f':
				thrarg.basename = optarg;
				break;
			case 't':
				thrnum = atoi(optarg);
				if (thrnum == 0) {
					printf("invalid thread number %s, using default\n", optarg);
					thrnum = DEFAULT_THRNUM;
				}
				break;
			case 'p':
				pipeline = atoi(optarg);
				if (pipeline == 0) {
					printf("invalid pipeline number %s, using conf value\n", optarg);
					pipeline = 0;
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

	if (pipeline)
		thrarg.p9_handle->pipeline = pipeline;

        INFO_LOG(1, "Init success");

	for (i=0; i<thrnum; i++)
		pthread_create(&thrid[i], NULL, readwritethr, &thrarg);

	for (i=0; i<thrnum; i++)
		pthread_join(thrid[i], NULL);

	printf("Total stats:\n");

	if (thrarg.write.tv_sec || thrarg.write.tv_usec)
		printf("Wrote %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", thrnum*thrarg.totalsize/1024/1024, thrarg.write.tv_sec/thrnum, thrarg.write.tv_usec/thrnum, thrnum*thrarg.totalsize/((thrarg.write.tv_sec*1000000+thrarg.write.tv_usec)/thrnum));
	if (thrarg.read.tv_sec || thrarg.read.tv_usec)
		printf("Read  %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", thrnum*thrarg.totalsize/1024/1024, thrarg.read.tv_sec/thrnum, thrarg.read.tv_usec/thrnum, thrnum*thrarg.totalsize/((thrarg.read.tv_sec*1000000+thrarg.read.tv_usec)/thrnum)*1000*1000/1024/1024);

	pthread_mutex_destroy(&thrarg.lock);
	pthread_barrier_destroy(&thrarg.barrier);
        p9_destroy(&thrarg.p9_handle);

        return rc;
}
