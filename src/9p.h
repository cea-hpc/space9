#ifndef P9
#define P9

#include <dirent.h>     // MAXNAMLEN
#include <sys/param.h>  // MAXPATHLEN
#include <string.h>     // memset

#include "bitmap.h"
#include "bucket.h"

/* 9p-specific types */

/**
 * @brief Length prefixed string type
 *
 * The protocol uses length prefixed strings for all
 * string data, so we replicate that for our internal
 * string members.
 */

struct p9_str {
	uint16_t  len; /*< Length of the string */
	char *str; /*< The string */
};


/**
 * enum p9_qid - QID types
 * @P9_QTDIR: directory
 * @P9_QTAPPEND: append-only
 * @P9_QTEXCL: excluse use (only one open handle allowed)
 * @P9_QTMOUNT: mount points
 * @P9_QTAUTH: authentication file
 * @P9_QTTMP: non-backed-up files
 * @P9_QTSYMLINK: symbolic links (9P2000.u)
 * @P9_QTLINK: hard-link (9P2000.u)
 * @P9_QTFILE: normal files
 *
 * QID types are a subset of permissions - they are primarily
 * used to differentiate semantics for a file system entity via
 * a jump-table.  Their value is also the most signifigant 16 bits
 * of the permission_
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/sta
 */
enum {
	P9_QTDIR = 0x80,
	P9_QTAPPEND = 0x40,
	P9_QTEXCL = 0x20,
	P9_QTMOUNT = 0x10,
	P9_QTAUTH = 0x08,
	P9_QTTMP = 0x04,
	P9_QTSYMLINK = 0x02,
	P9_QTLINK = 0x01,
	P9_QTFILE = 0x00,
};

/**
 * @brief file system entity information
 *
 * qids are /identifiers used by 9P servers to track file system
 * entities.  The type is used to differentiate semantics for operations
 * on the entity (ie. read means something different on a directory than
 * on a file).  The path provides a server unique index for an entity
 * (roughly analogous to an inode number), while the version is updated
 * every time a file is modified and can be used to maintain cache
 * coherency between clients and serves.
 * Servers will often differentiate purely synthetic entities by setting
 * their version to 0, signaling that they should never be cached and
 * should be accessed synchronously.
 *
 * See Also://plan9.bell-labs.com/magic/man2html/2/sta
 */

typedef struct p9_qid {
	uint8_t type; /*< Type */
	uint32_t version; /*< Monotonically incrementing version number */
	uint64_t path; /*< Per-server-unique ID for a file system element */
} p9_qid_t;


/* library types */

struct p9_fid {
	uint32_t fid;
	char path[MAXPATHLEN];
	int pathlen;
	int open;
	struct p9_qid qid;
};

struct p9_tag {
	msk_data_t *rdata;
};

struct p9_handle {
	uint16_t max_tag;
	char aname[MAXPATHLEN];
	char hostname[MAX_CANON+1];
	uint8_t *rdmabuf;
	msk_trans_t *trans;
	struct ibv_mr *mr;
	msk_data_t *rdata;
	msk_data_t *wdata;
	pthread_mutex_t wdata_lock;
	pthread_cond_t wdata_cond;
	pthread_mutex_t recv_lock;
	pthread_cond_t recv_cond;
	pthread_mutex_t tag_lock;
	pthread_cond_t tag_cond;
	pthread_mutex_t fid_lock;
	pthread_mutex_t credit_lock;
	pthread_cond_t credit_cond;
	uint32_t credits;
	bitmap_t *wdata_bitmap;
	bitmap_t *tags_bitmap;
	struct p9_tag *tags;
	uint32_t max_fid;
	bitmap_t *fids_bitmap;
	bucket_t *fids_bucket;
	uint32_t nfids;
	uint32_t uid;
	uint32_t recv_num;
	uint32_t msize;
	uint32_t debug;
	uint32_t full_debug;
	struct p9_fid *root_fid;
	struct p9_fid *cwd;
};

