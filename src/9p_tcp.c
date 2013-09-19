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

#include <stdio.h>	//printf
#include <stdlib.h>	//malloc
#include <string.h>	//memcpy
#include <inttypes.h>	//uint*_t
#include <errno.h>	//ENOMEM
#include <sys/socket.h> //sockaddr
#include <pthread.h>	//pthread_* (think it's included by another one)
#include <semaphore.h>  //sem_* (is it a good idea to mix sem and pthread_cond/mutex?)
#include <arpa/inet.h>  //inet_ntop
#include <netinet/in.h> //sock_addr_in
#include <unistd.h>	//fcntl
#include <fcntl.h>	//fcntl
#include <sys/epoll.h>	//epoll
#define MAX_EVENTS 10

#include "9p_internals.h"
#include "utils.h"

/**
 * \file	9p_tcp.c
 * \brief	tcp layer for mooshika - 9P specific
 * (because it uses the fact that 9p messages start with message size)
 *
 * slack asynchronus TCP
 *
 */

/**
 * \struct msk_ctx
 * Context data we can use during recv/send callbacks
 */
struct msk_ctx {
	enum msk_ctx_used {
		MSK_CTX_FREE = 0,
		MSK_CTX_PENDING,
		MSK_CTX_PROCESSING
	} used;				/**< 0 if we can use it for a new recv/send */
	uint32_t pos;			/**< current position inside our own buffer. 0 <= pos <= len */
	int num_sge;
	struct rdmactx *next;		/**< next context */
	msk_data_t *data;
	ctx_callback_t callback;
	ctx_callback_t err_callback;
	void *callback_arg;
};

#define MSK_DEBUG_EVENT 0x0001
#define MSK_DEBUG_SETUP 0x0002
#define MSK_DEBUG_SEND 0x0004
#define MSK_DEBUG_RECV 0x0008

struct msk_tcp_trans {
	int sockfd;
	sockaddr_union_t peer_sa;
	pthread_t cq_thrid;
	pthread_mutex_t lock;
};

#define tcpt(trans) ((struct msk_tcp_trans*)trans->cm_id)

struct msk_internals {
	pthread_mutex_t lock;
	int debug;
	pthread_t tcp_thread;
	int tcp_epollfd;
	unsigned int run_threads;
};

static struct msk_internals *internals;


void __attribute__ ((constructor)) msk_tcp_internals_init(void) {
	internals = malloc(sizeof(*internals));
	if (!internals) {
		ERROR_LOG("Out of memory");
	}

	memset(internals, 0, sizeof(*internals));

	internals->run_threads = 0;
	pthread_mutex_init(&internals->lock, NULL);
}

void __attribute__ ((destructor)) msk_tcp_internals_fini(void) {

	if (internals) {
		internals->run_threads = 0;

		if (internals->tcp_thread) {
			pthread_join(internals->tcp_thread, NULL);
			internals->tcp_thread = 0;
		}

		pthread_mutex_destroy(&internals->lock);
		free(internals);
		internals = NULL;
	}
}

/**
 * msk_tcp_create_thread: Simple wrapper around pthread_create
 */
#define THREAD_STACK_SIZE 2116488
static inline int msk_tcp_create_thread(pthread_t *thrid, void *(*start_routine)(void*), void *arg) {

	pthread_attr_t attr;
	int ret;

	/* Init for thread parameter (mostly for scheduling) */
	if ((ret = pthread_attr_init(&attr)) != 0) {
		INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "can't init pthread's attributes: %s (%d)", strerror(ret), ret);
		return ret;
	}

	if ((ret = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) != 0) {
		INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "can't set pthread's scope: %s (%d)", strerror(ret), ret);
		return ret;
	}

	if ((ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE)) != 0) {
		INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "can't set pthread's join state: %s (%d)", strerror(ret), ret);
		return ret;
	}

	if ((ret = pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE)) != 0) {
		INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "can't set pthread's stack size: %s (%d)", strerror(ret), ret);
		return ret;
	}

	return pthread_create(thrid, &attr, start_routine, arg);
}

