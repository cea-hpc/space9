#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <inttypes.h> // PRIu64
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <mooshika.h>
#include "9p.h"
#include "9p_proto.h"
#include "9p_proto_internals.h"
#include "utils.h"

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
int p9p_version(struct p9_handle *p9_handle) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;
	char *version;
	uint8_t msgtype;
	uint16_t len;

	/* Sanity check */
	if (p9_handle == NULL)
		return EINVAL;

	/* Version always done with P9_NOTAG, give the hint to getbuffer */
	tag = P9_NOTAG;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TVERSION, P9_NOTAG);
	p9_setvalue(cursor, p9_handle->msize, uint32_t);
	p9_setstr(cursor, 8 /*strlen("9P2000.L")*/, "9P2000.L");
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RVERSION:
			p9_getvalue(cursor, p9_handle->msize, uint32_t);
			p9_getstr(cursor, len, version);
			if (strncmp(version, "9P2000.L", len)) {
				rc = EINVAL;
			}
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TVERSION, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;	
}

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
int p9p_attach(struct p9_handle *p9_handle, uint32_t uid, struct p9_fid **pfid) {
	int rc;
	uint8_t msgtype;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;
	struct p9_fid *fid;

	/* Sanity check */
	if (p9_handle == NULL || pfid == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	rc = p9c_getfid(p9_handle, &fid);
	// FIXME: free buffer
	if (rc)
		return rc;

	p9_initcursor(cursor, data->data, P9_TATTACH, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, P9_NOFID, uint32_t);
	p9_setstr(cursor, 0, "");
	p9_setstr(cursor, strlen(p9_handle->aname), p9_handle->aname);
#ifdef ALLOW_UID_OVERRIDE
	p9_setvalue(cursor, uid, uint32_t);
#else
	p9_setvalue(cursor, geteuid(), uint32_t);
#endif
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RATTACH:
			p9_getqid(cursor, fid->qid);
			strncpy(fid->path, "/", 2);
			fid->pathlen = 1;
			*pfid = fid;
			break;

		case P9_RERROR:
			p9c_putfid(p9_handle, fid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TATTACH, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}



/**
 * @brief Creates a new fid from path relative to a fid, or clone the said fid if path is NULL
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
int p9p_walk(struct p9_handle *p9_handle, struct p9_fid *fid, char *path, struct p9_fid **pnewfid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint16_t nwname, nwqid;
	uint8_t msgtype;
	uint8_t *cursor;
	struct p9_fid *newfid;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || pnewfid == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	rc = p9c_getfid(p9_handle, &newfid);
	// FIXME: free buffer
	if (rc)
		return rc;

	p9_initcursor(cursor, data->data, P9_TWALK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, newfid->fid, uint32_t);

	/* clone or lookup ? */
	if (!path || path[0] == '\0') {
		INFO_LOG(p9_handle->debug, "walk clone fid %u (%s), newfid %u", fid->fid, fid->path, newfid->fid);
		p9_setvalue(cursor, 0, uint16_t);
		nwname = 0;
	} else {
		INFO_LOG(p9_handle->debug, "walk from fid %u (%s) to %s, newfid %u", fid->fid, fid->path, path, newfid->fid);
		/** @todo: this assumes wname = 1 and no / in path, fix me */
		if (strchr(path, '/') != NULL)
			return EINVAL;
 
		nwname = 1;
		p9_setvalue(cursor, nwname, uint16_t);
		p9_setstr(cursor, strnlen(path, MAXNAMLEN), path);
	}

	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RWALK:
			p9_getvalue(cursor, nwqid, uint16_t);
			if (nwqid != nwname) {
				return EIO;
			}
			if (nwqid != 0) {
				while (nwqid > 1) {
					p9_skipqid(cursor);
					nwqid--;
				}
				p9_getqid(cursor, newfid->qid);
			} else {
				memcpy(&newfid->qid, &fid->qid, sizeof(struct p9_qid));
			}
			strncpy(newfid->path, fid->path, fid->pathlen);
			if (path && fid->pathlen < MAXPATHLEN-2) {
				if (fid->path[fid->pathlen - 1] != '/') {
					strncpy(newfid->path + fid->pathlen, "/", MAXPATHLEN - fid->pathlen);
					strncpy(newfid->path + fid->pathlen + 1, path, MAXPATHLEN - fid->pathlen - 1);
				} else {
					strncpy(newfid->path + fid->pathlen, path, MAXPATHLEN - fid->pathlen);
				}
			}
			newfid->path[MAXPATHLEN-1] = '\0';
			newfid->pathlen = path_canonicalizer(newfid->path);

			*pnewfid = newfid;
			break;

		case P9_RERROR:
			p9c_putfid(p9_handle, newfid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TWALK, tag);
			p9c_putfid(p9_handle, newfid);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


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
int p9p_clunk(struct p9_handle *p9_handle, struct p9_fid *fid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TCLUNK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "clunk on fid %u (%s)", fid->fid, fid->path);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RCLUNK:
			/* nothing else */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TCLUNK, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);
	/* fid is invalid anyway */
	p9c_putfid(p9_handle, fid);

	return rc;
}

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
int p9p_lopen(struct p9_handle *p9_handle, struct p9_fid *fid, uint32_t flags, uint32_t *iounit) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TLOPEN, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, flags, uint32_t);
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RLOPEN:
			p9_getqid(cursor, fid->qid);
			if (iounit)
				p9_getvalue(cursor, *iounit, uint32_t);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TLOPEN, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

