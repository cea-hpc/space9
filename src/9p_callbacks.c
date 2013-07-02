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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "9p_internals.h"
#include "utils.h"
#include "settings.h"


void p9_disconnect_cb(msk_trans_t *trans) {
	struct p9_handle *p9_handle = trans->private_data;

        pthread_mutex_lock(&p9_handle->recv_lock);
	pthread_cond_broadcast(&p9_handle->recv_cond);
        pthread_mutex_unlock(&p9_handle->recv_lock);
}

void p9_recv_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg) {
	/* don't know which tag it was... do nothing */

	/* Only log error if it's not something we already know */
	if (trans->state == MSK_CONNECTED)
		ERROR_LOG("recv callback error");
}

void p9_recv_cb(msk_trans_t *trans, msk_data_t *data, void *arg) {
	struct p9_handle *p9_handle = trans->private_data;
	uint16_t tag;

	p9_get_tag(&tag, data->data);

	INFO_LOG(p9_handle->full_debug, "got reply for tag %u", tag);

	/* kludge on P9_NOTAG to have a smaller array */
	if (tag == P9_NOTAG)
		tag = p9_handle->max_tag-1;

	pthread_mutex_lock(&p9_handle->recv_lock);
	p9_handle->tags[tag].rdata = data;
	pthread_cond_broadcast(&p9_handle->recv_cond);
	pthread_mutex_unlock(&p9_handle->recv_lock);
}

void p9_send_cb(msk_trans_t *trans, msk_data_t *data, void *arg) {
	data->next = NULL;
}

void p9_send_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg) {
	uint16_t tag = (uint16_t)(uint64_t)arg;
	ERROR_LOG("message with tag %u was not sent correctly", tag);
	/** @todo: get a static buffer with RERROR to fill tag with */

	p9_send_cb(trans, data, arg);
}

