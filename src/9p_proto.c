/*
 * Copyright CEA/DAM/DIF (2013)
 * Contributor: Dominique Martinet <dominique.martinet@cea.fr>
 *
 * This file is part of the space9 9P userspace library.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with space9.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <inttypes.h> // PRIu64
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "9p_internals.h"
#include "9p_proto_internals.h"
#include "utils.h"

static const char version_str[] = "9P2000.L";
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
	p9_setstr(cursor, strlen(version_str), version_str);
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
			if (strncmp(version, version_str, len)) {
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


int p9p_auth(struct p9_handle *p9_handle, uint32_t uid, struct p9_fid **pafid) {
	int rc;
	uint8_t msgtype;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;
	struct p9_fid *fid;

	/* Sanity check */
	if (p9_handle == NULL || pafid == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	rc = p9c_getfid(p9_handle, &fid);
	if (rc) {
		p9c_abortrequest(p9_handle, data, tag);
		ERROR_LOG("not enough fids - failing auth");
		return rc;
	}

	p9_initcursor(cursor, data->data, P9_TAUTH, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
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
		case P9_RAUTH:
			p9_getqid(cursor, fid->qid);
			strncpy(fid->path, "<afid>", 7);
			fid->pathlen = 6;
			*pafid = fid;
			break;

		case P9_RERROR:
			p9c_putfid(p9_handle, &fid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TAUTH, tag);
			p9c_putfid(p9_handle, &fid);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


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

	/* handle reconnection: if fid already exists, take the same fid */
	if (*pfid)
		fid = *pfid;
	else
		rc = p9c_getfid(p9_handle, &fid);

	if (rc) {
		p9c_abortrequest(p9_handle, data, tag);
		ERROR_LOG("not enough fids - failing attach");
		return rc;
	}

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
			p9c_putfid(p9_handle, &fid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TATTACH, tag);
			p9c_putfid(p9_handle, &fid);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_flush(struct p9_handle *p9_handle, uint16_t oldtag) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TFLUSH, tag);
	p9_setvalue(cursor, oldtag, uint16_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "flush on tag %u", oldtag);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RFLUSH:
			/* nothing else */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TFLUSH, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

int p9p_rewalk(struct p9_handle *p9_handle, struct p9_fid *fid, char *path, uint32_t newfid_i) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint16_t nwname;
	uint8_t msgtype;
	uint8_t *cursor, *pnwname;
	char *subpath, *curpath;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || path == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TWALK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, newfid_i, uint32_t);

	/* clone or lookup ? */
	if (!path || path[0] == '\0') {
		INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "walk clone fid %u (%s), newfid %u", fid->fid, fid->path, newfid_i);
		p9_setvalue(cursor, 0, uint16_t);
		nwname = 0;
	} else {
		INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "walk from fid %u (%s) to %s, newfid %u", fid->fid, fid->path, path, newfid_i);

		nwname = 0;
		p9_savepos(cursor, pnwname, uint16_t);
		curpath = path;
		while ((subpath = strchr(curpath, '/')) != NULL) {
			subpath[0] = '\0';
			if (curpath != subpath) {
				p9_setstr(cursor, subpath-curpath, curpath);
				nwname += 1;
			}
			subpath[0] = '/';
			curpath = subpath+1;
		}

		if (strnlen(curpath,MAXNAMLEN) > 0) {
			p9_setstr(cursor, strnlen(curpath, MAXNAMLEN), curpath);
			nwname += 1;
		}
		p9_setvalue(pnwname, nwname, uint16_t);
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
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TWALK, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}

