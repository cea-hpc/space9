#ifndef P9_PROTO
#define P9_PROTO


/**
 * @brief Must be used first uppon connexion:
 * It is needed for client/server to agree on a msize, and to define the protocol version used (always "9P2000.L")
 *
 * This is done by default on init.
 *
 *
 * size[4] Tversion tag[2] msize[4] version[s]
 * size[4] Rversion tag[2] msize[4] version[s] 
 *
 * @param [INOUT] p9_handle: used to define the msize, which value is updated on success.
 * @return 0 on success, errno value on error.
 */
int p9_version(struct p9_handle *p9_handle);

/**
 * @brief Not implemented on either side, would be used with p9_attach to setup an authentification
 *
 *
 * size[4] Tauth tag[2] afid[4] uname[s] aname[s] n_uname[4]
 * size[4] Rauth tag[2] aqid[13]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/**
 * @brief Attach a mount point for a given user
 * Not authentification yet.
 *
 * This is also done on init, the fid 0 is always populated.
 *
 *
 * size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] n_uname[4]
 * size[4] Rattach tag[2] qid[13]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    uid:		uid to use
 * @param [OUT]   fid:		initial fid to populate
 * @return 0 on success, errno value on error.
 */
int p9_attach(struct p9_handle *p9_handle, uint32_t uid, struct p9_fid **pfid);


/**
 * @brief Flush is used to invalidate a tag, if the reply isn't needed anymore.
 *
 *
 * size[4] Tflush tag[2] oldtag[2]
 * size[4] Rflush tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    tag:		the tag to invalidate
 * @return 0 on success, errno value on error.
 */
int p9_flush(struct p9_handle *p9_handle, uint16_t tag);

/**
 * @brief Creates a new fid from path relative to a fid, or clone the said fid
 *
 *
 * size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
 * size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		existing fid to use
 * @param [IN]    path:		path to be based on. if NULL, clone the fid
 * @param [OUT]   pnewfid:	new fid to use
 * @return 0 on success, errno value on error.
 */
int p9_walk(struct p9_handle *p9_handle, struct p9_fid *fid, char *path, struct p9_fid **pnewfid);

/**
 * @brief Read from a file.
 * Even if count is > msize, more won't be received
 *
 *
 * size[4] Tread tag[2] fid[4] offset[8] count[4]
 * size[4] Rread tag[2] count[4] data[count]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to use
 * @param [IN]    offset:	offset from which to read
 * @param [IN]    count:	count of bytes to read
 * @param [OUT]   buffer:	data is copied there.
 *                This is $#@!^ inefficient, come up with a release mechanism to give just a pointer here.
 *                Cannot use the data directly to post a recv because recv order isn't controlled
 *                Where's our rdma write, huh?
 * @return number of bytes read if >= 0, -errno on error.
 *          0 indicates eof?
 */
int p9_read(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t offset, uint32_t count, char *data);

/**
 * @brief Write to a file.
 * Even if count is > msize, more won't be written
 *
 *
 * size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
 * size[4] Rwrite tag[2] count[4]
 * 
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to use
 * @param [IN]    offset:	offset from which to write
 * @param [IN]    count:	number of bytes to write
 * @param [IN]    buffer:	data to send
 * @return number of bytes written if >= 0, -errno on error
 */
int p9_write(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t offset, uint32_t count, char *data);

/**
 * @brief Clunk a fid.
 * Note that even on error, the fid is no longer valid after a clunk.
 *
 *
 * size[4] Tclunk tag[2] fid[4]
 * size[4] Rclunk tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to clunk
 * @return 0 on success, errno value on error.
 */
int p9_clunk(struct p9_handle *p9_handle, struct p9_fid *fid);

/**
 * @brief Clunk a fid and unlinks the file associated with it.
 * Note that the fid is clunked even on error.
 *
 *
 * size[4] Tremove tag[2] fid[4]
 * size[4] Rremove tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to remove
 * @return 0 on success, errno value on error.
 */
int p9_remove(struct p9_handle *p9_handle, struct p9_fid *fid);

/**
 * @brief Get filesystem information.
 *
 *
 * size[4] Tstatfs tag[2] fid[4]
 * size[4] Rstatfs tag[2] type[4] bsize[4] blocks[8] bfree[8] bavail[8]
 *                        files[8] ffree[8] fsid[8] namelen[4]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		gets the stats of the filesystem this fid belongs to
 * @param [OUT]   fs_stats:	Filled with infos. Must be non-NULL.
 * @return 0 on success, errno value on error.
 */
int statfs(struct p9_handle *p9_handle, struct p9_fid *fid, struct fs_stats *fs_stats);

/**
 * @brief Open a file by its fid
 *
 *
 * size[4] Tlopen tag[2] fid[4] flags[4]
 * size[4] Rlopen tag[2] qid[13] iounit[4]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to open
 * @param [IN]    flags:	open flags as described in Linux open(2): O_RDONLY, O_RDWR, O_WRONLY, etc.
 * @param [OUT]   qid:		qid set if non-NULL
 * @param [OUT]   iounit:	iounit set if non-NULL. This is the maximum size for a single read or write if not 0.
 *                              FIXME: useless imo, we know the msize and can compute this as cleverly as the server.
 *                              currently, ganesha sets this to 0 anyway.
 * @return 0 on success, errno value on error.
 */
int p9_lopen(struct p9_handle *p9_handle, struct p9_fid *fid, uint32_t flags, struct p9_qid *qid, uint32_t *iounit);

