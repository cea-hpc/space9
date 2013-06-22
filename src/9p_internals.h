
#ifndef P9_INTERNALS
#define P9_INTERNALS

#include "space9.h"
#include "bitmap.h"
#include "bucket.h"

#if HAVE_MOOSHIKA
#else

#define IBV_ACCESS_LOCAL_WRITE 1

typedef union sockaddr_union {
	struct sockaddr sa;
	struct sockaddr_in sa_in;
	struct sockaddr_in6 sa_int6;
	struct sockaddr_storage sa_stor;
} sockaddr_union_t;

typedef struct msk_ctx msk_ctx_t;
typedef struct msk_trans_attr msk_trans_attr_t;
typedef struct msk_trans msk_trans_t;
typedef void (*ctx_callback_t)(msk_trans_t *trans, msk_data_t *data, void *arg);
typedef void (*disconnect_callback_t) (msk_trans_t *trans);

/**
 * \struct msk_trans
 * RDMA transport instance
 */
struct msk_trans {
	enum msk_state {
		MSK_INIT,
		MSK_LISTENING,
		MSK_ADDR_RESOLVED,
		MSK_ROUTE_RESOLVED,
		MSK_CONNECT_REQUEST,
		MSK_CONNECTED,
		MSK_CLOSED,
		MSK_ERROR
	} state;			/**< tracks the transport state machine for connection setup and tear down */
	struct rdma_cm_id *cm_id;	/**< The RDMA CM ID */
	struct rdma_event_channel *event_channel;
	struct ibv_comp_channel *comp_channel;
	struct ibv_pd *pd;		/**< Protection Domain pointer */
	struct ibv_qp *qp;		/**< Queue Pair pointer */
	struct ibv_cq *cq;		/**< Completion Queue pointer */
	disconnect_callback_t disconnect_callback;
	void *private_data;
	long timeout;			/**< Number of mSecs to wait for connection management events */
	int sq_depth;			/**< The depth of the Send Queue */
	int max_send_sge;		/**< Maximum number of s/g elements per send */
	int rq_depth;			/**< The depth of the Receive Queue. */
	int max_recv_sge;		/**< Maximum number of s/g elements per recv */
	sockaddr_union_t addr;		/**< The remote peer's address */
	int server;			/**< 0 if client, number of connections to accept on server, -1 (MSK_SERVER_CHILD) if server's accepted connection */
	struct rdma_cm_id **conn_requests; /**< temporary child cm_id, only used for server */
	msk_ctx_t *send_buf;		/**< pointer to actual context data */
	msk_ctx_t *recv_buf;		/**< pointer to actual context data */
	pthread_mutex_t ctx_lock;	/**< lock for contexts */
	pthread_cond_t ctx_cond;	/**< cond for contexts */
	pthread_mutex_t cm_lock;	/**< lock for connection events */
	pthread_cond_t cm_cond;		/**< cond for connection events */
	struct ibv_recv_wr *bad_recv_wr;
	struct ibv_send_wr *bad_send_wr;
};

struct msk_trans_attr {
	disconnect_callback_t disconnect_callback;
	int debug;			/**< verbose output to stderr if set */
	int server;			/**< 0 if client, number of connections to accept on server */
	long timeout;			/**< Number of mSecs to wait for connection management events */
	int sq_depth;			/**< The depth of the Send Queue */
	int max_send_sge;		/**< Maximum number of s/g elements per send */
	int rq_depth;			/**< The depth of the Receive Queue. */
	int max_recv_sge;		/**< Maximum number of s/g elements per recv */
	int worker_count;		/**< Number of worker threads - works only for the first init */
	int worker_queue_size;		/**< Size of the worker data queue - works only for the first init */
	sockaddr_union_t addr;		/**< The remote peer's address */
	struct ibv_pd *pd;		/**< Protection Domain pointer */
};


/**
 * \struct msk_rloc
 * stores one remote address to write/read at
 */
typedef struct msk_rloc {
	uint64_t raddr; /**< remote memory address */
	uint32_t rkey; /**< remote key */
	uint32_t size; /**< size of the region we can write/read */
} msk_rloc_t;

#endif

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

struct p9_net_ops {
	int (*init)(msk_trans_t **ptrans, msk_trans_attr_t *attr);
	void (*destroy_trans)(msk_trans_t **ptrans);

	int (*connect)(msk_trans_t *trans);
	int (*finalize_connect)(msk_trans_t *trans);

	struct ibv_mr *(*reg_mr)(msk_trans_t *trans, void *memaddr, size_t size, int access);
	int (*dereg_mr)(struct ibv_mr *mr);

	int (*post_n_recv)(msk_trans_t *trans, msk_data_t *data, int num_sge, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg);
	int (*post_n_send)(msk_trans_t *trans, msk_data_t *data, int num_sge, ctx_callback_t callback, ctx_callback_t err_callback, void *callback_arg);

};

struct p9_handle {
	uint16_t max_tag;
	char aname[MAXPATHLEN];
	char hostname[MAX_CANON+1];
	uint8_t *rdmabuf;
	struct p9_net_ops *net_ops;
	msk_trans_t *trans;
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
	uint32_t umask;
	struct p9_fid *root_fid;
	struct p9_fid *cwd;
};


// 9p_callbacks.c

void p9_disconnect_cb(msk_trans_t *trans);

void p9_recv_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg);
void p9_recv_cb(msk_trans_t *trans, msk_data_t *data, void *arg);
void p9_send_cb(msk_trans_t *trans, msk_data_t *data, void *arg);
void p9_send_err_cb(msk_trans_t *trans, msk_data_t *data, void *arg);


#endif