int p9p_walk(struct p9_handle *p9_handle, struct p9_fid *fid, char *path, struct p9_fid **pnewfid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint16_t nwname, nwqid;
	uint8_t msgtype;
	uint8_t *cursor, *pnwname;
	char *subpath, *curpath;
	struct p9_fid *newfid;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || pnewfid == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	rc = p9c_getfid(p9_handle, &newfid);
	if (rc) {
		p9c_abortrequest(p9_handle, data, tag);
		ERROR_LOG("not enough fids - failing walk");
		return rc;
	}

	p9_initcursor(cursor, data->data, P9_TWALK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, newfid->fid, uint32_t);

	/* clone or lookup ? */
	if (!path || path[0] == '\0') {
		INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "walk clone fid %u (%s), newfid %u", fid->fid, fid->path, newfid->fid);
		p9_setvalue(cursor, 0, uint16_t);
		nwname = 0;
	} else {
		INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "walk from fid %u (%s) to %s, newfid %u", fid->fid, fid->path, path, newfid->fid);

		nwname = 0;
		p9_savepos(cursor, pnwname, uint16_t);
		curpath = path;
		while ((subpath = strchr(curpath, '/')) != NULL) {
			subpath[0] = '\0';
			if (curpath != subpath) {
				p9_setstr(cursor, subpath-curpath, curpath);
				nwname += 1;
			}
			subpath[0] = '/';
			curpath = subpath+1;
		}

		if (strnlen(curpath,MAXNAMLEN) > 0) {
			p9_setstr(cursor, strnlen(curpath, MAXNAMLEN), curpath);
			nwname += 1;
		}
		p9_setvalue(pnwname, nwname, uint16_t);
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
			if (path && path[0] != '\0' && fid->pathlen < MAXPATHLEN-2) {
				if (fid->path[fid->pathlen - 1] != '/') {
					strncpy(newfid->path + fid->pathlen, "/", MAXPATHLEN - fid->pathlen);
					strncpy(newfid->path + fid->pathlen + 1, path, MAXPATHLEN - fid->pathlen - 1);
				} else {
					strncpy(newfid->path + fid->pathlen, path, MAXPATHLEN - fid->pathlen);
				}
				newfid->path[MAXPATHLEN-1] = '\0';
				newfid->pathlen = path_canonicalizer(newfid->path);
			} else {
				newfid->path[fid->pathlen] = '\0';
				newfid->pathlen = fid->pathlen;
			}

			*pnewfid = newfid;
			break;

		case P9_RERROR:
			p9c_putfid(p9_handle, &newfid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TWALK, tag);
			p9c_putfid(p9_handle, &newfid);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_clunk(struct p9_handle *p9_handle, struct p9_fid **pfid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || pfid == NULL || *pfid == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TCLUNK, tag);
	p9_setvalue(cursor, (*pfid)->fid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "clunk on fid %u (%s)", (*pfid)->fid, (*pfid)->path);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);

	if (rc == 0 && data != NULL) {
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
	}

	/* fid is invalid anyway */
	p9c_putfid(p9_handle, pfid);

	return rc;
}


int p9p_remove(struct p9_handle *p9_handle, struct p9_fid **pfid) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || pfid == NULL || *pfid == NULL)
		return EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TREMOVE, tag);
	p9_setvalue(cursor, (*pfid)->fid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "remove on fid %u (%s)", (*pfid)->fid, (*pfid)->path);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);

	if (rc == 0 && data != NULL) {
		cursor = data->data;
		p9_getheader(cursor, msgtype);
		switch(msgtype) {
			case P9_RREMOVE:
				/* nothing else */
				break;

			case P9_RERROR:
				p9_getvalue(cursor, rc, uint32_t);
				break;

			default:
				ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TREMOVE, tag);
				rc = EIO;
		}

		p9c_putreply(p9_handle, data);
	}

	/* fid is invalid anyway */
	p9c_putfid(p9_handle, pfid);

	return rc;
}


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

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "lopen on fid %u (%s), flags 0x%x", fid->fid, fid->path, flags);

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
			if (flags & O_WRONLY)
				fid->openflags = WRFLAG;
			else if (flags & O_RDWR)
				fid->openflags = RDFLAG | WRFLAG;
			else
				fid->openflags = RDFLAG;
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
	if (gid == 0)
		gid = getegid();
	p9_setvalue(cursor, gid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "lcreate from fid %u (%s) to name %s, flag 0x%x, mode %u", fid->fid, fid->path, name, flags, mode);

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
			strncat(fid->path, name, MAXPATHLEN - strlen(fid->path));
			if (flags & O_WRONLY)
				fid->openflags = WRFLAG;
			else if (flags & O_RDWR)
				fid->openflags = RDFLAG | WRFLAG;
			else
				fid->openflags = RDFLAG;
			p9_getqid(cursor, fid->qid);
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

	if (P9_ROOM_TSYMLINK + strlen(name) + strlen(symtgt) > p9_handle->msize)
		return ERANGE;

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
			if (qid)
				p9_getqid(cursor, *qid);
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