/**
 * @brief Create a new file and open it.
 * This will fail if the file already exists.
 *
 *
 * size[4] Tlcreate tag[2] fid[4] name[s] flags[4] mode[4] gid[4]
 * size[4] Rlcreate tag[2] qid[13] iounit[4]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [INOUT] fid:		fid of the directory where to create the new file.
 *				Will be the created file's on success
 * @param [IN]    name:		name of the new file
 * @param [IN]    flags:	Linux kernel intent bits
 * @param [IN]    mode:		Linux creat(2) mode bits
 * @param [IN]    gid:		effective gid
 * @param [OUT]   iounit:	iounit to set if non-NULL
 * @return 0 on success, errno value on error.
 */
int p9p_lcreate(struct p9_handle *p9_handle, struct p9_fid *fid, char *name, uint32_t flags, uint32_t mode,
               uint32_t gid, uint32_t *iounit) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || name == NULL || fid == NULL || strchr(name, '/') != NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TLCREATE, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setvalue(cursor, flags, uint32_t);
	p9_setvalue(cursor, mode, uint32_t);
	p9_setvalue(cursor, gid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "lcreate from fid %u (%s) to name %s, flag %u, mode %u", fid->fid, fid->path, name, flags, mode);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RLCREATE:
			p9_skipqid(cursor);
			if (iounit)
				p9_getvalue(cursor, *iounit, uint32_t);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TLCREATE, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

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
int p9p_symlink(struct p9_handle *p9_handle, struct p9_fid *dfid, char *name, char *symtgt, uint32_t gid,
               struct p9_qid *qid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || name == NULL || dfid == NULL || strchr(name, '/') != NULL || symtgt == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TSYMLINK, tag);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setstr(cursor, strlen(symtgt), symtgt);
	p9_setvalue(cursor, gid, uint32_t);
	p9_setmsglen(cursor, data);
	/** @todo FIXME make sure this fits before writing it... */

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RSYMLINK:
			p9_skipqid(cursor);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TSYMLINK, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

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
int p9p_mknod(struct p9_handle *p9_handle, struct p9_fid *dfid, char *name, uint32_t mode, uint32_t major, uint32_t minor,
             uint32_t gid, struct p9_qid *qid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || name == NULL || dfid == NULL || strchr(name, '/') != NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TMKNOD, tag);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setvalue(cursor, mode, uint32_t);
	p9_setvalue(cursor, major, uint32_t);
	p9_setvalue(cursor, minor, uint32_t);
	p9_setvalue(cursor, gid, uint32_t);
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RMKNOD:
			if (qid)
				p9_getqid(cursor, *qid);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TMKNOD, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

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
int p9p_rename(struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_fid *dfid, char *name) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || dfid == NULL || name == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TRENAME, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RRENAME:
			/* nothing */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TRENAME, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

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
int p9pz_readlink(struct p9_handle *p9_handle, struct p9_fid *fid, char **ztarget, uint32_t *zsize, msk_data_t **pdata) {
 	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || ztarget == NULL || zsize == NULL || pdata == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TREADLINK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RREADLINK:
			*pdata = data;
			p9_getstr(cursor, *zsize, *ztarget);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			p9c_putreply(p9_handle, data);
			break;

		default:
			p9c_putreply(p9_handle, data);
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TREADLINK, tag);
			rc = EIO;
	}

	return rc;
}

