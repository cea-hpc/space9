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
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <unistd.h>     // gethostname
#include "9p_internals.h"
#include "9p_tcp.h"
#include "utils.h"
#include "settings.h"

struct p9_conf {
	char aname[MAXPATHLEN];
	uint32_t uid;
	uint32_t recv_num;
	uint32_t max_fid;
	uint32_t max_tag;
	uint32_t msize;
	uint32_t pipeline;
	uint32_t debug;
	struct p9_net_ops *net_ops;
	struct msk_trans_attr trans_attr;
};

enum conftype { 
	INT,
	UINT,
	IP,
	IP6,
	PORT,
	PORT6,
	STRING,
	SIZE,
	NET_TYPE
};

struct conf {
	char *token;
	enum conftype type;
	int offset;
};

#define offsetof(type, member)  __builtin_offsetof (type, member)

#if HAVE_MOOSHIKA
static char *p9_net_rdma_s = "rdma";
static struct p9_net_ops p9_rdma_ops = {
	.init = msk_init,
	.destroy_trans = msk_destroy_trans,
	.connect = msk_connect,
	.finalize_connect = msk_finalize_connect,
	.reg_mr = msk_reg_mr,
	.dereg_mr = msk_dereg_mr,
	.post_n_send = msk_post_n_send,
	.post_n_recv = msk_post_n_recv,
};
#endif

static char *p9_net_tcp_s = "tcp";
static struct p9_net_ops p9_tcp_ops = {
	.init = msk_tcp_init,
	.destroy_trans = msk_tcp_destroy_trans,
	.connect = msk_tcp_connect,
	.finalize_connect = msk_tcp_finalize_connect,
	.reg_mr = msk_tcp_reg_mr,
	.dereg_mr = msk_tcp_dereg_mr,
	.post_n_send = msk_tcp_post_n_send,
	.post_n_recv = msk_tcp_post_n_recv,
};

static struct conf conf_array[] = {
	{ "server", IP, 0 },
	{ "port", PORT, 0 },
	{ "server6", IP6, 0 },
	{ "port6", PORT6, 0 },
	{ "rdma_debug", UINT, offsetof(struct p9_conf, trans_attr) + offsetof(struct msk_trans_attr, debug) },
	{ "debug", UINT, offsetof(struct p9_conf, debug) },
	{ "msize", SIZE, offsetof(struct p9_conf, msize) },
	{ "recv_num", UINT, offsetof(struct p9_conf, trans_attr) + offsetof(struct msk_trans_attr, rq_depth)  },
	{ "rq_depth", UINT, offsetof(struct p9_conf, trans_attr) + offsetof(struct msk_trans_attr, rq_depth) },
	{ "sq_depth", UINT, offsetof(struct p9_conf, trans_attr) + offsetof(struct msk_trans_attr, sq_depth) },
	{ "aname", STRING, offsetof(struct p9_conf, aname) },
	{ "uid", UINT, offsetof(struct p9_conf, uid) },
	{ "max_fid", UINT, offsetof(struct p9_conf, max_fid) },
	{ "max_tag", UINT, offsetof(struct p9_conf, max_tag) },
	{ "pipeline", UINT, offsetof(struct p9_conf, pipeline) },
	{ "net_type", NET_TYPE, 0 },
	{ NULL, 0, 0 }
};

