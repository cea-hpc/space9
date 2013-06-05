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
int p9_version(struct p9_handle *p9_handle) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;
	char *version;
	uint16_t len;

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
	p9_skipheader(cursor);
	p9_getvalue(cursor, p9_handle->msize, uint32_t);
	p9_getstr(cursor, len, version);
	if (strncmp(version, "9P2000.L", len)) {
		rc = EINVAL;
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
int p9_attach(struct p9_handle *p9_handle, uint32_t uid, struct p9_fid **pfid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;
	struct p9_fid *fid;

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
	p9_skipheader(cursor);
	p9_getqid(cursor, fid->qid);
	strncpy(fid->path, "/", 2);
	fid->pathlen = 1;

	p9c_putreply(p9_handle, data);

	*pfid = fid;

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
int p9_walk(struct p9_handle *p9_handle, struct p9_fid *fid, char *path, struct p9_fid **pnewfid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint16_t nwname, nwqid;
	uint8_t *cursor;
	struct p9_fid *newfid;

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
	p9_skipheader(cursor);
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

	p9c_putreply(p9_handle, data);

	*pnewfid = newfid;

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
int p9_clunk(struct p9_handle *p9_handle, struct p9_fid *fid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;

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
	p9_skipheader(cursor);
	/* nothing else */

	p9c_putreply(p9_handle, data);
	p9c_putfid(p9_handle, fid);

	return rc;
}
