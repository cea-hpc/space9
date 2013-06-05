#ifndef P9_PROTO_INTERNALS
#define P9_PROTO_INTERNALS

#define P9_HDR_SIZE  4
#define P9_TYPE_SIZE 1
#define P9_TAG_SIZE  2
#define P9_STD_HDR_SIZE (P9_HDR_SIZE + P9_TYPE_SIZE + P9_TAG_SIZE)

/**
 * enum p9_msg_t - 9P message types
 * @P9_TLERROR: not used
 * @P9_RLERROR: response for any failed request for 9P2000.L
 * @P9_TSTATFS: file system status request
 * @P9_RSTATFS: file system status response
 * @P9_TSYMLINK: make symlink request
 * @P9_RSYMLINK: make symlink response
 * @P9_TMKNOD: create a special file object request
 * @P9_RMKNOD: create a special file object response
 * @P9_TLCREATE: prepare a handle for I/O on an new file for 9P2000.L
 * @P9_RLCREATE: response with file access information for 9P2000.L
 * @P9_TRENAME: rename request
 * @P9_RRENAME: rename response
 * @P9_TMKDIR: create a directory request
 * @P9_RMKDIR: create a directory response
 * @P9_TVERSION: version handshake request
 * @P9_RVERSION: version handshake response
 * @P9_TAUTH: request to establish authentication channel
 * @P9_RAUTH: response with authentication information
 * @P9_TATTACH: establish user access to file service
 * @P9_RATTACH: response with top level handle to file hierarchy
 * @P9_TERROR: not used
 * @P9_RERROR: response for any failed request
 * @P9_TFLUSH: request to abort a previous request
 * @P9_RFLUSH: response when previous request has been cancelled
 * @P9_TWALK: descend a directory hierarchy
 * @P9_RWALK: response with new handle for position within hierarchy
 * @P9_TOPEN: prepare a handle for I/O on an existing file
 * @P9_ROPEN: response with file access information
 * @P9_TCREATE: prepare a handle for I/O on a new file
 * @P9_RCREATE: response with file access information
 * @P9_TREAD: request to transfer data from a file or directory
 * @P9_RREAD: response with data requested
 * @P9_TWRITE: reuqest to transfer data to a file
 * @P9_RWRITE: response with out much data was transfered to file
 * @P9_TCLUNK: forget about a handle to an entity within the file system
 * @P9_RCLUNK: response when server has forgotten about the handle
 * @P9_TREMOVE: request to remove an entity from the hierarchy
 * @P9_RREMOVE: response when server has removed the entity
 * @P9_TSTAT: request file entity attributes
 * @P9_RSTAT: response with file entity attributes
 * @P9_TWSTAT: request to update file entity attributes
 * @P9_RWSTAT: response when file entity attributes are updated
 *
 * There are 14 basic operations in 9P2000, paired as
 * requests and responses.  The one special case is ERROR
 * as there is no @P9_TERROR request for clients to transmit to
 * the server, but the server may respond to any other reques
 * with an @P9_RERROR.
 *
 * See Also: http://plan9.bell-labs.com/sys/man/5/INDEX.html
 */