int p9pz_readlink(struct p9_handle *p9_handle, struct p9_fid *fid, char **ztarget, msk_data_t **pdata) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || ztarget == NULL || pdata == NULL || (fid->openflags & RDFLAG) == 0)
		return -EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return -rc;

	p9_initcursor(cursor, data->data, P9_TREADLINK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setmsglen(cursor, data);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return -rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return -rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RREADLINK:
			*pdata = data;
			p9_getstr(cursor, rc, *ztarget);
			cursor[0] = '\0';
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			p9c_putreply(p9_handle, data);
			rc = -rc;
			break;

		default:
			p9c_putreply(p9_handle, data);
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TREADLINK, tag);
			rc = -EIO;
	}

	return rc;
}


int p9p_readlink(struct p9_handle *p9_handle, struct p9_fid *fid, char *target, uint32_t size) {
	char *ztarget;
	msk_data_t *data;
	int rc;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || target == NULL)
		return -EINVAL;

	rc = p9pz_readlink(p9_handle, fid, &ztarget, &data);
	if (rc >= 0) {
		memcpy(target, ztarget, MIN(size-1, rc));
		target[MIN(size-1,rc)] = '\0';
		p9c_putreply(p9_handle, data);
	}

	return rc;
}


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

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "mkdir dfid %u (%s), name %s mode %u", dfid->fid, dfid->path, name, mode);

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


int p9p_readdir(struct p9_handle *p9_handle, struct p9_fid *fid, uint64_t *poffset,
                p9p_readdir_cb callback, void *callback_arg) {
	int rc;
	msk_data_t *data;
	uint64_t offset;
	uint32_t count, i;
	struct p9_qid qid;
	uint16_t namelen;
	uint16_t tag;
	uint8_t type;
	uint8_t msgtype;
	uint8_t readahead;
	char *name;
	uint8_t *cursor, *start;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || (fid->openflags & RDFLAG) == 0)
		return -EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return -rc;

	p9_initcursor(cursor, data->data, P9_TREADDIR, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, *poffset, uint64_t);
	/* ROOM_RREADDIR is the size of the readdir reply header, and keep one more for the final 0 we write */
	p9_setvalue(cursor, p9_handle->msize - P9_ROOM_RREADDIR - 1, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "readdir fid %u (%s), offset %#"PRIx64, fid->fid, fid->path, *poffset);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return -rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return -rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RREADDIR:
			p9_getvalue(cursor, count, uint32_t);
			start = cursor;
			readahead = '\0';
			for (i=0; cursor < start + count; i++) {
				if (readahead != '\0')
					*cursor = readahead;
				p9_getqid(cursor, qid);
				p9_getvalue(cursor, offset, uint64_t);
				p9_getvalue(cursor, type, uint8_t);
				p9_getstr(cursor, namelen, name);
				/* null terminate name to make processing easier and remember what was there */
				readahead = *cursor;
				*cursor = '\0';
				rc = callback(callback_arg, p9_handle, fid, &qid, type, namelen, name);
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


ssize_t p9pz_read_send(struct p9_handle *p9_handle, struct p9_fid *fid, size_t count, uint64_t offset, uint16_t *ptag) {
	ssize_t rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || count == 0 || (fid->openflags & RDFLAG) == 0)
		return -EINVAL;


	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return -rc;

	count = p9p_read_len(p9_handle, count);

	p9_initcursor(cursor, data->data, P9_TREAD, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, offset, uint64_t);
	p9_setvalue(cursor, count, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "read fid %u (%s), offset %"PRIu64", count %zi", fid->fid, fid->path, offset, count);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return -rc;

	*ptag = tag;
	return 0;
}

