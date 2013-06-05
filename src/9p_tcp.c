/*
 *
 * Copyright CEA/DAM/DIF (2013)
 * contributor : Dominique Martinet  dominique.martinet@cea.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

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

#include <mooshika.h>
#include "utils.h"

/**
 * \file	trans_tcp.c
 * \brief	tcp layer for mooshika, to use it anyway
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
	struct rdmactx *next;		/**< next context */
	msk_data_t *pdata;
	ctx_callback_t callback;
	ctx_callback_t err_callback;
	union {
		struct ibv_recv_wr rwr;
		struct ibv_send_wr wwr;
	} wr;
	void *callback_arg;
	struct ibv_sge sg_list[0]; 		/**< this is actually an array. note that when you malloc you have to add its size */
};

struct msk_internals {
	pthread_mutex_t lock;
	int debug;
	pthread_t tcp_thread;
	int tcp_epollfd;
	unsigned int run_threads;
};

static struct msk_internals *internals;


void __attribute__ ((constructor)) msk_internals_init(void) {
	internals = malloc(sizeof(*internals));
	if (!internals) {
		ERROR_LOG("Out of memory");
	}

	memset(internals, 0, sizeof(*internals));

	internals->run_threads = 0;
	pthread_mutex_init(&internals->lock, NULL);
}

void __attribute__ ((destructor)) msk_internals_fini(void) {

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


void *tcp_feeder_thread(void *arg) {
	pthread_exit(NULL);
}

int msk_init(msk_trans_t **ptrans, msk_trans_attr_t *attr) {
	int ret;

	msk_trans_t *trans;

	if (!ptrans || !attr) {
		ERROR_LOG("Invalid argument");
		return EINVAL;
	}

	trans = malloc(sizeof(msk_trans_t));
	if (!trans) {
		ERROR_LOG("Out of memory");
		return ENOMEM;
	}

	do {
		memset(trans, 0, sizeof(msk_trans_t));

		trans->state = MSK_INIT;

		if (!attr->addr.sa_stor.ss_family) { //FIXME: do a proper check?
			ERROR_LOG("address has to be defined");
			ret = EDESTADDRREQ;
			break;
		}
		trans->addr.sa_stor = attr->addr.sa_stor;

		trans->server = attr->server;
		trans->timeout = attr->timeout   ? attr->timeout  : 3000000; // in ms
		trans->sq_depth = attr->sq_depth ? attr->sq_depth : 50;
		trans->max_send_sge = attr->max_send_sge ? attr->max_send_sge : 1;
		trans->rq_depth = attr->rq_depth ? attr->rq_depth : 50;
		trans->max_recv_sge = attr->max_recv_sge ? attr->max_recv_sge : 1;
		trans->disconnect_callback = attr->disconnect_callback;

		ret = pthread_mutex_init(&trans->cm_lock, NULL);
		if (ret) {
			ERROR_LOG("pthread_mutex_init failed: %s (%d)", strerror(ret), ret);
			break;
		}
		ret = pthread_cond_init(&trans->cm_cond, NULL);
		if (ret) {
			ERROR_LOG("pthread_cond_init failed: %s (%d)", strerror(ret), ret);
			break;
		}
		ret = pthread_mutex_init(&trans->ctx_lock, NULL);
		if (ret) {
			ERROR_LOG("pthread_mutex_init failed: %s (%d)", strerror(ret), ret);
			break;
		}
		ret = pthread_cond_init(&trans->ctx_cond, NULL);
		if (ret) {
			ERROR_LOG("pthread_cond_init failed: %s (%d)", strerror(ret), ret);
			break;
		}

		pthread_mutex_lock(&internals->lock);
		internals->debug = attr->debug;
		internals->run_threads++;
		pthread_mutex_unlock(&internals->lock);
		//ret = msk_spawn_tcp_threads();

		if (ret) {
			ERROR_LOG("Could not start worker threads: %s (%d)", strerror(ret), ret);
			break;
		}
	} while (0);

	if (ret) {
		msk_destroy_trans(&trans);
		return ret;
	}

	*ptrans = trans;

	return 0;
}

// server specific:
int msk_bind_server(msk_trans_t *trans) {
	return EIO;
}
msk_trans_t *msk_accept_one_wait(msk_trans_t *trans, int msleep) {
	return NULL;
}
msk_trans_t *msk_accept_one_timedwait(msk_trans_t *trans, struct timespec *abstime) {
	return NULL;
}
int msk_finalize_accept(msk_trans_t *trans) {
	return EIO;
}
void msk_destroy_trans(msk_trans_t **ptrans) {
}

int msk_connect(msk_trans_t *trans) {
	return EIO;
}
int msk_finalize_connect(msk_trans_t *trans) {
	return EIO;
}


/* utility functions */

struct ibv_mr *msk_reg_mr(msk_trans_t *trans, void *memaddr, size_t size, int access) {
	return NULL;
}
int msk_dereg_mr(struct ibv_mr *mr) {
	return 0;
}

msk_rloc_t *msk_make_rloc(struct ibv_mr *mr, uint64_t addr, uint32_t size) {
	return NULL;
}


int msk_post_n_recv(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg) {
	return EIO;
}
int msk_post_n_send(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg) {
	return EIO;
}
int msk_wait_n_recv(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr) {
	return EIO;
}
int msk_wait_n_send(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr) {
	return EIO;
}
int msk_post_n_read(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr, msk_rloc_t *rloc, ctx_callback_t callback, ctx_callback_t err_callback, void* callback_arg) {
	return EIO;
}
int msk_post_n_write(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr, msk_rloc_t *rloc, ctx_callback_t callback, ctx_callback_t err_callback, void* callback_arg) {
	return EIO;
}
int msk_wait_n_read(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr, msk_rloc_t *rloc) {
	return EIO;
}
int msk_wait_n_write(msk_trans_t *trans, msk_data_t *pdata, int num_sge, struct ibv_mr *mr, msk_rloc_t *rloc) {
	return EIO;
}


void msk_print_devinfo(msk_trans_t *trans) {
	ERROR_LOG("Not implemented for TCP");
}

struct sockaddr *msk_get_dst_addr(msk_trans_t *trans) {
	//FIXME getpeername
	return NULL;
}
struct sockaddr *msk_get_src_addr(msk_trans_t *trans) {
	//FIXME getsockname
	return NULL;
}
uint16_t msk_get_src_port(msk_trans_t *trans) {
	//FIXME
	return 0;
}
uint16_t msk_get_dst_port(msk_trans_t *trans) {
	//FIXME
	return 0;
}