enum p9_msgtype {
	P9_TLERROR = 6,
	P9_RLERROR,
	P9_TSTATFS = 8,
	P9_RSTATFS,
	P9_TLOPEN = 12,
	P9_RLOPEN,
	P9_TLCREATE = 14,
	P9_RLCREATE,
	P9_TSYMLINK = 16,
	P9_RSYMLINK,
	P9_TMKNOD = 18,
	P9_RMKNOD,
	P9_TRENAME = 20,
	P9_RRENAME,
	P9_TREADLINK = 22,
	P9_RREADLINK,
	P9_TGETATTR = 24,
	P9_RGETATTR,
	P9_TSETATTR = 26,
	P9_RSETATTR,
	P9_TXATTRWALK = 30,
	P9_RXATTRWALK,
	P9_TXATTRCREATE = 32,
	P9_RXATTRCREATE,
	P9_TREADDIR = 40,
	P9_RREADDIR,
	P9_TFSYNC = 50,
	P9_RFSYNC,
	P9_TLOCK = 52,
	P9_RLOCK,
	P9_TGETLOCK = 54,
	P9_RGETLOCK,
	P9_TLINK = 70,
	P9_RLINK,
	P9_TMKDIR = 72,
	P9_RMKDIR,
	P9_TRENAMEAT = 74,
	P9_RRENAMEAT,
	P9_TUNLINKAT = 76,
	P9_RUNLINKAT,
	P9_TVERSION = 100,
	P9_RVERSION,
	P9_TAUTH = 102,
	P9_RAUTH,
	P9_TATTACH = 104,
	P9_RATTACH,
	P9_TERROR = 106,
	P9_RERROR,
	P9_TFLUSH = 108,
	P9_RFLUSH,
	P9_TWALK = 110,
	P9_RWALK,
	P9_TOPEN = 112,
	P9_ROPEN,
	P9_TCREATE = 114,
	P9_RCREATE,
	P9_TREAD = 116,
	P9_RREAD,
	P9_TWRITE = 118,
	P9_RWRITE,
	P9_TCLUNK = 120,
	P9_RCLUNK,
	P9_TREMOVE = 122,
	P9_RREMOVE,
	P9_TSTAT = 124,
	P9_RSTAT,
	P9_TWSTAT = 126,
	P9_RWSTAT,
};


/* Various header lengths to check message sizes : */

/* size[4] Rread tag[2] count[4] data[count] */
#define P9_ROOM_RREAD (P9_STD_HDR_SIZE + 4 )

/* size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count] */
#define P9_ROOM_TWRITE (P9_STD_HDR_SIZE + 4 + 8 + 4)

/* size[4] Rreaddir tag[2] count[4] data[count] */
#define P9_ROOM_RREADDIR (P9_STD_HDR_SIZE + 4 )


typedef struct p9_fid__
{
  uint32_t                fid;
  p9_qid_t               qid;
  char                    name[MAXPATHLEN];
  union
    {
       uint32_t      iounit;
       struct p9_xattr_desc
        {
          unsigned int xattr_id;
          caddr_t      xattr_content;
        } xattr;
    } specdata;
} p9_fid_t;

#define p9_getheader( __cursor, __var)    \
do {                                      \
  __cursor += P9_HDR_SIZE;                \
  __var = *(uint8_t*)__cursor;             \
  __cursor += P9_TYPE_SIZE + P9_TAG_SIZE; \
} while( 0 )

#define p9_getptr( __cursor, __pvar, __type ) \
do {                                          \
  __pvar = (__type *)__cursor;                \
  __cursor += sizeof( __type );               \
} while( 0 )

#define p9_getvalue( __cursor, __var, __type ) \
do {                                           \
  __var = *(__type *)__cursor;                 \
  __cursor += sizeof( __type );                \
} while( 0 )

#define p9_getstr( __cursor, __len, __str )   \
do {                                          \
  __len = *(uint16_t *)__cursor;              \
  __cursor += sizeof( uint16_t );             \
  __str = (char*)__cursor;                    \
  __cursor += __len;                          \
} while( 0 )

#define p9_setptr( __cursor, __pvar, __type ) \
do {                                          \
  *((__type *)__cursor) = *__pvar;            \
  __cursor += sizeof( __type );               \
} while( 0 )

#define p9_setvalue( __cursor, __var, __type ) \
do {                                           \
  *((__type *)__cursor) = __var;               \
  __cursor += sizeof( __type );                \
} while( 0 )

#define p9_savepos( __cursor, __savedpos, __type ) \
do {                                               \
  __savedpos = __cursor;                           \
  __cursor += sizeof( __type );                    \
} while ( 0 )

/* Insert a qid */
#define p9_setqid( __cursor, __qid )       \
do {                                       \
  *((uint8_t *)__cursor) = __qid.type;     \
  __cursor += sizeof( uint8_t );           \
  *((uint32_t *)__cursor) = __qid.version; \
  __cursor += sizeof( uint32_t );          \
  *((uint64_t *)__cursor) = __qid.path;    \
  __cursor += sizeof( uint64_t );          \
} while( 0 )