ssize_t p9pz_read_wait(struct p9_handle *p9_handle, msk_data_t **pdata, uint16_t tag) {
	ssize_t rc;
	msk_data_t *data;
	uint8_t msgtype;
	uint8_t *cursor;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return -rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RREAD:
			p9_getvalue(cursor, rc, uint32_t);
			data->data += P9_ROOM_RREAD;
			data->size -= P9_ROOM_RREAD;
			*pdata = data;
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

ssize_t p9pz_read(struct p9_handle *p9_handle, struct p9_fid *fid, size_t count, uint64_t offset, msk_data_t **pdata) {
	ssize_t rc;
	uint16_t tag;

	if (pdata == NULL)
		return -EINVAL;

	rc = p9pz_read_send(p9_handle, fid, count, offset, &tag);
	if (rc)
		return rc;

	return p9pz_read_wait(p9_handle, pdata, tag);
}

ssize_t p9p_read(struct p9_handle *p9_handle, struct p9_fid *fid, char *buf, size_t count, uint64_t offset) {
	msk_data_t *data;
	ssize_t rc;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || buf == NULL)
		return -EINVAL;

	rc = p9pz_read(p9_handle, fid, count, offset, &data);

	if (rc >= 0) {
		memcpy(buf, data->data, MIN(count, rc+1));
		p9c_putreply(p9_handle, data);
	}

	return rc;
}


ssize_t p9pz_write_send(struct p9_handle *p9_handle, struct p9_fid *fid, msk_data_t *data, uint64_t offset, uint16_t *ptag) {
	ssize_t rc;
	msk_data_t *header_data;
	uint16_t tag;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || data == NULL || data->size == 0 || (fid->openflags & WRFLAG) == 0)
		return -EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &header_data, &tag);
	if (rc != 0 || header_data == NULL)
		return -rc;

	data->size = p9p_write_len(p9_handle, data->size);

	p9_initcursor(cursor, header_data->data, P9_TWRITE, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, offset, uint64_t);
	p9_setvalue(cursor, data->size, uint32_t);
	p9_setmsglen(cursor, header_data);
	*((uint32_t*)header_data->data) = header_data->size + data->size;

	header_data->next = data;

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "write tag %u,  fid %u (%s), offset %"PRIu64", count %u", tag, fid->fid, fid->path, offset, data->size);

	rc = p9c_sendrequest(p9_handle, header_data, tag);
	if (rc != 0)
		return -rc;

	*ptag = tag;
	return 0;
}

ssize_t p9pz_write_wait(struct p9_handle *p9_handle, uint16_t tag) {
	ssize_t rc;
	msk_data_t *data;
	uint8_t *cursor;
	uint8_t msgtype;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return -rc;

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "writewait tag %u", tag);

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

ssize_t p9pz_write(struct p9_handle *p9_handle, struct p9_fid *fid, msk_data_t *data, uint64_t offset) {
	ssize_t rc;
	uint16_t tag;

	rc = p9pz_write_send(p9_handle, fid, data, offset, &tag);
	if (rc)
		return rc;

	return p9pz_write_wait(p9_handle, tag);
}

ssize_t p9p_write_send(struct p9_handle *p9_handle, struct p9_fid *fid, char *buf, size_t count, uint64_t offset, uint16_t *ptag) {
	ssize_t rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || buf == NULL || count == 0 || (fid->openflags & WRFLAG) == 0)
		return -EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return -rc;

	count = p9p_write_len(p9_handle, count);

	p9_initcursor(cursor, data->data, P9_TWRITE, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, offset, uint64_t);
	p9_setvalue(cursor, count, uint32_t);
	memcpy(cursor, buf, count);
	cursor += count;
	p9_setmsglen(cursor, data);


	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "write fid %u (%s), offset %"PRIu64", count %zu", fid->fid, fid->path, offset, count);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return -rc;

	*ptag = tag;
	return 0;
}

