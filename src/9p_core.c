#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <assert.h>
#include <mooshika.h>
#include "9p.h"
#include "log.h"
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
	uint16_t tag;
//	int rc;

	data = NULL;
	*pdata = data;

	/* Do we need to find a tag */
	if (*ptag != P9_NOTAG) {
		pthread_mutex_lock(&p9_handle->tag_lock);
		while ((tag = get_and_set_first_bit(p9_handle->tags, p9_handle->max_tag)) == UINT32_MAX)
			pthread_cond_wait(&p9_handle->tag_cond, &p9_handle->tag_lock);
		pthread_mutex_unlock(&p9_handle->tag_lock);
		*ptag = tag;
	}
	return 0;
}

/**
 * @brief Send a buffer obtained through getbuffer
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    data:		buffer to send
 * @return 0 on success, errno value on error
 */
int p9c_sendrequest(struct p9_handle *p9_handle, msk_data_t *data) {
	// We need more recv buffers ready than requests pending
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

	if (tag != P9_NOTAG)
		clear_bit(p9_handle->tags, tag);
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
	return 0;
}
