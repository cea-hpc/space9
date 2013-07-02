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

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <unistd.h>     // sleep
#include <assert.h>
#include "9p_internals.h"
#include "utils.h"
#include "settings.h"


int p9c_reconnect(struct p9_handle *p9_handle) {
	int sleeptime = 0;
	int rc = 0, i;
	struct ibv_mr *mr;


	while (!p9_handle->trans || p9_handle->trans->state != MSK_CONNECTED) {
		if (p9_handle->trans)
			p9_handle->net_ops->destroy_trans(&p9_handle->trans);

		if (sleeptime) {
			sleep(sleeptime);
			sleeptime *= 2;
		} else {
			sleeptime = 2;
		}

		/* mooshika init */
		rc = p9_handle->net_ops->init(&p9_handle->trans, &p9_handle->trans_attr);
		if (rc) {
			ERROR_LOG("msk_init failed: %s (%d)", strerror(rc), rc);
			continue;
		}

		p9_handle->trans->private_data = p9_handle;

		rc = p9_handle->net_ops->connect(p9_handle->trans);
		if (rc) {
			ERROR_LOG("msk_connect failed: %s (%d)", strerror(rc), rc);
			continue;
		}


		mr = p9_handle->net_ops->reg_mr(p9_handle->trans, p9_handle->rdmabuf, 2 * p9_handle->recv_num * p9_handle->msize, IBV_ACCESS_LOCAL_WRITE);
		if (mr == NULL) {
			ERROR_LOG("Could not register memory buffer");
			rc = EIO;
			continue;
		}

		for (i=0; i < p9_handle->recv_num; i++) {
			p9_handle->rdata[i].data = p9_handle->rdmabuf + i * p9_handle->msize;
			p9_handle->wdata[i].data = p9_handle->rdata[i].data + p9_handle->recv_num * p9_handle->msize;
			p9_handle->rdata[i].size = p9_handle->rdata[i].max_size = p9_handle->wdata[i].max_size = p9_handle->msize;
			p9_handle->rdata[i].mr = mr;
			p9_handle->wdata[i].mr = mr;
			rc = p9_handle->net_ops->post_n_recv(p9_handle->trans, &p9_handle->rdata[i], 1, p9_recv_cb, p9_recv_err_cb, NULL);
			if (rc) {
				ERROR_LOG("Could not post recv buffer %i: %s (%d)", i, strerror(rc), rc);
				rc = EIO;
				break;
			}
		}
		if (rc)
			continue;

		p9_handle->credits = p9_handle->recv_num;

		rc = p9_handle->net_ops->finalize_connect(p9_handle->trans);
		if (rc)
			continue;

		rc = p9p_version(p9_handle);
		if (rc)
			break;

		rc = p9p_attach(p9_handle, p9_handle->uid, &p9_handle->root_fid);
		if (rc)
			break;

		rc = p9p_walk(p9_handle, p9_handle->root_fid, NULL, &p9_handle->cwd);
		if (rc)
			break;
	}

	return rc;
}

int p9c_getbuffer(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t *ptag) {
	msk_data_t *data;
	uint32_t wdata_i, tag;

	pthread_mutex_lock(&p9_handle->wdata_lock);
	while ((wdata_i = get_and_set_first_bit(p9_handle->wdata_bitmap, p9_handle->recv_num)) == p9_handle->recv_num) {
		INFO_LOG(p9_handle->debug & P9_DEBUG_SEND, "waiting for wdata to free up (sendrequest's acknowledge callback)");
		pthread_cond_wait(&p9_handle->wdata_cond, &p9_handle->wdata_lock);
	}
	pthread_mutex_unlock(&p9_handle->wdata_lock);

	pthread_mutex_lock(&p9_handle->credit_lock);
	while (p9_handle->credits == 0) {
		INFO_LOG(p9_handle->debug & P9_DEBUG_SEND, "waiting for credit (putreply)");
		pthread_cond_wait(&p9_handle->credit_cond, &p9_handle->credit_lock);
	}
	p9_handle->credits--;
	pthread_mutex_unlock(&p9_handle->credit_lock);

	data = &p9_handle->wdata[wdata_i];
	*pdata = data;

	pthread_mutex_lock(&p9_handle->tag_lock);
	/* kludge on P9_NOTAG to have a smaller array */
	if (*ptag == P9_NOTAG) {
		tag = p9_handle->max_tag-1;
		while(get_bit(p9_handle->tags_bitmap, tag))
			pthread_cond_wait(&p9_handle->tag_cond, &p9_handle->tag_lock);
		set_bit(p9_handle->tags_bitmap, tag);
	} else {
		while ((tag = get_and_set_first_bit(p9_handle->tags_bitmap, p9_handle->max_tag)) == p9_handle->max_tag)
			pthread_cond_wait(&p9_handle->tag_cond, &p9_handle->tag_lock);
	}
	pthread_mutex_unlock(&p9_handle->tag_lock);


	p9_handle->tags[tag].rdata = NULL;
	p9_handle->tags[tag].wdata_i = wdata_i;

	*ptag = (uint16_t)tag;
	return 0;
}


int p9c_sendrequest(struct p9_handle *p9_handle, msk_data_t *data, uint16_t tag) {
	// We need more recv buffers ready than requests pending
	int rc;

	INFO_LOG(p9_handle->debug & P9_DEBUG_SEND, "send request for tag %u", tag);

	rc = p9_handle->net_ops->post_n_send(p9_handle->trans, data, (data->next != NULL) ? 2 : 1, p9_send_cb, p9_send_err_cb, (void*)(uint64_t)tag);

	if (rc) {
		p9c_reconnect(p9_handle);
		return p9c_sendrequest(p9_handle, data, tag);
	}

	return rc;
}