int p9p_readlink(struct p9_handle *p9_handle, struct p9_fid *fid, char *target, uint32_t size) {
	char *ztarget;
	msk_data_t *data;
	uint32_t zsize = 0;
	int rc;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || target == NULL)
		return EINVAL;

	rc = p9pz_readlink(p9_handle, fid, &ztarget, &zsize, &data);
	if (rc == 0) {
		strncpy(target, ztarget, MIN(size, zsize));
		p9c_putreply(p9_handle, data);
	}

	return rc;
}


/** p9_mkdir
 *
 *
 * size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4]
 * size[4] Rmkdir tag[2] qid[13]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    dfid:		directory fid where to create dir
 * @param [IN]    name:		name of the new directory
 * @param [IN]    mode:		mode
 * @param [IN]    gid:		gid
 * @param [OUT]   qid:		new directory qid if not NULL
 * @return 0 on success, errno value on error.
 */
int p9p_mkdir(struct p9_handle *p9_handle, struct p9_fid *dfid, char *name, uint32_t mode,
               uint32_t gid, struct p9_qid *qid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || name == NULL || dfid == NULL || strchr(name, '/') != NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TMKDIR, tag);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setvalue(cursor, mode, uint32_t);
	p9_setvalue(cursor, gid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "mkdir dfid %u (%s), name %s mode %u", dfid->fid, dfid->path, name, mode);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RMKDIR:
			if (qid)
				p9_getqid(cursor, *qid);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TMKDIR, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}



/** p9_readdir
 *
 *
 * size[4] Treaddir tag[2] fid[4] offset[8] count[4]
 * size[4] Rreaddir tag[2] count[4] data[count]
 *   data is: qid[13] offset[8] type[1] name[s]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		directory fid
 * @param [INOUT] offset:	offset to start from, will be set to where we left off on return
 * @param [IN]    callback:	callback to call for each entry.
 *                              processing stops if callback returns non-zero
 * @param [IN]    callback_arg:	user-provided callback arg
 * @return 0 on eod, number of entires read if positive, -errno value on error (or callback return value)
 */
int p9p_readdir(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t *poffset,
                p9p_readdir_cb callback, void *callback_arg) {
	int rc;
	msk_data_t *data;
	uint64_t offset;
	uint32_t count, i;
	struct p9_qid qid;
	uint8_t type;
	char *name;
	uint16_t namelen;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor, *start;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TREADDIR, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, *poffset, uint64_t);
	p9_setvalue(cursor, p9_handle->msize - P9_ROOM_RREADDIR, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "readdir fid %u (%s), offset %#"PRIx64, fid->fid, fid->path, *poffset);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RREADDIR:
			p9_getvalue(cursor, count, uint32_t);
			start = cursor;
			for (i=0; cursor < start + count; i++) {
				p9_getqid(cursor, qid);
				p9_getvalue(cursor, offset, uint64_t);
				p9_getvalue(cursor, type, uint8_t);
				p9_getstr(cursor, namelen, name);
				rc = callback(callback_arg, fid, &qid, type, namelen, name);
				if (rc)
					break;
			}

			/* set count to number of processed items */

			rc = i;
			if (rc > 0) {
				*poffset = offset;
			}
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			rc = -rc;
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TREADDIR, tag);
			rc = -EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}



/**
 * @brief zero-copy read from a file.
 * Even if count is > msize, more won't be received
 * There MUST be a finalize call to p9c_putreply(p9_handle, data) on success
 *
 * size[4] Tread tag[2] fid[4] offset[8] count[4]
 * size[4] Rread tag[2] count[4] data[count]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to use
 * @param [IN]    offset:	offset from which to read
 * @param [IN]    count:	count of bytes to read
 * @param [OUT]   zbuf:		data pointer here
 * @return number of bytes read if >= 0, -errno on error.
 *          0 indicates eof?
 */
