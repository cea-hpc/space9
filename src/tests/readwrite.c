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
#include <mooshika.h>

#include "9p.h"
#include "9p_proto.h"
#include "utils.h"
#include "settings.h"


#define DEFAULT_THRNUM 1
#define CHUNKSIZE (1024*1024-P9_ROOM_TWRITE)
#define TOTALSIZE 2*1024*1024*1024L
#define FILENAME "readwrite"

struct thrarg {
	struct p9_handle *p9_handle;
	pthread_mutex_t lock;
	pthread_barrier_t barrier;
	struct timeval write;
	struct timeval read;
};

static void *readwritethr(void* arg) {
	struct thrarg *thrarg = arg;
	struct p9_handle *p9_handle = thrarg->p9_handle;
	struct p9p_setattr attr;
	struct p9_fid *fid;
	struct timeval start, write, read;
	int rc, tmprc;
	uint64_t offset;
	msk_data_t *data;
	char *zbuf;
	char buffer[CHUNKSIZE];
	memset(buffer, 0x61626364, CHUNKSIZE);
	char filename[MAXNAMLEN];
	snprintf(filename, MAXNAMLEN, "%s_%lx", FILENAME, pthread_self());


	do {
		/* get a fid to write in */
		rc = p9p_walk(p9_handle, p9_handle->root_fid, filename, &fid);
		if (rc) {
			/* file doesn't exist, create it */
			rc = p9p_walk(p9_handle, p9_handle->root_fid, NULL, &fid);
			if (rc) {
				printf("couldn't clone root fid?! error: %s (%d)\n", strerror(rc), rc);
				break;
			}
			rc = p9p_lcreate(p9_handle, fid, filename, O_RDWR, 0640, 0, NULL);
			if (rc) {
				printf("couldn't create file %s in dir %s. error: %s (%d)\n", filename, fid->path, strerror(rc), rc);
				break;
			}
		} else {
			/* found file, open and truncate it */
			rc = p9p_lopen(p9_handle, fid, O_RDWR, NULL);
			if (rc) {
				printf("couldn't open file %s. error: %s (%d)\n", fid->path, strerror(rc), rc);
				break;
			}
			memset(&attr, 0, sizeof(attr));
			attr.valid = P9_SETATTR_SIZE;
			attr.size = 0;
			rc = p9p_setattr(p9_handle, fid, &attr);
			if (rc) {
				printf("couldn't truncate file %s. error: %s (%d)\n", fid->path, strerror(rc), rc);
				break;
			}
		}

		/* write */

		pthread_barrier_wait(&thrarg->barrier);
		gettimeofday(&start, NULL);
		offset = 0LL;
		data = malloc(sizeof(msk_data_t));
		data->data = (uint8_t*)buffer;
		data->size = CHUNKSIZE;
		data->max_size = CHUNKSIZE;
		p9c_reg_mr(p9_handle, data);
		do {
			/* rc = p9p_write(p9_handle, fid, offset, MIN(CHUNKSIZE, (uint32_t)(TOTALSIZE-offset)), buffer); */
			if (TOTALSIZE-offset < CHUNKSIZE)
				data->size = TOTALSIZE-offset;
			rc = p9pz_write(p9_handle, fid, offset, data);
			if (rc < 0)
				break;
			offset += rc;
		} while (rc > 0 && TOTALSIZE > offset);
		p9c_dereg_mr(data);
		free(data);
		if (rc < 0) {
			rc = -rc;
			printf("write failed at offset %"PRIu64", error: %s (%d)\n", offset, strerror(rc), rc);
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
		offset = 0LL;
		do {
			/* rc = p9p_read(p9_handle, fid, offset, MIN(CHUNKSIZE, (uint32_t)(TOTALSIZE-offset)), buffer); */

			rc = p9pz_read(p9_handle, fid, offset, ((TOTALSIZE-offset < CHUNKSIZE) ? TOTALSIZE-offset : CHUNKSIZE), &zbuf, &data);
			if (rc < 0)
				break;

			p9c_putreply(p9_handle, data);
			offset += rc;
		} while (rc > 0 && TOTALSIZE > offset);
		if (rc < 0) {
			rc = -rc;
			printf("read failed at offset %"PRIu64", error: %s (%d)\n", offset, strerror(rc), rc);
			break;
		} else
			rc = 0;
		gettimeofday(&read, NULL);
		read.tv_sec = read.tv_sec - start.tv_sec - (start.tv_usec > read.tv_usec ? 1 : 0);
		read.tv_usec = (start.tv_usec > read.tv_usec ? 1000000 : 0 ) +  read.tv_usec - start.tv_usec;
	} while (0);

	if (fid) {
		tmprc = p9p_clunk(p9_handle, fid);
		if (tmprc) {
			printf("clunk failed, rc: %s (%d)\n", strerror(tmprc), tmprc);
		}
		tmprc = p9p_unlinkat(p9_handle, p9_handle->root_fid, filename, 0);
		if (tmprc) {
			printf("unlinkat failed, rc: %s (%d)\n", strerror(tmprc), tmprc);
		}
	}


	if (write.tv_usec || write.tv_sec)
		printf("Wrote %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", TOTALSIZE/1024/1024, write.tv_sec, write.tv_usec, TOTALSIZE/(write.tv_sec*1000000+write.tv_usec));
	if (read.tv_usec || read.tv_sec)
		printf("Read  %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", TOTALSIZE/1024/1024, read.tv_sec, read.tv_usec, TOTALSIZE/(read.tv_sec*1000000+read.tv_usec)*1000*1000/1024/1024);

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


int main(int argc, char **argv) {
	int rc, i;
	struct p9_handle *p9_handle;

	pthread_t *thrid;
	int thrnum = 0;
	struct thrarg thrarg;

	if (argc >= 2) {
		thrnum=atoi(argv[1]);
	}
	if (thrnum == 0) {
		thrnum = DEFAULT_THRNUM;
	}

	thrid = malloc(sizeof(pthread_t)*thrnum);

        rc = p9_init(&p9_handle, "../sample.conf");
        if (rc) {
                ERROR_LOG("Init failure: %s (%d)", strerror(rc), rc);
                return rc;
        }

        INFO_LOG(1, "Init success");

	memset(&thrarg, 0, sizeof(struct thrarg));
	thrarg.p9_handle = p9_handle;
	pthread_mutex_init(&thrarg.lock, NULL);
	pthread_barrier_init(&thrarg.barrier, NULL, thrnum);

	for (i=0; i<thrnum; i++)
		pthread_create(&thrid[i], NULL, readwritethr, &thrarg);

	for (i=0; i<thrnum; i++)
		pthread_join(thrid[i], NULL);

	printf("Total stats:\n");

	if (thrarg.write.tv_sec || thrarg.write.tv_usec)
		printf("Wrote %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", thrnum*TOTALSIZE/1024/1024, thrarg.write.tv_sec/thrnum, thrarg.write.tv_usec/thrnum, thrnum*TOTALSIZE/((thrarg.write.tv_sec*1000000+thrarg.write.tv_usec)/thrnum));
	if (thrarg.read.tv_sec || thrarg.read.tv_usec)
		printf("Read  %"PRIu64"MB in %lu.%06lus - estimate speed: %luMB/s\n", thrnum*TOTALSIZE/1024/1024, thrarg.read.tv_sec/thrnum, thrarg.read.tv_usec/thrnum, thrnum*TOTALSIZE/((thrarg.read.tv_sec*1000000+thrarg.read.tv_usec)/thrnum)*1000*1000/1024/1024);

	pthread_mutex_destroy(&thrarg.lock);
	pthread_barrier_destroy(&thrarg.barrier);
        p9_destroy(&p9_handle);

        return rc;
}