ssize_t p9p_write_wait(struct p9_handle *p9_handle, uint16_t tag) {
	ssize_t rc;
	msk_data_t *data;
	uint8_t msgtype;
	uint8_t *cursor;
	
	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return -rc;

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

ssize_t p9p_write(struct p9_handle *p9_handle, struct p9_fid *fid, char *buf, size_t count, uint64_t offset) {
	ssize_t rc;
	uint16_t tag;

	rc = p9p_write_send(p9_handle, fid, buf, count, offset, &tag);
	if (rc)
		return rc;

	return p9p_write_wait(p9_handle, tag);
}

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
	if (rc) {
		p9c_abortrequest(p9_handle, data, tag);
		ERROR_LOG("not enough fids - failing xattrwalk");
		return rc;
	}

	p9_initcursor(cursor, data->data, P9_TXATTRWALK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, newfid->fid, uint32_t);
	if (name)
		p9_setstr(cursor, strnlen(name, XATTR_NAME_MAX), name);
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
			newfid->openflags = RDFLAG;
			*pnewfid = newfid;
			break;

		case P9_RERROR:
			p9c_putfid(p9_handle, &newfid);
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TXATTRWALK, tag);
			p9c_putfid(p9_handle, &newfid);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


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
			fid->openflags = WRFLAG;
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

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "renameat on dfid %u (%s) name %s to dfid %u (%s) name %s", dfid->fid, dfid->path, name, newdfid->fid, newdfid->path, newname);

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

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "unlinkat on dfid %u (%s) name %s", dfid->fid, dfid->path, name);

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


int p9p_getattr(struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_getattr *attr) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || attr == NULL)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	if (attr->valid == 0) {
		attr->valid = P9_GETATTR_BASIC;
	}

	p9_initcursor(cursor, data->data, P9_TGETATTR, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, attr->valid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "getattr on fid %u (%s), attr mask 0x%"PRIx64, fid->fid, fid->path, attr->valid);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RGETATTR:
			p9_getvalue(cursor, attr->valid, uint64_t);
			p9_skipqid(cursor);
			/* get values anyway */
			p9_getvalue(cursor, attr->mode, uint32_t);
			p9_getvalue(cursor, attr->uid, uint32_t);
			p9_getvalue(cursor, attr->gid, uint32_t);
			p9_getvalue(cursor, attr->nlink, uint64_t);
			p9_getvalue(cursor, attr->rdev, uint64_t);
			p9_getvalue(cursor, attr->size, uint64_t);
			p9_getvalue(cursor, attr->blksize, uint64_t);
			p9_getvalue(cursor, attr->blkcount, uint64_t);
			p9_getvalue(cursor, attr->atime_sec, uint64_t);
			p9_skipvalue(cursor, uint64_t); /* atime_nsec */
			p9_getvalue(cursor, attr->mtime_sec, uint64_t);
			p9_skipvalue(cursor, uint64_t); /* mtime_nsec */
			p9_getvalue(cursor, attr->ctime_sec, uint64_t);
#if 0
			p9_skipvalue(cursor, uint64_t); /* ctime_nsec */
			p9_skipvalue(cursor, uint64_t); /* btime_sec */
			p9_skipvalue(cursor, uint64_t); /* btime_nsec */
			p9_skipvalue(cursor, uint64_t); /* gen */
			p9_skipvalue(cursor, uint64_t); /* data_version */