int p9pz_read(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t offset, uint32_t count, char **zbuf, uint32_t *zsize, msk_data_t **pdata) {
 	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || zbuf == NULL || zsize == NULL || pdata == NULL || count == 0)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	if (count > p9_handle->msize - P9_ROOM_RREAD)
		count = p9_handle->msize - P9_ROOM_RREAD;

	p9_initcursor(cursor, data->data, P9_TREAD, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, offset, uint64_t);
	p9_setvalue(cursor, count, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "read fid %u (%s), offset %"PRIu64", count %u", fid->fid, fid->path, offset, count);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RREAD:
			*pdata = data;
			p9_getvalue(cursor, *zsize, uint32_t);
			p9_getptr(cursor, *zbuf, char);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			p9c_putreply(p9_handle, data);
			rc = -rc;
			break;

		default:
			p9c_putreply(p9_handle, data);
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TREAD, tag);
			rc = -EIO;
	}

	return rc;
}

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
 * @param [OUT]   buf:		data is copied there.
 * @return number of bytes read if >= 0, -errno on error.
 *          0 indicates eof
 */
int p9p_read(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t offset, uint32_t count, char *buf) {
	char *zbuf;
	msk_data_t *data;
	uint32_t zsize;
	int rc;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || buf == NULL)
		return EINVAL;

	rc = p9pz_read(p9_handle, fid, offset, count, &zbuf, &zsize, &data);
	if (rc == 0)
		strncpy(buf, zbuf, MIN(count, zsize));

	p9c_putreply(p9_handle, data);

	if (rc)
		return -rc;
	else
		return MIN(zsize, INT_MAX);
}

/**
 * @brief zero-copy write from a file.
 * Even if count is > msize, more won't be received
 * data MUST be registered with p9c_reg_mr(p9_handle, data) first
 *
 * size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
 * size[4] Rwrite tag[2] count[4]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to use
 * @param [IN]    offset:	offset from which to write
 * @param [IN]    data:		msk_registered msk_data pointer here
 * @return number of bytes written if >= 0, -errno on error.
 */
int p9pz_write(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t offset, msk_data_t *data) {
 	int rc;
	msk_data_t *header_data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || data == NULL || data->size == 0)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &header_data, &tag);
	if (rc != 0 || header_data == NULL)
		return rc;

	if (data->size > p9_handle->msize - P9_ROOM_TWRITE)
		data->size = p9_handle->msize - P9_ROOM_TWRITE;

	p9_initcursor(cursor, header_data->data, P9_TWRITE, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, offset, uint64_t);
	p9_setvalue(cursor, data->size, uint32_t);
	p9_setmsglen(cursor, header_data);
	*((uint32_t*)header_data->data) = header_data->size + data->size;

	header_data->next = data;

	INFO_LOG(p9_handle->debug, "write fid %u (%s), offset %"PRIu64", count %u", fid->fid, fid->path, offset, data->size);

	rc = p9c_sendrequest(p9_handle, header_data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &header_data, tag);
	if (rc != 0 || header_data == NULL)
		return rc;

	cursor = header_data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RWRITE:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			rc = -rc;
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TWRITE, tag);
			rc = -EIO;
	}

	p9c_putreply(p9_handle, header_data);

	return rc;
}

/**
 * @brief Write from a file.
 * Even if count is > msize, more won't be received
 *
 *
 * size[4] Twrite tag[2] fid[4] offset[8] count[4]
 * size[4] Rwrite tag[2] count[4] data[count]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to use
 * @param [IN]    offset:	offset from which to write
 * @param [IN]    count:	count of bytes to write
 * @param [OUT]   buf:		data is copied there.
 * @return number of bytes write if >= 0, -errno on error.
 *          0 indicates eof?
 */
int p9p_write(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t offset, uint32_t count, char *buf) {
 	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || buf == NULL || count == 0)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	if (count > p9_handle->msize - P9_ROOM_TWRITE)
		count = p9_handle->msize - P9_ROOM_TWRITE;

	p9_initcursor(cursor, data->data, P9_TWRITE, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, offset, uint64_t);
	p9_setvalue(cursor, count, uint32_t);
	memcpy(cursor, buf, count);
	cursor += count;
	p9_setmsglen(cursor, data);


	INFO_LOG(p9_handle->debug, "write fid %u (%s), offset %"PRIu64", count %u", fid->fid, fid->path, offset, data->size);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RWRITE:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			rc = -rc;
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TWRITE, tag);
			rc = -EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