int p9c_abortrequest(struct p9_handle *p9_handle, msk_data_t *data, uint16_t tag) {
	/* release data and tag, getreply code */
	pthread_mutex_lock(&p9_handle->wdata_lock);
	clear_bit(p9_handle->wdata_bitmap, p9_handle->tags[tag].wdata_i);
	pthread_cond_signal(&p9_handle->wdata_cond);
	pthread_mutex_unlock(&p9_handle->wdata_lock);

	pthread_mutex_lock(&p9_handle->tag_lock);
	if (tag == P9_NOTAG)
		tag = p9_handle->max_tag -1;
	clear_bit(p9_handle->tags_bitmap, tag);
	pthread_cond_broadcast(&p9_handle->tag_cond);
	pthread_mutex_unlock(&p9_handle->tag_lock);

	/* ... and credit, putreply code */
	pthread_mutex_lock(&p9_handle->credit_lock);
	p9_handle->credits++;
	pthread_cond_broadcast(&p9_handle->credit_cond);
	pthread_mutex_unlock(&p9_handle->credit_lock);

	return 0;
}


int p9c_getreply(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t tag) {

	pthread_mutex_lock(&p9_handle->recv_lock);
	while (p9_handle->tags[tag].rdata == NULL && p9_handle->trans->state == MSK_CONNECTED) {
		pthread_cond_wait(&p9_handle->recv_cond, &p9_handle->recv_lock);
	}
	pthread_mutex_unlock(&p9_handle->recv_lock);

	if (p9_handle->trans->state != MSK_CONNECTED) {
		p9c_reconnect(p9_handle);
		p9c_sendrequest(p9_handle, &p9_handle->wdata[p9_handle->tags[tag].wdata_i], tag);
		return p9c_getreply(p9_handle, pdata, tag);
	}

	pthread_mutex_lock(&p9_handle->wdata_lock);
	clear_bit(p9_handle->wdata_bitmap, p9_handle->tags[tag].wdata_i);
	pthread_cond_signal(&p9_handle->wdata_cond);
	pthread_mutex_unlock(&p9_handle->wdata_lock);

	*pdata = p9_handle->tags[tag].rdata;
	pthread_mutex_lock(&p9_handle->tag_lock);
	if (tag == P9_NOTAG)
		tag = p9_handle->max_tag -1;
	clear_bit(p9_handle->tags_bitmap, tag);
	pthread_cond_broadcast(&p9_handle->tag_cond);
	pthread_mutex_unlock(&p9_handle->tag_lock);

	return 0;
}


int p9c_putreply(struct p9_handle *p9_handle, msk_data_t *data) {
	int rc;

	rc = p9_handle->net_ops->post_n_recv(p9_handle->trans, data, 1, p9_recv_cb, p9_recv_err_cb, NULL);
	if (rc) {
		ERROR_LOG("Could not post recv buffer %p: %s (%d)", data, strerror(rc), rc);
		rc = EIO;
	} else {
		pthread_mutex_lock(&p9_handle->credit_lock);
		p9_handle->credits++;
		pthread_cond_broadcast(&p9_handle->credit_cond);
		pthread_mutex_unlock(&p9_handle->credit_lock);
	}

	return rc;
}


int p9c_getfid(struct p9_handle *p9_handle, struct p9_fid **pfid) {
	struct p9_fid *fid;
	uint32_t fid_i;

	pthread_mutex_lock(&p9_handle->fid_lock);
	fid_i = get_and_set_first_bit(p9_handle->fids_bitmap, p9_handle->max_fid);
	pthread_mutex_unlock(&p9_handle->fid_lock);

	if (fid_i == p9_handle->max_fid)
		return ERANGE;

	fid = bucket_get(p9_handle->fids_bucket);
	if (fid == NULL) {
		pthread_mutex_lock(&p9_handle->fid_lock);
		clear_bit(p9_handle->fids_bitmap, fid_i);
		pthread_mutex_unlock(&p9_handle->fid_lock);
		return ENOMEM;
	}

	fid->p9_handle = p9_handle;
	fid->fid = fid_i;
	fid->openflags = 0;
	fid->offset = 0L;
	*pfid = fid;
	return 0;
}


int p9c_putfid(struct p9_handle *p9_handle, struct p9_fid **pfid) {
	pthread_mutex_lock(&p9_handle->fid_lock);
	clear_bit(p9_handle->fids_bitmap, (*pfid)->fid);
	pthread_mutex_unlock(&p9_handle->fid_lock);

	bucket_put(p9_handle->fids_bucket, (void**)pfid);

	return 0;
}


int p9c_reg_mr(struct p9_handle *p9_handle, msk_data_t *data) {
	data->mr = p9_handle->net_ops->reg_mr(p9_handle->trans, data->data, data->max_size, IBV_ACCESS_LOCAL_WRITE);
#if HAVE_MOOSHIKA
	if (data->mr == NULL) {
		return -1;
	}
#endif
	return 0;
}

int p9c_dereg_mr(struct p9_handle *p9_handle, msk_data_t *data) {
	return p9_handle->net_ops->dereg_mr(data->mr);
}