#define p9_skipqid(__cursor)                   \
do {                                           \
  __cursor += sizeof( uint8_t )                \
    + sizeof( uint32_t ) + sizeof( uint64_t ); \
} while( 0 )

#define p9_getqid( __cursor, __qid )       \
do {                                       \
  (__qid).type = *((uint8_t *)__cursor);     \
  __cursor += sizeof( uint8_t );           \
  (__qid).version = *((uint32_t *)__cursor); \
  __cursor += sizeof( uint32_t );          \
  (__qid).path = *((uint64_t *)__cursor);    \
  __cursor += sizeof(uint64_t);            \
} while( 0 )


/* Insert a non-null terminated string */
#define p9_setstr( __cursor, __len, __str ) \
do {                                        \
  *((uint16_t *)__cursor) = __len;          \
  __cursor += sizeof( uint16_t );           \
  memcpy( __cursor, __str, __len );         \
  __cursor += __len;                        \
} while( 0 )

/* p9_setbuffer :
 * Copy data from __buffer into the reply,
 * with a length uint32_t header.
 */
#define p9_setbuffer( __cursor, __len, __buffer ) \
do {                                              \
  *((uint32_t *)__cursor) = __len;                \
  __cursor += sizeof( uint32_t );                 \
  memcpy( __cursor, __buffer, __len );            \
  __cursor += __len;                              \
} while( 0 )

#define p9_initcursor( __cursor, __start, __msgtype, __tag ) \
do {                                                         \
  __cursor = __start + P9_HDR_SIZE;                          \
  *((uint8_t *)__cursor) = __msgtype;                        \
  __cursor += sizeof( uint8_t );                             \
  *((uint16_t *)__cursor) = __tag;                           \
  __cursor += sizeof( uint16_t );                            \
} while( 0 )

/* p9_setmsglen :
 * Calculate message size, and write this value in the
 * header of the 9p message.
 */
#define p9_setmsglen( __cursor, __start )                   \
do {                                                        \
  *((uint32_t *)__start) =  (uint32_t)(__cursor - __start); \
} while( 0 )


/* p9_checkbound :
 * Check that the message size is less than *__maxlen,
 * AND set *__maxlen to actual message size.
 */
#define p9_checkbound( __cursor, __start, __maxlen ) \
do {                                                 \
if( (uint32_t)( __cursor - __start ) > *__maxlen )   \
  return -1;                                         \
else                                                 \
   *__maxlen = (uint32_t)( __cursor - __start ) ;    \
} while( 0 )

/* Bit values for getattr valid field.
 */
#define P9_GETATTR_MODE		0x00000001ULL
#define P9_GETATTR_NLINK	0x00000002ULL
#define P9_GETATTR_UID		0x00000004ULL
#define P9_GETATTR_GID		0x00000008ULL
#define P9_GETATTR_RDEV		0x00000010ULL
#define P9_GETATTR_ATIME	0x00000020ULL
#define P9_GETATTR_MTIME	0x00000040ULL
#define P9_GETATTR_CTIME	0x00000080ULL
#define P9_GETATTR_INO		0x00000100ULL
#define P9_GETATTR_SIZE		0x00000200ULL
#define P9_GETATTR_BLOCKS	0x00000400ULL

#define P9_GETATTR_BTIME	0x00000800ULL
#define P9_GETATTR_GEN		0x00001000ULL
#define P9_GETATTR_DATA_VERSION	0x00002000ULL

#define P9_GETATTR_BASIC	0x000007ffULL /* Mask for fields up to BLOCKS */
#define P9_GETATTR_ALL		0x00003fffULL /* Mask for All fields above */

/* Bit values for setattr valid field from <linux/fs.h>.
 */