int parser(char *conf_file, struct p9_conf *p9_conf) {
	FILE *fd;
	char line[2*MAXPATHLEN];
	int i, ret, rc;
	char buf_s[MAXNAMLEN];
	int buf_i;
	char *port;
	void *ptr;
	rc = 0;

	fd = fopen(conf_file, "r");

	if (fd == NULL) {
		rc = errno;
		ERROR_LOG("Could not open %s: %s (%d)", conf_file, strerror(rc), rc);
		return rc;
	}

	// fill default values.
	memset(p9_conf, 0, sizeof(struct p9_conf));
	port = NULL;
	p9_conf->trans_attr.server = -1;
	p9_conf->trans_attr.disconnect_callback = p9_disconnect_cb;
	p9_conf->trans_attr.max_send_sge = 2;
	p9_conf->trans_attr.rq_depth = DEFAULT_RECV_NUM;
	p9_conf->msize = DEFAULT_MSIZE;
	p9_conf->max_fid = DEFAULT_MAX_FID;
	p9_conf->max_tag = DEFAULT_MAX_TAG;
	p9_conf->debug = DEFAULT_DEBUG;
	p9_conf->pipeline = DEFAULT_PIPELINE;
	p9_conf->trans_attr.debug = DEFAULT_RDMA_DEBUG;
#if HAVE_MOOSHIKA
	p9_conf->net_ops = &p9_rdma_ops;
#else
	p9_conf->net_ops = &p9_tcp_ops;
#endif

	while (fgets(line, 2*MAXPATHLEN, fd)) {
		// skip comments
		if (line[0] == '#' || line[0] == '\n')
			continue;

		for (i=0; conf_array[i].token != NULL; i++) {
			if (strncasecmp(conf_array[i].token, line, strlen(conf_array[i].token)))
				continue;

			// we have a match
			switch(conf_array[i].type) {
				case UINT:
					ptr = (char*)p9_conf + conf_array[i].offset;
					if (sscanf(line, "%*s = %i", (int*)ptr) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						rc = EINVAL;
						goto out;
					}
					INFO_LOG(p9_conf->debug & P9_DEBUG_SETUP, "Read %s: %i", conf_array[i].token, *(int*)ptr);
					break;
				case STRING:
					ptr = (char*)p9_conf + conf_array[i].offset;
					if (sscanf(line, "%*s = %s", (char*)ptr) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						rc = EINVAL;
						goto out;
					}
					INFO_LOG(p9_conf->debug & P9_DEBUG_SETUP, "Read %s: %s", conf_array[i].token, (char*)ptr);
					break;
				case SIZE:
					ptr = (char*)p9_conf + conf_array[i].offset;
					ret = sscanf(line, "%*s = %i %[a-zA-Z] %i", (int*)ptr, buf_s, &buf_i);
					if (ret >= 2) {
						if (set_size((int*)ptr, buf_s)) {
							ERROR_LOG("set_size error on line: %s", line);
							rc = EINVAL;
							goto out;
						}
						if (ret == 3)
							*(int*)ptr += buf_i;
					} else if (ret != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						rc = EINVAL;
						goto out;
					}
					INFO_LOG(p9_conf->debug & P9_DEBUG_SETUP, "Read %s: %i", conf_array[i].token, *(int*)ptr);
					break;
				case IP:
					if (sscanf(line, "%*s = %s", buf_s) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						rc = EINVAL;
						goto out;
					}
					p9_conf->trans_attr.node = strdup(buf_s);

					// Sanity check: we got an IP
					p9_conf->trans_attr.server = 0;

					INFO_LOG(p9_conf->debug & P9_DEBUG_SETUP, "Read %s: %s", conf_array[i].token, buf_s);
					break;
				case PORT:
					if (sscanf(line, "%*s = %s", buf_s) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						rc = EINVAL;
						goto out;
					}

					port = strdup(buf_s);

					INFO_LOG(p9_conf->debug & P9_DEBUG_SETUP, "Read %s: %s", conf_array[i].token, buf_s);
					break;
				case NET_TYPE:
					if (sscanf(line, "%*s = %s", buf_s) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						rc = EINVAL;
						goto out;
					}
					if (strncasecmp(buf_s, p9_net_tcp_s, strlen(p9_net_tcp_s)) == 0) {
						p9_conf->net_ops = &p9_tcp_ops;
#if HAVE_MOOSHIKA
					} else if (strncasecmp(buf_s, p9_net_rdma_s, strlen(p9_net_rdma_s)) == 0) {
						p9_conf->net_ops = &p9_rdma_ops;
#endif
					} else {
						ERROR_LOG("Unknown net type: %s. Assuming default.", buf_s);
					}
					break;
				default:
					ERROR_LOG("token %s not yet implemented", conf_array[i].token);
			}
			break;
		}

		// no match found
		if (conf_array[i].token == NULL) {
			ERROR_LOG("Unknown configuration entry: %s", line);
		}
	}

	// Default port. depends on the sin family
	if (port == NULL) {
		if (p9_conf->net_ops == &p9_tcp_ops)
			p9_conf->trans_attr.port = strdup(DEFAULT_PORT_TCP);
#if HAVE_MOOSHIKA
		else if(p9_conf->net_ops == &p9_rdma_ops)
			p9_conf->trans_attr.port = strdup(DEFAULT_PORT_RDMA);
#endif
		else
			ERROR_LOG("ops neither tcp nor rdma?");
	} else {
		p9_conf->trans_attr.port = port;
	}

out:
	fclose(fd);
	return rc;
}