static void *msk_tcp_recv_thread(void *arg) {
	msk_trans_t *trans = arg;
	char *junk;
	int rc, i;
	msk_data_t *data;
	msk_ctx_t *ctx;
	uint32_t n, packet_size, read_size;

	while (trans->state == MSK_CONNECTED) {
		pthread_mutex_lock(&trans->ctx_lock);
		do {
			for (i = 0, ctx = trans->recv_buf;
			     i < trans->qp_attr.cap.max_recv_wr;
			     i++, ctx = (msk_ctx_t*)((uint8_t*)ctx + sizeof(msk_ctx_t)))
				if (!ctx->used != MSK_CTX_PENDING)
					break;

			if (i == trans->qp_attr.cap.max_recv_wr) {
				INFO_LOG(internals->debug & MSK_DEBUG_RECV, "Waiting for cond");
				pthread_cond_wait(&trans->ctx_cond, &trans->ctx_lock);
			}

		} while ( i == trans->qp_attr.cap.max_recv_wr );
		ctx->used = MSK_CTX_PROCESSING;
		pthread_mutex_unlock(&trans->ctx_lock);

		data = ctx->data;

		data->size = 0;
		while (data->size < 4) {
			n = read(tcpt(trans)->sockfd, data->data + data->size, 4 - data->size);
			data->size += n;
		}
		packet_size = *((uint32_t*)data->data);
		read_size = packet_size;
		if (packet_size > data->max_size) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "packet bigger than data maxsize? (resp. %u and %u)", packet_size, data->max_size);
			read_size = data->max_size;
		}

		do {
			n = read(tcpt(trans)->sockfd, data->data + data->size, read_size - data->size);
			if (n < 0 && errno == EINTR) {
				continue;
			} else if (n < 0) {
				rc = errno;
				INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "recv error! %s (%d)", strerror(rc), rc);
				break;
			}
			data->size += n;
		} while(data->size < read_size);

		if (packet_size > read_size) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "packet too big for buffer, throwing %u bytes out", packet_size - read_size);
			read_size = packet_size - read_size;
			junk = malloc(1024);
			while (read_size > 0) {
				n = read(tcpt(trans)->sockfd, junk, (read_size > 1024 ? 1024 : read_size));
				if (n < 0 && errno == EINTR) {
					continue;
				} else if (n < 0) {
					rc = errno;
					INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "recv error! %s (%d)", strerror(rc), rc);
					break;
				}
				read_size -= n;
			}
			free(junk);
		}

		ctx->callback(trans, data, ctx->callback_arg);

		pthread_mutex_lock(&trans->ctx_lock);
		ctx->used = MSK_CTX_FREE;
		pthread_cond_broadcast(&trans->ctx_cond);
		pthread_mutex_unlock(&trans->ctx_lock);
	}
	pthread_exit(NULL);
}

void msk_tcp_destroy_trans(msk_trans_t **ptrans) {
}

int msk_tcp_setup_buffers(msk_trans_t *trans) {
	trans->recv_buf = malloc(trans->qp_attr.cap.max_recv_wr * sizeof(struct msk_ctx));
	if (trans->recv_buf == NULL)
		return ENOMEM;

	memset(trans->recv_buf, 0, trans->qp_attr.cap.max_recv_wr * sizeof(struct msk_ctx));

	return 0;
}

int msk_tcp_init(msk_trans_t **ptrans, msk_trans_attr_t *attr) {
	int ret;

	msk_trans_t *trans;

	if (!ptrans || !attr) {
		ERROR_LOG("Invalid argument");
		return EINVAL;
	}

	pthread_mutex_lock(&internals->lock);
	internals->debug = attr->debug;
	pthread_mutex_unlock(&internals->lock);

	trans = malloc(sizeof(msk_trans_t));
	if (!trans) {
		INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Out of memory");
		return ENOMEM;
	}

	do {
		memset(trans, 0, sizeof(msk_trans_t));

		trans->cm_id /* tcpt(trans) */ = malloc(sizeof(struct msk_tcp_trans));
		if (!tcpt(trans)) {
			ret = ENOMEM;
			break;
		}
		memset(trans->cm_id, 0, sizeof(struct msk_tcp_trans));

		trans->state = MSK_INIT;

		trans->node = strdup(attr->node);
		if (!trans->node) {
			ret = ENOMEM;
			break;
		}
		trans->port = strdup(attr->port);
		if (!trans->port) {
			ret = ENOMEM;
			break;
		}

		trans->server = attr->server;
		trans->timeout = attr->timeout   ? attr->timeout  : 3000000; // in ms
		trans->qp_attr.cap.max_send_wr = attr->sq_depth ? attr->sq_depth : 50;
		trans->qp_attr.cap.max_send_sge = attr->max_send_sge ? attr->max_send_sge : 1;
		trans->qp_attr.cap.max_recv_wr = attr->rq_depth ? attr->rq_depth : 50;
		trans->qp_attr.cap.max_recv_sge = attr->max_recv_sge ? attr->max_recv_sge : 1;
		trans->disconnect_callback = attr->disconnect_callback;

		ret = pthread_mutex_init(&trans->cm_lock, NULL);
		if (ret) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_mutex_init failed: %s (%d)", strerror(ret), ret);
			break;
		}
		ret = pthread_cond_init(&trans->cm_cond, NULL);
		if (ret) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_cond_init failed: %s (%d)", strerror(ret), ret);
			break;
		}
		ret = pthread_mutex_init(&trans->ctx_lock, NULL);
		if (ret) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_mutex_init failed: %s (%d)", strerror(ret), ret);
			break;
		}
		ret = pthread_cond_init(&trans->ctx_cond, NULL);
		if (ret) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_cond_init failed: %s (%d)", strerror(ret), ret);
			break;
		}

		ret = pthread_mutex_init(&tcpt(trans)->lock, NULL);
		if (ret) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_mutex_init failed: %s (%d)", strerror(ret), ret);
			break;
		}

	} while (0);

	if (ret) {
		msk_tcp_destroy_trans(&trans);
		return ret;
	}

	*ptrans = trans;

	return 0;
}