#define P9_SETATTR_MODE		0x00000001UL
#define P9_SETATTR_UID		0x00000002UL
#define P9_SETATTR_GID		0x00000004UL
#define P9_SETATTR_SIZE		0x00000008UL
#define P9_SETATTR_ATIME	0x00000010UL
#define P9_SETATTR_MTIME	0x00000020UL
#define P9_SETATTR_CTIME	0x00000040UL
#define P9_SETATTR_ATIME_SET	0x00000080UL
#define P9_SETATTR_MTIME_SET	0x00000100UL

/* Bit values for lock type.
 */
#define P9_LOCK_TYPE_RDLCK 0
#define P9_LOCK_TYPE_WRLCK 1
#define P9_LOCK_TYPE_UNLCK 2

/* Bit values for lock status.
 */
#define P9_LOCK_SUCCESS 0
#define P9_LOCK_BLOCKED 1
#define P9_LOCK_ERROR 2
#define P9_LOCK_GRACE 3

/* Bit values for lock flags.
 */
#define P9_LOCK_FLAGS_BLOCK 1
#define P9_LOCK_FLAGS_RECLAIM 2

/* Structures for Protocol Operations
 * These cannot be used directly because of structure packing, and are here for informative purpose only.
 */
/* header, common to all others */
struct p9_header {
	uint32_t len;
	uint8_t msgtype;
	uint16_t tag;
};

