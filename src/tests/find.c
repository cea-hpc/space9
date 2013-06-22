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
#include <mooshika.h>

#include "space9.h"
#include "utils.h"
#include "settings.h"

#include "bucket.h"

#define DEFAULT_THRNUM 10
#define STARTPOINT "sigmund"

struct nlist {
	char name[MAXNAMLEN];
	struct p9_fid *pfid;
	struct nlist *next;
};

struct cb_arg {
	int debug;
	bucket_t *buck;
	struct nlist *tail;
};

static int rd_cb(void *arg, struct p9_handle *p9_handle, struct p9_fid *dfid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	struct cb_arg *cb_arg = arg;
	struct nlist *n;

	/* skip . and .. */
	if (strncmp(name, ".", namelen) == 0 || strncmp(name, "..", namelen) == 0)
		return 0;

	if (cb_arg->debug)
		printf("%.*s\n", namelen, name);

	if (qid->type == P9_QTDIR) {
		n = bucket_get(cb_arg->buck);
		if (n == NULL)
			return ENOMEM;

		strncpy(n->name, name, MIN(MAXNAMLEN, namelen));
		if (namelen < MAXNAMLEN) {
			n->name[namelen] = '\0';
		} else {
			n->name[MAXNAMLEN-1] = '\0';
		}
		n->pfid = dfid;

		n->next = cb_arg->tail;
		cb_arg->tail = n;
	}

	return 0;
}

static void *walkthr(void* arg) {
	struct p9_handle *p9_handle = arg;
	struct p9_fid *fid;
	int rc;
	uint64_t offset;
	bucket_t *buck;
	struct cb_arg cb_arg;
	struct nlist *nlist;

	buck = bucket_init(100, sizeof(struct nlist));
	cb_arg.buck = buck;
	cb_arg.debug = p9_handle->debug;
	cb_arg.tail = bucket_get(buck);
	strncpy(cb_arg.tail->name, STARTPOINT, MAXNAMLEN);
	cb_arg.tail->next = NULL;
	cb_arg.tail->pfid = p9_handle->root_fid;

	rc = 0;
	fid = cb_arg.tail->pfid;

	while (rc == 0 && cb_arg.tail != NULL) {
		if (cb_arg.tail->name[0] == '\0') {
			p9p_clunk(p9_handle, cb_arg.tail->pfid);
			nlist = cb_arg.tail;
			cb_arg.tail = cb_arg.tail->next;
			bucket_put(buck, nlist);
			continue;
		}

		rc = p9p_walk(p9_handle, cb_arg.tail->pfid, cb_arg.tail->name, &fid);
		if (rc) {
			printf("walk failed, rc: %s (%d)\n", strerror(rc), rc);
			break;
		}

		cb_arg.tail->name[0] = '\0';
		cb_arg.tail->pfid = fid;

		offset = 0LL;
		do {
			rc = p9p_readdir(p9_handle, fid, &offset, rd_cb, &cb_arg);
		} while (rc > 0);

		if (rc) {
			rc = -rc;
			printf("readdir failed, rc: %s (%d)\n", strerror(rc), rc);
			break;
		}
	}

	if (rc)
		printf("thread ended, rc=%d\n", rc);

	pthread_exit(NULL);	
}


int main(int argc, char **argv) {
	int rc, i;
	struct p9_handle *p9_handle;

	pthread_t *thrid;
	int thrnum = 0;

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

	for (i=0; i<thrnum; i++)
		pthread_create(&thrid[i], NULL, walkthr, p9_handle);

	for (i=0; i<thrnum; i++)
		pthread_join(thrid[i], NULL);

        p9_destroy(&p9_handle);

        return rc;
}