// server specific:
int msk_tcp_bind_server(msk_trans_t *trans) {
	int rc;
	struct addrinfo *res;

	do {
		tcpt(trans)->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (tcpt(trans)->sockfd == -1) {
			rc = errno;
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Socket creation failed: %s (%d)", strerror(rc), rc);
			break;
		}

		rc = getaddrinfo(trans->node, trans->port, NULL, &res);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "getaddrinfo failed: %s (%d)", strerror(rc), rc);
			break;
		}

		rc = bind(tcpt(trans)->sockfd, res->ai_addr, INET_ADDRSTRLEN);
		if (rc) {
			rc = errno;
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "bind failed: %s (%d)", strerror(rc), rc);
			break;
		}

		freeaddrinfo(res);

		rc = listen(tcpt(trans)->sockfd, trans->server);
		if (rc) {
			rc = errno;
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "listen failed: %s (%d)", strerror(rc), rc);
			break;
		}
	} while (0);

	return rc;
}

static msk_trans_t *clone_trans(msk_trans_t *listening_trans) {
	msk_trans_t *trans = malloc(sizeof(msk_trans_t));
	struct msk_tcp_trans *tcpt = malloc(sizeof(struct msk_tcp_trans));
	int rc;

	if (!trans || !tcpt) {
		INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "malloc failed");
		return NULL;
	}

	memcpy(trans, listening_trans, sizeof(msk_trans_t));

	trans->cm_id = (void*)tcpt;
	trans->state = MSK_CONNECT_REQUEST;

	memset(&trans->cm_lock, 0, sizeof(pthread_mutex_t));
	memset(&trans->cm_cond, 0, sizeof(pthread_cond_t));
	memset(&trans->ctx_lock, 0, sizeof(pthread_mutex_t));
	memset(&trans->ctx_cond, 0, sizeof(pthread_cond_t));

	do {
		rc = msk_tcp_setup_buffers(trans);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Couldn't setup buffers");
			break;
		}
		rc = pthread_mutex_init(&trans->cm_lock, NULL);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_mutex_init failed: %s (%d)", strerror(rc), rc);
			break;
		}
		rc = pthread_cond_init(&trans->cm_cond, NULL);
			if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_cond_init failed: %s (%d)", strerror(rc), rc);
			break;
		}
		rc = pthread_mutex_init(&trans->ctx_lock, NULL);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_mutex_init failed: %s (%d)", strerror(rc), rc);
			break;
		}
		rc = pthread_cond_init(&trans->ctx_cond, NULL);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "pthread_cond_init failed: %s (%d)", strerror(rc), rc);
			break;
		}
	} while (0);

	if (rc) {
		msk_tcp_destroy_trans(&trans);
		trans = NULL;
	}

	return trans;
}

msk_trans_t *msk_tcp_accept_one_wait(msk_trans_t *trans, int msleep) {
	msk_trans_t *child_trans;
	socklen_t len = sizeof(sockaddr_union_t);

	child_trans = clone_trans(trans);

	if (!child_trans)
		return NULL;

	tcpt(child_trans)->sockfd = accept(tcpt(trans)->sockfd, &tcpt(child_trans)->peer_sa.sa, &len);

	if (tcpt(child_trans)->sockfd == -1) {
		msk_tcp_destroy_trans(&child_trans);
		return NULL;
	}

	child_trans->state = MSK_CONNECT_REQUEST;

	return child_trans;
}
msk_trans_t *msk_tcp_accept_one_timedwait(msk_trans_t *trans, struct timespec *abstime) {
	return msk_tcp_accept_one_wait(trans, 0);
}

int msk_tcp_finalize_accept(msk_trans_t *trans) {
	int rc;

	if (trans->state != MSK_CONNECT_REQUEST)
		return EINVAL;

	rc = msk_tcp_create_thread(&tcpt(trans)->cq_thrid, msk_tcp_recv_thread, trans);
	if (!rc)
		trans->state = MSK_CONNECTED;

	return rc;
}