void p9_destroy(struct p9_handle **pp9_handle) {
	struct p9_handle *p9_handle = *pp9_handle;
	if (p9_handle) {
		if (p9_handle->cwd) {
			p9p_clunk(p9_handle, &p9_handle->cwd);
		}
		if (p9_handle->root_fid) {
			p9p_clunk(p9_handle, &p9_handle->root_fid);
		}
		bitmap_destroy(&p9_handle->wdata_bitmap);
		bitmap_destroy(&p9_handle->fids_bitmap);
		bitmap_destroy(&p9_handle->tags_bitmap);
		bucket_destroy(&p9_handle->fids_bucket);
		if (p9_handle->tags) {
			free(p9_handle->tags);
			p9_handle->tags = NULL;
		}
		if (p9_handle->wdata) {
			free(p9_handle->wdata);
			p9_handle->wdata = NULL;
		}
		if (p9_handle->rdata) {
			p9_handle->net_ops->dereg_mr(p9_handle->rdata[0].mr);
			free(p9_handle->rdata);
			p9_handle->rdata = NULL;
		}
		if (p9_handle->rdmabuf) {
			free(p9_handle->rdmabuf);
			p9_handle->rdmabuf = NULL;
		}
		if (p9_handle->trans) {
			p9_handle->net_ops->destroy_trans(&p9_handle->trans);
		}
		if (p9_handle->trans_attr.node) {
			free(p9_handle->trans_attr.node);
			p9_handle->trans_attr.node = NULL;
		}
		if (p9_handle->trans_attr.port) {
			free(p9_handle->trans_attr.port);
			p9_handle->trans_attr.port = NULL;
		}
		free(p9_handle);
		*pp9_handle = NULL;
	}
}

