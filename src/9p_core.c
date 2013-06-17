#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <assert.h>
#include <mooshika.h>
#include "9p.h"
#include "utils.h"
#include "settings.h"


/**
 * @brief Get a buffer to fill that will be ok to send directly
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [OUT]   pdata:	filled with appropriate buffer
 * @param [OUT]   tag:		available tag to use in the reply. If set to P9_NOTAG, this is taken instead.
 * @return 0 on success, errno value on error
 */
int p9c_getbuffer(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t *ptag) {
	msk_data_t *data;
	uint32_t wdata_i, tag;

	pthread_mutex_lock(&p9_handle->wdata_lock);
	while ((wdata_i = get_and_set_first_bit(p9_handle->wdata_bitmap, p9_handle->recv_num)) == p9_handle->recv_num) {
		INFO_LOG(p9_handle->debug, "waiting for wdata to free up (sendrequest's acknowledge callback)");
		pthread_cond_wait(&p9_handle->wdata_cond, &p9_handle->wdata_lock);
	}
	pthread_mutex_unlock(&p9_handle->wdata_lock);

	pthread_mutex_lock(&p9_handle->credit_lock);
	while (p9_handle->credits == 0) {
		INFO_LOG(p9_handle->debug, "waiting for credit (putreply)");
		pthread_cond_wait(&p9_handle->credit_cond, &p9_handle->credit_lock);
	}
	p9_handle->credits--;
	pthread_mutex_unlock(&p9_handle->credit_lock);

	data = &p9_handle->wdata[wdata_i];
	*pdata = data;

	pthread_mutex_lock(&p9_handle->tag_lock);
	/* kludge on P9_NOTAG to have a smaller array */
	if (*ptag == P9_NOTAG) {
		while(get_bit(p9_handle->tags_bitmap, 0))
			pthread_cond_wait(&p9_handle->tag_cond, &p9_handle->tag_lock);
		set_bit(p9_handle->tags_bitmap, 0);
		tag = 0;
	} else {
		while ((tag = get_and_set_first_bit(p9_handle->tags_bitmap, p9_handle->max_tag)) == p9_handle->max_tag)
			pthread_cond_wait(&p9_handle->tag_cond, &p9_handle->tag_lock);
	}
	pthread_mutex_unlock(&p9_handle->tag_lock);


	p9_handle->tags[tag].rdata = NULL;

	*ptag = (uint16_t)tag;
	return 0;
}

/**
 * @brief Send a buffer obtained through getbuffer
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    data:		buffer to send
 * @return 0 on success, errno value on error
 */
int p9c_sendrequest(struct p9_handle *p9_handle, msk_data_t *data, uint16_t tag) {
	// We need more recv buffers ready than requests pending

	if (data->next != NULL) {
		msk_post_n_send(p9_handle->trans, data, 2, p9_send_cb, p9_send_err_cb, (void*)(uint64_t)tag);
	} else {
		msk_post_send(p9_handle->trans, data, p9_send_cb, p9_send_err_cb, (void*)(uint64_t)tag);
	}

	return 0;
}

/**
 * @brief Waits for a reply with a given tag to arrive
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [OUT]   pdata:	filled with appropriate buffer
 * @param [IN]    tag:		tag to wait for
 * @return 0 on success, errno value on error
 */
int p9c_getreply(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t tag) {

	pthread_mutex_lock(&p9_handle->recv_lock);
	while (p9_handle->tags[tag].rdata == NULL) {
		pthread_cond_wait(&p9_handle->recv_cond, &p9_handle->recv_lock);
	}
	pthread_mutex_unlock(&p9_handle->recv_lock);

	*pdata = p9_handle->tags[tag].rdata;
	pthread_mutex_lock(&p9_handle->tag_lock);
	if (tag != P9_NOTAG)
		clear_bit(p9_handle->tags_bitmap, tag);
	pthread_cond_broadcast(&p9_handle->tag_cond);
	pthread_mutex_unlock(&p9_handle->tag_lock);

	return 0;
}

/**
 * @brief Signal we're done with the buffer and it can be used again
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    data:		buffer to reuse
 * @return 0 on success, errno value on error
 */
int p9c_putreply(struct p9_handle *p9_handle, msk_data_t *data) {
	int rc;

	rc = msk_post_recv(p9_handle->trans, data, p9_recv_cb, p9_recv_err_cb, NULL);
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

/**
 * @brief Get a fid structure ready to be used
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [OUT]   pfid:		fid to be filled
 * @return 0 on success, errno value on error
 */
int p9c_getfid(struct p9_handle *p9_handle, struct p9_fid **pfid) {
	struct p9_fid *fid;
	uint32_t fid_i;

	pthread_mutex_lock(&p9_handle->fid_lock);
	while ((fid_i = get_and_set_first_bit(p9_handle->fids_bitmap, p9_handle->max_fid)) == p9_handle->max_fid)
		pthread_cond_wait(&p9_handle->fid_cond, &p9_handle->fid_lock);
	pthread_mutex_unlock(&p9_handle->fid_lock);


	fid = bucket_get(p9_handle->fids_bucket);
	if (fid == NULL) {
		pthread_mutex_lock(&p9_handle->fid_lock);
		clear_bit(p9_handle->fids_bitmap, fid_i);
		pthread_cond_signal(&p9_handle->fid_cond);
		pthread_mutex_unlock(&p9_handle->fid_lock);
		return ENOMEM;
	}

	fid->fid = fid_i;
	fid->open = 0;
	*pfid = fid;
	return 0;
}

/**
 * @brief Release a fid after clunk
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to release
 * @return 0 on success, errno value on error
 */
int p9c_putfid(struct p9_handle *p9_handle, struct p9_fid *fid) {
	pthread_mutex_lock(&p9_handle->fid_lock);
	clear_bit(p9_handle->fids_bitmap, fid->fid);
	pthread_cond_signal(&p9_handle->fid_cond);
	pthread_mutex_unlock(&p9_handle->fid_lock);

	bucket_put(p9_handle->fids_bucket, fid);

	return 0;
}