/** p9_xattrwalk
 *
 * Allocate a new fid to read the content of xattr name from fid
 * if name is NULL or empty, content will be the list of xattrs
 *
 * size[4] Txattrwalk tag[2] fid[4] newfid[4] name[s]
 * size[4] Rxattrwalk tag[2] size[8]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to clone
 * @param [OUT]   newfid:	newfid where xattr will be readable
 * @param [IN]    name:		name of xattr to read, or NULL for the list
 * @param [OUT]	  psize:	size available for reading
 * @return 0 on success, errno value on error.
 */
int p9p_xattrwalk(struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_fid **pnewfid, char *name, uint64_t *psize) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;
	struct p9_fid *newfid;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || pnewfid == NULL || psize == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	rc = p9c_getfid(p9_handle, &newfid);
	// FIXME: free buffer
	if (rc)
		return rc;

	p9_initcursor(cursor, data->data, P9_TXATTRWALK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, newfid->fid, uint32_t);
	if (name)
		p9_setstr(cursor, strnlen(name, MAXPATHLEN), name);
	else
		p9_setstr(cursor, 0, "");

	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RXATTRWALK:
			p9_getvalue(cursor, *psize, uint64_t);
			*pnewfid = newfid;
			break;

		case P9_RERROR:
			p9c_putfid(p9_handle, newfid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TXATTRWALK, tag);
			p9c_putfid(p9_handle, newfid);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

/** p9_xattrcreate
 *
 * Replace fid with one where xattr content will be writable
 *
 * size[4] Txattrcreate tag[2] fid[4] name[s] attr_size[8] flags[4]
 * size[4] Rxattrcreate tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    fid:		fid to use
 * @param [IN]    name:		name of xattr to create
 * @param [IN]    size:		size of the xattr that will be written
 * @param [IN]    flags:	flags (derifed from linux setxattr flags: XATTR_CREATE, XATTR_REPLACE)
 * @return 0 on success, errno value on error.
 */
int p9p_xattrcreate(struct p9_handle *p9_handle, struct p9_fid *fid, char *name, uint64_t size, uint32_t flags) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || name == NULL || fid == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TXATTRCREATE, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setvalue(cursor, size, uint64_t);
	p9_setvalue(cursor, flags, uint32_t);
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RXATTRCREATE:
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TXATTRCREATE, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}



/** p9_renameat
 *
 * renameat is preferred over rename
 *
 * size[4] Trenameat tag[2] olddirfid[4] oldname[s] newdirfid[4] newname[s]
 * size[4] Rrenameat tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    dfid:		fid of the directory where file currently is
 * @param [IN]    name:		current filename
 * @param [IN]    newdfid:	fid of the directory to move into
 * @param [IN]    newname:	new filename
 * @return 0 on success, errno value on error.
 */
int p9p_renameat(struct p9_handle *p9_handle, struct p9_fid *dfid, char *name, struct p9_fid *newdfid, char *newname) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || dfid == NULL || name == NULL || newdfid == NULL || newname == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TRENAMEAT, tag);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setvalue(cursor, newdfid->fid, uint32_t);
	p9_setstr(cursor, strlen(newname), newname);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "renameat on dfid %u (%s) name %s to dfid %u (%s) name %s", dfid->fid, dfid->path, name, newdfid->fid, newdfid->path, newname);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RRENAMEAT:
			/* nothing else */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TRENAMEAT, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

/** p9_unlinkat
 *
 * unlink file by name
 *
 * size[4] Tunlinkat tag[2] dirfid[4] name[s] flags[4]
 * size[4] Runlinkat tag[2]
 *
 * @param [IN]    p9_handle:	connection handle
 * @param [IN]    dfid:		fid of the directory where file currently is
 * @param [IN]    name:		name of file to unlink
 * @param [IN]    flags:	unlink flags, unused by server?
 * @return 0 on success, errno value on error.
 */
int p9p_unlinkat(struct p9_handle *p9_handle, struct p9_fid *dfid, char *name, uint32_t flags) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || dfid == NULL || name == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TUNLINKAT, tag);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setvalue(cursor, flags, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug, "unlinkat on dfid %u (%s) name %s", dfid->fid, dfid->path, name);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RUNLINKAT:
			/* nothing else */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TUNLINKAT, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}