int msk_tcp_connect(msk_trans_t *trans) {
	int rc;
	struct addrinfo *res;

	do {
		rc = msk_tcp_setup_buffers(trans);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Couldn't setup buffers");
			break;
		}

		tcpt(trans)->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (tcpt(trans)->sockfd == -1) {
			rc = errno;
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Socket creation failed: %s (%d)", strerror(rc), rc);
			break;
		}

		rc = getaddrinfo(trans->node, trans->port, NULL, &res);
		if (rc) {
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "getaddrinfo failed: %s (%d)", strerror(rc), rc);
			break;
		}

		rc = connect(tcpt(trans)->sockfd, res->ai_addr, INET_ADDRSTRLEN);
		if (rc) {
			rc = errno;
			INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Connect failed: %s (%d)", strerror(rc), rc);
			break;
		}

		freeaddrinfo(res);
	} while (0);

	if (!rc)
		trans->state = MSK_CONNECT_REQUEST;

	return rc;
}
int msk_tcp_finalize_connect(msk_trans_t *trans) {
	int rc;

	if (trans->state != MSK_CONNECT_REQUEST)
		return EINVAL;

	rc = msk_tcp_create_thread(&tcpt(trans)->cq_thrid, msk_tcp_recv_thread, trans);
	if (!rc)
		trans->state = MSK_CONNECTED;

	return rc;
}


/* utility functions */

struct ibv_mr *msk_tcp_reg_mr(msk_trans_t *trans, void *memaddr, size_t size, int access) {
	return memaddr;
}
int msk_tcp_dereg_mr(struct ibv_mr *mr) {
	return 0;
}

msk_rloc_t *msk_tcp_make_rloc(struct ibv_mr *mr, uint64_t addr, uint32_t size) {
	return NULL;
}

int msk_tcp_post_n_recv(msk_trans_t *trans, msk_data_t *data, int num_sge, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg) {
	struct msk_ctx *ctx;
	int i;

	pthread_mutex_lock(&trans->ctx_lock);
	do {
		for (i = 0, ctx = trans->recv_buf;
		     i < trans->qp_attr.cap.max_recv_wr;
		     i++, ctx = (msk_ctx_t*)((uint8_t*)ctx + sizeof(msk_ctx_t)))
			if (!ctx->used != MSK_CTX_FREE)
				break;

		if (i == trans->qp_attr.cap.max_recv_wr) {
			INFO_LOG(internals->debug & MSK_DEBUG_RECV, "Waiting for cond");
			pthread_cond_wait(&trans->ctx_cond, &trans->ctx_lock);
		}

	} while ( i == trans->qp_attr.cap.max_recv_wr );
	ctx->data = data;
	ctx->num_sge = num_sge;
	ctx->callback = callback;
	ctx->err_callback = err_callback;
	ctx->callback_arg = callback_arg;
	ctx->used = MSK_CTX_PENDING;
	pthread_cond_broadcast(&trans->ctx_cond);
	pthread_mutex_unlock(&trans->ctx_lock);

	return 0;
}

int msk_tcp_post_n_send(msk_trans_t *trans, msk_data_t *data_arg, int num_sge, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg) {
	int rc, i;
	uint32_t cur;
	msk_data_t *data = data_arg;
	pthread_mutex_lock(&tcpt(trans)->lock);

	rc = 0;
	for (i=0; i < num_sge; i++) {
		if (!data) {
			rc = EINVAL;
			break;
		}
		cur = 0;
		while (rc == 0 && cur < data->size) {
			rc = write(tcpt(trans)->sockfd, data->data, data->size - cur);
			if (rc < 0 && errno == EINTR) {
				continue;
			} else if (rc < 0) {
				rc = errno;
				INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "write failed: %s (%d)", strerror(rc), rc);
			} else {
				cur += rc;
				rc = 0;
			}
		}

		if (rc)
			break;
		data = data->next;
	}
	pthread_mutex_unlock(&tcpt(trans)->lock);

	if (rc)
		err_callback(trans, data_arg, callback_arg);
	else
		callback(trans, data_arg, callback_arg);

	return rc;
}

void msk_tcp_print_devinfo(msk_trans_t *trans) {
	INFO_LOG(internals->debug & MSK_DEBUG_EVENT, "Not implemented for TCP");
}

struct sockaddr *msk_tcp_get_dst_addr(msk_trans_t *trans) {
	return &tcpt(trans)->peer_sa.sa;
}
struct sockaddr *msk_tcp_get_src_addr(msk_trans_t *trans) {
	//FIXME getsockname
	return NULL;
}
uint16_t msk_tcp_get_src_port(msk_trans_t *trans) {
	//FIXME
	return 0;
}
uint16_t msk_tcp_get_dst_port(msk_trans_t *trans) {
	//FIXME
	return 0;
}
