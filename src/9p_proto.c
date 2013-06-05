#include <stdio.h>
#include <string.h>
#include <errno.h>
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
	p9_setmsglen(cursor, data->data);

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
			rc = EIO;;
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
	p9_setvalue(cursor, uid, uint32_t);
	p9_setmsglen(cursor, data->data);

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
			p9_getvalue(cursor, rc, uint32_t);
			p9c_putfid(p9_handle, fid);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TATTACH, tag);
			rc = EIO;;
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
	if (p9_handle == NULL || fid == NULL)
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
	if (!path) {
		p9_setvalue(cursor, 0, uint16_t);
		nwname = 0;
	} else {
		/** @todo: this assumes wname = 1 and no / in path, fix me */
		if (strchr(path, '/') != NULL)
			return EINVAL;
 
		nwname = 1;
		p9_setvalue(cursor, nwname, uint16_t);
		p9_setstr(cursor, strnlen(path, MAXNAMLEN), path);
	}

	p9_setmsglen(cursor, data->data);

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
				p9_getqid(cursor, fid->qid);
			}
			strncpy(newfid->path, fid->path, fid->pathlen);
			strncpy(newfid->path + fid->pathlen, "/", 1);
			strncpy(newfid->path + fid->pathlen + 1, path, MAXPATHLEN - fid->pathlen - 1);
			newfid->path[MAXPATHLEN-1] = '\0';
			newfid->pathlen = strlen(newfid->path);


			*pnewfid = newfid;
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TWALK, tag);
			rc = EIO;;
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
	p9_setmsglen(cursor, data->data);

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
			rc = EIO;;
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
	p9_setmsglen(cursor, data->data);

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
			rc = EIO;;
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
	p9_setmsglen(cursor, data->data);

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
			rc = EIO;;
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
	p9_setmsglen(cursor, data->data);
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
			rc = EIO;;
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
	p9_setmsglen(cursor, data->data);

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
			rc = EIO;;
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
	p9_setmsglen(cursor, data->data);

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
			rc = EIO;;
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
int p9p_zreadlink(struct p9_handle *p9_handle, struct p9_fid *fid, char **ztarget, uint32_t *zsize, msk_data_t **pdata) {
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
	p9_setmsglen(cursor, data->data);

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
			p9_getstr(cursor, *zsize, *ztarget);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TREADLINK, tag);
			rc = EIO;;
	}

	return rc;
}

int p9p_readlink(struct p9_handle *p9_handle, struct p9_fid *fid, char *target, uint32_t size) {
	char *ztarget;
	msk_data_t *data;
	uint32_t zsize;
	int rc;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || target == NULL)
		return EINVAL;

	rc = p9p_zreadlink(p9_handle, fid, &ztarget, &zsize, &data);
	if (zsize > 0)
		strncpy(target, ztarget, MIN(size, zsize));

	p9c_putreply(p9_handle, data);

	return rc;
}