int p9_init(struct p9_handle **pp9_handle, char *conf_file) {
	struct addrinfo hints, *info;
	struct p9_conf p9_conf;
	struct p9_handle *p9_handle;
	int rc, i;

	rc = parser(conf_file, &p9_conf);
	if (rc) {
		ERROR_LOG("parsing error");
		return rc;
	}
	if (p9_conf.aname[0] == '\0' || p9_conf.trans_attr.server == -1) {
		ERROR_LOG("You need to set at least aname and server");
		return EINVAL;
	}

	p9_handle = malloc(sizeof(struct p9_handle));
	if (p9_handle == NULL) {
		ERROR_LOG("Could not allocate p9_handle");
		return ENOMEM;
	}
	memset(p9_handle, 0, sizeof(struct p9_handle));

	do {
		strcpy(p9_handle->aname, p9_conf.aname);
		p9_handle->aname_len = strlen(p9_handle->aname);

		p9_handle->debug = p9_conf.debug;
		p9_handle->pipeline = p9_conf.pipeline;
		p9_handle->uid = p9_conf.uid;
		p9_handle->recv_num = p9_conf.trans_attr.rq_depth;
		p9_handle->msize = p9_conf.msize;
		p9_handle->max_fid = p9_conf.max_fid;
		p9_handle->max_tag = (p9_conf.max_tag > 65535 ? 65535 : p9_conf.max_tag);
		p9_handle->net_ops = p9_conf.net_ops;
		p9_handle->umask = umask(0);
		umask(p9_handle->umask);
		memcpy(&p9_handle->trans_attr, &p9_conf.trans_attr, sizeof(struct msk_trans_attr));

		/* cache our own hostname - p9_ahndle->hostname is MAX_CANON+1 long*/
		p9_handle->hostname[MAX_CANON] = '\0';
		gethostname(p9_handle->hostname, MAX_CANON);

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC; /*either IPV4 or IPV6*/
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_CANONNAME;

		if ((rc = getaddrinfo(p9_handle->hostname, "http", &hints, &info)) != 0) {
		    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
		    break;
		}
		strncpy(p9_handle->hostname, info->ai_canonname, MAX_CANON);
		freeaddrinfo(info);

		/* alloc buffers */
		p9_handle->rdmabuf = malloc(2 * p9_handle->recv_num * p9_conf.msize);
		p9_handle->rdata = malloc(p9_handle->recv_num * sizeof(msk_data_t));
		p9_handle->wdata = malloc(p9_handle->recv_num * sizeof(msk_data_t));
		if (p9_handle->rdmabuf == NULL || p9_handle->rdata == NULL || p9_handle->wdata == NULL) {
			ERROR_LOG("Could not allocate data buffer (%luMB)",
			          2 * p9_handle->recv_num * (p9_conf.msize + sizeof(msk_data_t)) / 1024 / 1024);
			rc = ENOMEM;
			break;
		}
		memset(p9_handle->rdata, 0, p9_handle->recv_num * sizeof(msk_data_t));
		memset(p9_handle->wdata, 0, p9_handle->recv_num * sizeof(msk_data_t));

		for (i=0; i < p9_handle->recv_num; i++) {
			p9_handle->rdata[i].data = p9_handle->rdmabuf + i * p9_handle->msize;
			p9_handle->wdata[i].data = p9_handle->rdata[i].data + p9_handle->recv_num * p9_handle->msize;
			p9_handle->rdata[i].size = p9_handle->rdata[i].max_size = p9_handle->wdata[i].max_size = p9_handle->msize;
		}
		p9_handle->credits = p9_handle->recv_num;

		 /* bitmaps, divide by /8 (=/64*8)*/
		p9_handle->wdata_bitmap = bitmap_init(p9_handle->recv_num);
		p9_handle->fids_bitmap = bitmap_init(p9_handle->max_fid);
		p9_handle->tags_bitmap = bitmap_init(p9_handle->max_tag);
		p9_handle->fids_bucket = bucket_init(p9_handle->max_fid/8, sizeof(struct p9_fid));
		p9_handle->tags = calloc(1, p9_handle->max_tag * sizeof(struct p9_tag));
		p9_handle->fids = calloc(1, p9_handle->max_fid * sizeof(void*));
		if (p9_handle->wdata_bitmap == NULL || p9_handle->fids_bitmap == NULL ||
		    p9_handle->tags_bitmap == NULL || p9_handle->fids_bucket == NULL ||
		    p9_handle->tags == NULL || p9_handle->fids == NULL) {
			rc = ENOMEM;
			break;
		}

		pthread_mutex_init(&p9_handle->wdata_lock, NULL);
		pthread_cond_init(&p9_handle->wdata_cond, NULL);
		pthread_mutex_init(&p9_handle->recv_lock, NULL);
		pthread_cond_init(&p9_handle->recv_cond, NULL);
		pthread_mutex_init(&p9_handle->tag_lock, NULL);
		pthread_cond_init(&p9_handle->tag_cond, NULL);
		pthread_mutex_init(&p9_handle->fid_lock, NULL);
		pthread_mutex_init(&p9_handle->connection_lock, NULL);
		pthread_mutex_init(&p9_handle->credit_lock, NULL);
		pthread_cond_init(&p9_handle->credit_cond, NULL);

		rc = p9c_reconnect(p9_handle);
		if (rc)
			break;

		rc = p9p_walk(p9_handle, p9_handle->root_fid, NULL, &p9_handle->cwd);
		if (rc)
			break;

	} while (0);

	if (rc) {
		p9_destroy(&p9_handle);
		return rc;
	}

	*pp9_handle = p9_handle;
	return 0;
}