struct fs_stats {
	uint32_t type;
	uint32_t bsize;
	uint64_t blocks;
	uint64_t bfree;
	uint64_t bavail;
	uint64_t filse;
	uint64_t ffree;
	uint64_t fsid;
	uint32_t namelen;
};

/* 9P Magic Numbers */
#define P9_NOTAG	(uint16_t)(~0)
#define P9_NOFID	(uint32_t)(~0)
#define P9_NONUNAME	(uint32_t)(~0)
#define P9_MAXWELEM	16

static inline void p9_get_tag(uint16_t *ptag, uint8_t *data) {
	memcpy(ptag, data + sizeof(uint32_t) /* msg len */ + sizeof(uint8_t) /* msg type */, sizeof(uint16_t));
}




// 9p_callbacks.c

void p9_disconnect_cb(msk_trans_t *trans);

void p9_recv_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg);
void p9_recv_cb(msk_trans_t *trans, msk_data_t *data, void *arg);
void p9_send_cb(msk_trans_t *trans, msk_data_t *data, void *arg);
void p9_send_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg);




// 9p_core.c


/**
 * @brief Get a buffer to fill that will be ok to send directly
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [OUT]   pdata:	filled with appropriate buffer
 * @param [OUT]   tag:		available tag to use in the reply. If set to P9_NOTAG, this is taken instead.
 * @return 0 on success, errno value on error
 */
int p9c_getbuffer(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t *ptag);

/**
 * @brief Send a buffer obtained through getbuffer
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    data:		buffer to send
 * @param [IN]    tag:		tag to use
 * @return 0 on success, errno value on error
 */
int p9c_sendrequest(struct p9_handle *p9_handle, msk_data_t *data, uint16_t tag);

/**
 * @brief Put the buffer back in the list of available buffers for use
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    data:		buffer to put back
 * @param [IN]    tag:		tag to put back
 * @return 0 on success, errno value on error
 */
int p9c_abortrequest(struct p9_handle *p9_handle, msk_data_t *data, uint16_t tag);


/**
 * @brief Waits for a reply with a given tag to arrive
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [OUT]   pdata:	filled with appropriate buffer
 * @param [IN]    tag:		tag to wait for
 * @return 0 on success, errno value on error
 */
int p9c_getreply(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t tag);

/**
 * @brief Signal we're done with the buffer and it can be used again
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    data:		buffer to reuse
 * @return 0 on success, errno value on error
 */
int p9c_putreply(struct p9_handle *p9_handle, msk_data_t *data);

/**
 * @brief Get a fid structure ready to be used
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [OUT]   pfid:		fid to be filled
 * @return 0 on success, errno value on error
 */
int p9c_getfid(struct p9_handle *p9_handle, struct p9_fid **pfid);

/**
 * @brief Release a fid after clunk
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to release
 * @return 0 on success, errno value on error
 */
int p9c_putfid(struct p9_handle *p9_handle, struct p9_fid *fid);

static inline int p9c_reg_mr(struct p9_handle *p9_handle, msk_data_t *data) {
	data->mr = msk_reg_mr(p9_handle->trans, data->data, data->max_size, IBV_ACCESS_LOCAL_WRITE);
	if (data->mr == NULL) {
		return -1;
	}

	return 0;
}

static inline int p9c_dereg_mr(msk_data_t *data) {
	return msk_dereg_mr(data->mr);
}


// 9p_init.c

int p9_init(struct p9_handle **pp9_handle, char *conf_file);
void p9_destroy(struct p9_handle **pp9_handle);


// 9p_libc.c
int p9l_open(struct p9_handle *p9_handle, struct p9_fid **pfid, char *path, uint32_t mode, uint32_t flags, uint32_t gid);
int p9l_cd(struct p9_handle *p9_handle, char *path);


#endif