struct p9_rerror {
	uint32_t ecode;
};
struct p9_tstatfs {
	uint32_t fid;
};
struct p9_rstatfs {
	uint32_t type;
	uint32_t bsize;
	uint64_t blocks;
	uint64_t bfree;
	uint64_t bavail;
	uint64_t files;
	uint64_t ffree;
	uint64_t fsid;
	uint32_t namelen;
};
struct p9_tlopen {
	uint32_t fid;
	uint32_t flags;
};
struct p9_rlopen {
	struct p9_qid qid;
	uint32_t iounit;
};
struct p9_tlcreate {
	uint32_t fid;
	struct p9_str name;
	uint32_t flags;
	uint32_t mode;
	uint32_t gid;
};
struct p9_rlcreate {
	struct p9_qid qid;
	uint32_t iounit;
};
struct p9_tsymlink {
	uint32_t fid;
	struct p9_str name;
	struct p9_str symtgt;
	uint32_t gid;
};
struct p9_rsymlink {
	struct p9_qid qid;
};
struct p9_tmknod {
	uint32_t fid;
	struct p9_str name;
	uint32_t mode;
	uint32_t major;
	uint32_t minor;
	uint32_t gid;
};
struct p9_rmknod {
	struct p9_qid qid;
};
struct p9_trename {
	uint32_t fid;
	uint32_t dfid;
	struct p9_str name;
};
struct p9_rrename {
};
struct p9_treadlink {
	uint32_t fid;
};
struct p9_rreadlink {
	struct p9_str target;
};
struct p9_tgetattr {
	uint32_t fid;
	uint64_t request_mask;
};
struct p9_rgetattr {
	uint64_t valid;
	struct p9_qid qid;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint64_t nlink;
	uint64_t rdev;
	uint64_t size;
	uint64_t blksize;
	uint64_t blocks;
	uint64_t atime_sec;
	uint64_t atime_nsec;
	uint64_t mtime_sec;
	uint64_t mtime_nsec;
	uint64_t ctime_sec;
	uint64_t ctime_nsec;
	uint64_t btime_sec;
	uint64_t btime_nsec;
	uint64_t gen;
	uint64_t data_version;
};
struct p9_tsetattr {
	uint32_t fid;
	uint32_t valid;
	uint32_t mode;
	uint32_t uid;
	uint32_t gid;
	uint64_t size;
	uint64_t atime_sec;
	uint64_t atime_nsec;
	uint64_t mtime_sec;
	uint64_t mtime_nsec;
};
struct p9_rsetattr {
};
struct p9_txattrwalk {
	uint32_t fid;
	uint32_t attrfid;
	struct p9_str name;
};
struct p9_rxattrwalk {
	uint64_t size;
};
struct p9_txattrcreate {
	uint32_t fid;
	struct p9_str name;
	uint64_t size;
	uint32_t flag;
};
struct p9_rxattrcreate {
};
struct p9_treaddir {
	uint32_t fid;
	uint64_t offset;
	uint32_t count;
};
struct p9_rreaddir {
	uint32_t count;
	uint8_t *data;
};
struct p9_tfsync {
	uint32_t fid;
};
struct p9_rfsync {
};
struct p9_tlock {
	uint32_t fid;
	uint8_t type;
	uint32_t flags;
	uint64_t start;
	uint64_t length;
	uint32_t proc_id;
	struct p9_str client_id;
};
struct p9_rlock {
	uint8_t status;
};
struct p9_tgetlock {
	uint32_t fid;
	uint8_t type;
	uint64_t start;
	uint64_t length;
	uint32_t proc_id;
	struct p9_str client_id;
};
struct p9_rgetlock {
	uint8_t type;
	uint64_t start;
	uint64_t length;
	uint32_t proc_id;
	struct p9_str client_id;
};
struct p9_tlink {
	uint32_t dfid;
	uint32_t fid;
	struct p9_str name;
};
struct p9_rlink {
};
struct p9_tmkdir {
	uint32_t fid;
	struct p9_str name;
	uint32_t mode;
	uint32_t gid;
};
struct p9_rmkdir {
	struct p9_qid qid;
};
struct p9_trenameat {
	uint32_t olddirfid;
	struct p9_str oldname;
	uint32_t newdirfid;
	struct p9_str newname;
};
struct p9_rrenameat {
};
struct p9_tunlinkat {
	uint32_t dirfid;
	struct p9_str name;
	uint32_t flags;
};
struct p9_runlinkat {
};
struct p9_tawrite {
	uint32_t fid;
	uint8_t datacheck;
	uint64_t offset;
	uint32_t count;
	uint32_t rsize;
	uint8_t *data;
	uint32_t check;
};
struct p9_rawrite {
	uint32_t count;
};
struct p9_tversion {
	uint32_t  msize;
	struct p9_str version;
};
struct p9_rversion {
	uint32_t msize;
	struct p9_str version;
};
struct p9_tauth {
	uint32_t afid;
	struct p9_str uname;
	struct p9_str aname;
	uint32_t n_uname;		/* 9P2000.u extensions */
};
struct p9_rauth {
	struct p9_qid qid;
};
struct p9_tflush {
	uint16_t oldtag;
};
struct p9_rflush {
};
struct p9_tattach {
	uint32_t fid;
	uint32_t afid;
	struct p9_str uname;
	struct p9_str aname;
	uint32_t n_uname;		/* 9P2000.u extensions */
};
struct p9_rattach {
	struct p9_qid qid;
};
struct p9_twalk {
	uint32_t fid;
	uint32_t newfid;
	uint16_t nwname;
	struct p9_str wnames[P9_MAXWELEM];
};
struct p9_rwalk {
	uint16_t nwqid;
	struct p9_qid wqids[P9_MAXWELEM];
};
struct p9_topen {
	uint32_t fid;
	uint8_t mode;
};
struct p9_ropen {
	struct p9_qid qid;
	uint32_t iounit;
};
struct p9_tcreate {
	uint32_t fid;
	struct p9_str name;
	uint32_t perm;
	uint8_t mode;
	struct p9_str extension;
};
struct p9_rcreate {
	struct p9_qid qid;
	uint32_t iounit;
};
struct p9_tread {
	uint32_t fid;
	uint64_t offset;
	uint32_t count;
};
struct p9_rread {
	uint32_t count;
	uint8_t *data;
};
struct p9_twrite {
	uint32_t fid;
	uint64_t offset;
	uint32_t count;
	uint8_t *data;
};
struct p9_rwrite {
	uint32_t count;
};
struct p9_tclunk {
	uint32_t fid;
};
struct p9_rclunk {
};
struct p9_tremove {
	uint32_t fid;
};
struct p9_rremove {
};
union p9_tmsg {
};

#endif
