#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <mooshika.h>
#include "9p.h"
#include "utils.h"
#include "settings.h"


void p9_disconnect_cb(msk_trans_t *trans) {
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
		tag = 0;

	pthread_mutex_lock(&p9_handle->recv_lock);
	p9_handle->tags[tag].rdata = data;
	pthread_cond_broadcast(&p9_handle->recv_cond);
	pthread_mutex_unlock(&p9_handle->recv_lock);
}

void p9_send_cb(msk_trans_t *trans, msk_data_t *data, void *arg) {
	struct p9_handle *p9_handle = trans->private_data;
	uint32_t wdata_i = data - p9_handle->wdata;

	data->next = NULL;

	pthread_mutex_lock(&p9_handle->wdata_lock);
	clear_bit(p9_handle->wdata_bitmap, wdata_i);
	pthread_cond_signal(&p9_handle->wdata_cond);
	pthread_mutex_unlock(&p9_handle->wdata_lock);
}

void p9_send_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg) {
	uint16_t tag = (uint16_t)(uint64_t)arg;
	ERROR_LOG("message with tag %u was not sent correctly", tag);
	/** @todo: get a static buffer with RERROR to fill tag with */

	p9_send_cb(trans, data, arg);
}