#endif
			attr->ino = fid->qid.path;
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TGETATTR, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_setattr(struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_setattr *attr) {
	int rc;
	msk_data_t *data;
	uint16_t tag;
	uint8_t msgtype;
	uint8_t *cursor;

	/* Sanity check */
	if (p9_handle == NULL || fid == NULL || attr == NULL || attr->valid == 0)
		return EINVAL;

	tag = 0;
	rc = p9c_getbuffer(p9_handle, &data, &tag);
	if (rc != 0 || data == NULL)
		return rc;

	p9_initcursor(cursor, data->data, P9_TSETATTR, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, attr->valid, uint32_t);
	p9_setvalue(cursor, attr->mode, uint32_t);
	p9_setvalue(cursor, attr->uid, uint32_t);
	p9_setvalue(cursor, attr->gid, uint32_t);
	p9_setvalue(cursor, attr->size, uint64_t);
	p9_setvalue(cursor, attr->atime_sec, uint64_t);
	p9_setvalue(cursor, 0LL, uint64_t); /* atime_nsec */
	p9_setvalue(cursor, attr->mtime_sec, uint64_t);
	p9_setvalue(cursor, 0LL, uint64_t); /* mtime_nsec */
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "setattr on fid %u (%s), attr mask 0x%"PRIx32, fid->fid, fid->path, attr->valid);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RSETATTR:
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TSETATTR, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_fsync(struct p9_handle *p9_handle, struct p9_fid *fid) {
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

	p9_initcursor(cursor, data->data, P9_TFSYNC, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "fsync on fid %u (%s)", fid->fid, fid->path);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RFSYNC:
			/* nothing else */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TFSYNC, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_link(struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_fid *dfid, char *name) {
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

	p9_initcursor(cursor, data->data, P9_TLINK, tag);
	p9_setvalue(cursor, dfid->fid, uint32_t);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setstr(cursor, strlen(name), name);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "link from fid %u (%s) to dfid %u (%s), filename %s", fid->fid, fid->path, dfid->fid, dfid->path, name);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RLINK:
			/* nothing else */
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TLINK, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_lock(struct p9_handle *p9_handle, struct p9_fid *fid, uint8_t type, uint32_t flags, uint64_t start, uint64_t length, uint32_t proc_id) {
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

	p9_initcursor(cursor, data->data, P9_TLOCK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, type, uint8_t);
	p9_setvalue(cursor, flags, uint32_t);
	p9_setvalue(cursor, start, uint64_t);
	p9_setvalue(cursor, length, uint64_t);
	p9_setvalue(cursor, proc_id, uint32_t);
	p9_setstr(cursor, strlen(p9_handle->hostname), p9_handle->hostname);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "lock on fid %u (%s), type %u, flags 0x%x, start %"PRIu64", length %"PRIu64", proc_id %u", fid->fid, fid->path, type, flags, start, length, proc_id);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RLOCK:
			p9_getvalue(cursor, rc, uint8_t);
			switch(rc) {
				case P9_LOCK_SUCCESS:
					rc = 0;
					break;

				case P9_LOCK_ERROR:
					rc = EACCES;
					break;

				case P9_LOCK_BLOCKED:
				case P9_LOCK_GRACE:
					rc = EAGAIN;
					break;

				default:
					rc=EINVAL;
					break;
			}
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TLOCK, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}


int p9p_getlock(struct p9_handle *p9_handle, struct p9_fid *fid, uint8_t *ptype, uint64_t *pstart, uint64_t *plength, uint32_t *pproc_id) {
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

	p9_initcursor(cursor, data->data, P9_TGETLOCK, tag);
	p9_setvalue(cursor, fid->fid, uint32_t);
	p9_setvalue(cursor, *ptype, uint8_t);
	p9_setvalue(cursor, *pstart, uint64_t);
	p9_setvalue(cursor, *plength, uint64_t);
	p9_setvalue(cursor, *pproc_id, uint32_t);
	p9_setstr(cursor, strlen(p9_handle->hostname), p9_handle->hostname);
	p9_setmsglen(cursor, data);

	INFO_LOG(p9_handle->debug & P9_DEBUG_PROTO, "getlock on fid %u (%s), type %u, start %"PRIu64", length %"PRIu64", proc_id %u", fid->fid, fid->path, *ptype, *pstart, *plength, *pproc_id);

	rc = p9c_sendrequest(p9_handle, data, tag);
	if (rc != 0)
		return rc;

	rc = p9c_getreply(p9_handle, &data, tag);
	if (rc != 0 || data == NULL)
		return rc;

	cursor = data->data;
	p9_getheader(cursor, msgtype);
	switch(msgtype) {
		case P9_RGETLOCK:
			p9_getvalue(cursor, *ptype, uint8_t);
			p9_getvalue(cursor, *pstart, uint64_t);
			p9_getvalue(cursor, *plength, uint64_t);
			p9_getvalue(cursor, *pproc_id, uint32_t);
			break;

		case P9_RERROR:
			p9_getvalue(cursor, rc, uint32_t);
			break;

		default:
			ERROR_LOG("Wrong reply type %u to msg %u/tag %u", msgtype, P9_TGETLOCK, tag);
			rc = EIO;
	}

	p9c_putreply(p9_handle, data);

	return rc;
}