/**
 * @brief Create a new file and open it.
 * This will fail if the file already exists.
 *
 *
 * size[4] Tlcreate tag[2] dfid[4] name[s] flags[4] mode[4] gid[4]
 * size[4] Rlcreate tag[2] qid[13] iounit[4]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    dfid:		fid of the directory where to create the new file
 * @param [IN]    name:		name of the new file
 * @param [IN]    flags:	Linux kernel intent bits
 * @param [IN]    mode:		Linux creat(2) mode bits
 * @param [IN]    gid:		effective gid
 * @param [OUT]   qid:		qid to fill if non-NULL
 * @param [OUT]   iounit:	iounit to set if non-NULL
 * @return 0 on success, errno value on error.
 */
int p9_lcreate(struct p9_handle *p9_handle, uint32_t dfid, char *name, uint32_t flags, uint32_t mode,
               uint32_t gid, struct p9_qid *qid, uint32_t *iounit);

/**
 * @brief Create a symlink
 *
 *
 * size[4] Tsymlink tag[2] dfid[4] name[s] symtgt[s] gid[4]
 * size[4] Rsymlink tag[2] qid[13]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    dfid:		fid of the directory where the new symlink will be created
 * @param [IN]    name:		name of the link
 * @param [IN]    symtgt:	link target
 * @param [IN]    gid:		effective gid
 * @param [OUT]   qid:		qid to fill if non-NULL
 * @return 0 on success, errno value on error.
 */
int p9_symlink(struct p9_handle *p9_handle, uint32_t dfid, char *name, char *symtgt, uint32_t gid,
               struct p9_qid *qid);

/**
 * @brief mknod.
 *
 *
 * size[4] Tmknod tag[2] dfid[4] name[s] mode[4] major[4] minor[4] gid[4]
 * size[4] Rmknod tag[2] qid[13]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    dfid:		fid of the directory where to create the node
 * @param [IN]    name:		name of the node
 * @param [IN]    mode:		Linux mknod(2) mode bits.
 * @param [IN]    major:	major number
 * @param [IN]    minor:	minor number
 * @param [IN]    gid:		effective gid
 * @param [OUT]   qid:		qid to fill if non-NULL
 * @return 0 on success, errno value on error.
 */
int p9_mknod(struct p9_handle *p9_handle, uint32_t dfid, char *name, uint32_t mode, uint32_t major, uint32_t minor,
             uint32_t gid, struct p9_qid *qid);

/**
 * @brief Move the file associated with fid
 *
 *
 * size[4] Trename tag[2] fid[4] dfid[4] name[s]
 * size[4] Rrename tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		source fid
 * @param [IN]    dfid:		destination directory
 * @param [IN]    name:		destination name
 * @return 0 on success, errno value on error.
 */
int p9_rename(struct p9_handle *p9_handle, struct p9_fid *fid, uint32_t dfid, char *name);

/**
 * @brief readlink.
 *
 *
 * size[4] Treadlink tag[2] fid[4]
 * size[4] Rreadlink tag[2] target[s]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid of the link
 * @param [OUT]   target:	content of the link
 * @param [IN]    size:		size of the target buffer
 * @return 0 on success, errno value on error.
 */
int p9_readlink(struct p9_handle *p9_handle, struct p9_fid *fid, char *target, uint32_t size);

/** p9_getattr
 *
 *
 * size[4] Tgetattr tag[2] fid[4] request_mask[8]
 * size[4] Rgetattr tag[2] valid[8] qid[13] mode[4] uid[4] gid[4] nlink[8]
 *                  rdev[8] size[8] blksize[8] blocks[8]
 *                  atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
 *                  ctime_sec[8] ctime_nsec[8] btime_sec[8] btime_nsec[8]
 *                  gen[8] data_version[8]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_setattr
 *
 *
 * size[4] Tsetattr tag[2] fid[4] valid[4] mode[4] uid[4] gid[4] size[8]
 *                  atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
 * size[4] Rsetattr tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_xattrwalk
 *
 *
 * size[4] Txattrwalk tag[2] fid[4] newfid[4] name[s]
 * size[4] Rxattrwalk tag[2] size[8]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_xattrcreate
 *
 *
 * size[4] Txattrcreate tag[2] fid[4] name[s] attr_size[8] flags[4]
 * size[4] Rxattrcreate tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_readdir
 *
 *
 * size[4] Treaddir tag[2] fid[4] offset[8] count[4]
 * size[4] Rreaddir tag[2] count[4] data[count]
 *   data is: qid[13] offset[8] type[1] name[s]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_fsync
 *
 *
 * size[4] Tfsync tag[2] fid[4]
 * size[4] Rfsync tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_lock
 *
 *
 * size[4] Tlock tag[2] fid[4] type[1] flags[4] start[8] length[8] proc_id[4] client_id[s]
 * size[4] Rlock tag[2] status[1]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_getlock
 *
 *
 * size[4] Tgetlock tag[2] fid[4] type[1] start[8] length[8] proc_id[4] client_id[s]
 * size[4] Rgetlock tag[2] type[1] start[8] length[8] proc_id[4] client_id[s]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_link
 *
 *
 * size[4] Tlink tag[2] dfid[4] fid[4] name[s]
 * size[4] Rlink tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_mkdir
 *
 *
 * size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4]
 * size[4] Rmkdir tag[2] qid[13]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_renameat
 *
 *
 * size[4] Trenameat tag[2] olddirfid[4] oldname[s] newdirfid[4] newname[s]
 * size[4] Rrenameat tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */

/** p9_unlinkat
 *
 *
 * size[4] Tunlinkat tag[2] dirfd[4] name[s] flags[4]
 * size[4] Runlinkat tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param
 * @return 0 on success, errno value on error.
 */




#endif
