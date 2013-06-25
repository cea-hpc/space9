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

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>

#include "9p_internals.h"
#include "utils.h"

int p9l_walk(struct p9_handle *p9_handle, struct p9_fid *dfid, char *path, struct p9_fid **pfid, int flags) {
	int rc;
	int rec = 0, clunkdir = 0;
	struct p9_fid *fid, *tmp_dfid, *tmp_fid = NULL;
	char *basename, *readlink;
	msk_data_t *data = NULL;

	do {
		if (rec) {
			if (data) {
				p9c_putreply(p9_handle, data);
				data = NULL;
			}
			if (tmp_fid) {
				p9p_clunk(p9_handle, &tmp_fid);
				tmp_fid = NULL;
			}
			tmp_fid = fid;

			rc = p9p_lopen(p9_handle, fid, O_RDONLY, NULL);
			if (rc)
				break;
			rc = p9pz_readlink(p9_handle, fid, &readlink, &data);
			if (rc > 0 && readlink[0] != '/') {
				/* walk into link directory if path is relative from link */
				path_basename(path, &basename);
				if (path != basename) {
					basename--;
					basename[0] = '\0';
					rc = p9p_walk(p9_handle, dfid, path, &tmp_dfid);
					basename[0] = '/';
					if (rc)
						break;
					if (clunkdir)
						p9p_clunk(p9_handle, &dfid);
					dfid = tmp_dfid;
					clunkdir = 1;
				}
			} else if (rc == 0) {
				rc = ENOENT;
				break;
			} else if (rc < 0) {
				break;
			}
			path = readlink;
		}
		rc = p9p_walk(p9_handle, dfid, path, &fid);
		if (rc)
			break;

		if (flags & AT_SYMLINK_NOFOLLOW)
			break;

		rec++;
	} while (rc == 0 && fid->qid.type == P9_QTSYMLINK && rec < MAXSYMLINKS);

	/* cleanup */
	if (clunkdir) {
		p9p_clunk(p9_handle, &dfid);
	}
	if (data) {
		p9c_putreply(p9_handle, data);
	}
	if (tmp_fid) {
		p9p_clunk(p9_handle, &tmp_fid);
	}

	if (rec == MAXSYMLINKS) {
		rc = EMLINK;
		p9p_clunk(p9_handle, &fid);
	}

	if (!rc)
		*pfid = fid;

	return rc;
}
static inline int p9l_rootwalk(struct p9_handle *p9_handle, char *path, struct p9_fid **pfid, int flags) {
	return p9l_walk(p9_handle, (path[0] != '/' ? p9_handle->cwd : p9_handle->root_fid), path, pfid, flags);
}

int p9l_clunk(struct p9_fid **pfid) {
	if (!pfid || !*pfid)
		return EINVAL;

	if ((*pfid)->fid == (*pfid)->p9_handle->root_fid->fid || (*pfid)->fid == (*pfid)->p9_handle->cwd->fid)
		return 0;

	return p9p_clunk((*pfid)->p9_handle, pfid);
}

int p9l_remove(struct p9_fid **pfid) {
	if (!pfid || !*pfid)
		return EINVAL;

	if ((*pfid)->fid == (*pfid)->p9_handle->root_fid->fid || (*pfid)->fid == (*pfid)->p9_handle->cwd->fid)
		return 0;

	return p9p_remove((*pfid)->p9_handle, pfid);
}

int p9l_cd(struct p9_handle *p9_handle, char *path) {
	char *canon_path;
	struct p9_fid *fid;
	int rc;

	/* sanity checks */
	if (p9_handle == NULL || path == NULL)
		return EINVAL;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);
	rc = p9l_rootwalk(p9_handle, canon_path, &fid, 0);
	if (!rc) {
		if (fid->qid.type != P9_QTDIR) {
			rc = ENOTDIR;
			p9p_clunk(p9_handle, &fid);
		} else {
			p9p_clunk(p9_handle, &p9_handle->cwd);
			p9_handle->cwd = fid;
		}
	}

	free(canon_path);
	return rc;
}

int p9l_rm(struct p9_handle *p9_handle, char *path) {
	char *canon_path, *dirname, *basename;
	struct p9_fid *fid = NULL;
	int rc, relative;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);
	relative = path_split(canon_path, &dirname, &basename);

	if (dirname[0] != '\0') {
		rc = p9l_rootwalk(p9_handle, dirname, &fid, 0);
		if (!rc) {
			rc = p9p_unlinkat(p9_handle, fid, basename, 0);
			p9l_clunk(&fid);
		}
	} else {
		rc = p9p_unlinkat(p9_handle, (relative ? p9_handle->cwd : p9_handle->root_fid), basename, 0);
	}

	free(canon_path);
	return rc;
}

int p9l_umask(struct p9_handle *p9_handle, uint32_t mask) {
	uint32_t old_mask = p9_handle->umask;
	p9_handle->umask = mask;
	return old_mask;
}

int p9l_mkdir(struct p9_handle *p9_handle, char *path, uint32_t mode) {
	char *canon_path, *dirname, *basename;
	struct p9_fid *fid = NULL;
	int rc, relative;

	/* sanity checks */
	if (p9_handle == NULL || path == NULL)
		return EINVAL;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);
	relative = path_split(canon_path, &dirname, &basename);

	if (dirname[0] != '\0') {
		rc = p9l_rootwalk(p9_handle, dirname, &fid, 0);
		if (!rc) {
			rc = p9p_mkdir(p9_handle, fid, basename, mode & p9_handle->umask, 0, NULL);
			p9l_clunk(&fid);
		}
	} else {
		rc = p9p_mkdir(p9_handle, (relative ? p9_handle->cwd : p9_handle->root_fid), basename, mode & p9_handle->umask, 0, NULL);
	}

	free(canon_path);
	return rc;
}

int p9l_symlink(struct p9_handle *p9_handle, char *target, char *linkname) {
	char *canon_path, *dirname, *basename;
	struct p9_fid *fid = NULL;
	int rc, relative;

	/* sanity checks */
	if (p9_handle == NULL || target == NULL || linkname == NULL)
		return EINVAL;

	canon_path = malloc(strlen(linkname)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, linkname);
	path_canonicalizer(canon_path);
	relative = path_split(canon_path, &dirname, &basename);

	if (dirname[0] != '\0') {
		rc = p9l_rootwalk(p9_handle, dirname, &fid, 0);
		if (!rc) {
			rc = p9p_symlink(p9_handle, fid, basename, target, getegid(), NULL);
			p9l_clunk(&fid);
		}
	} else {
		rc = p9p_symlink(p9_handle, (relative ? p9_handle->cwd : p9_handle->root_fid), basename, target, getegid(), NULL);
	}

	free(canon_path);
	return rc;
}

int p9l_link(struct p9_handle *p9_handle, char *target, char *linkname) {
	char *target_canon_path;
	char *linkname_canon_path, *linkname_dirname, *linkname_basename;
	struct p9_fid *target_fid = NULL, *linkname_fid = NULL;
	int rc, linkname_relative;

	/* sanity checks */
	if (p9_handle == NULL || target == NULL || linkname == NULL)
		return EINVAL;

	target_canon_path = malloc(strlen(target)+1);
	linkname_canon_path = malloc(strlen(linkname)+1);

	do {
		if (!target_canon_path || !linkname_canon_path) {
			rc = ENOMEM;
			break;
		}

		strcpy(target_canon_path, target);
		path_canonicalizer(target_canon_path);
		rc = p9l_rootwalk(p9_handle, target_canon_path, &target_fid, 0);
		if (rc) {
			INFO_LOG(p9_handle->debug, "walk failed to '%s', %s (%d)", target_canon_path, strerror(rc), rc);
			break;
		}

		strcpy(linkname_canon_path, linkname);
		path_canonicalizer(linkname_canon_path);

		/* check if linkname is a directory first */
		rc = p9l_rootwalk(p9_handle, linkname_canon_path, &linkname_fid, 0);
		if (rc != 0 && rc != ENOENT) {
			INFO_LOG(p9_handle->debug, "walk failed to '%s', %s (%d)", linkname_canon_path, strerror(rc), rc);
			break;
		} else if (rc == ENOENT || linkname_fid->qid.type != P9_QTDIR) {
			/* not a directory, walk to dirname instead */
			linkname_relative = path_split(linkname_canon_path, &linkname_dirname, &linkname_basename);
			if (linkname_dirname[0] != '\0') {
				rc = p9l_rootwalk(p9_handle, linkname_dirname, &linkname_fid, 0);
				if (rc) {
					INFO_LOG(p9_handle->debug, "walk failed to '%s', %s (%d)", linkname_dirname, strerror(rc), rc);
					break;
				}
			} else {
				linkname_fid = linkname_relative ? p9_handle->root_fid : p9_handle ->cwd;
			}
		} else {
			/* is a directory, use it. */
			path_basename(target_canon_path, &linkname_basename);
		}

		if (linkname_basename[0] == '\0')
			path_basename(target_canon_path, &linkname_basename);

		rc = p9p_link(p9_handle, target_fid, linkname_fid, linkname_basename);
		if (rc) {
			INFO_LOG(p9_handle->debug, "link failed with target fid %u (%s) to dir fid %u (%s), name %s, error: %s (%d)", target_fid->fid, target_fid->path, linkname_fid->fid, linkname_fid->path, linkname_basename, strerror(rc), rc);
		}
	} while (0);

	p9l_clunk(&target_fid);
	p9l_clunk(&linkname_fid);
	free(target_canon_path);
	free(linkname_canon_path);
	return rc;
}

int p9l_mv(struct p9_handle *p9_handle, char *src, char *dst) {
	char *src_canon_path, *src_dirname, *src_basename;
	char *dst_canon_path, *dst_dirname, *dst_basename;
	struct p9_fid *src_fid = NULL, *dst_fid = NULL;
	int rc, src_relative, dst_relative;

	/* sanity checks */
	if (p9_handle == NULL || src == NULL || dst == NULL)
		return EINVAL;

	src_canon_path = malloc(strlen(src)+1);
	dst_canon_path = malloc(strlen(dst)+1);

	do {
		if (!src_canon_path || !dst_canon_path) {
			rc = ENOMEM;
			break;
		}

		strcpy(src_canon_path, src);
		path_canonicalizer(src_canon_path);
		src_relative = path_split(src_canon_path, &src_dirname, &src_basename);
		if (src_dirname[0] != '\0') {
			rc = p9l_rootwalk(p9_handle, src_dirname, &src_fid, 0);
			if (rc) {
				INFO_LOG(p9_handle->debug, "walk failed to '%s', %s (%d)", src_dirname, strerror(rc), rc);
				break;
			}
		} else {
			src_fid = src_relative ? p9_handle->cwd : p9_handle->root_fid;
		}

		strcpy(dst_canon_path, dst);
		path_canonicalizer(dst_canon_path);

		/* check if dst is a directory first */
		rc = p9l_rootwalk(p9_handle, dst_canon_path, &dst_fid, 0);
		if (rc != 0 && rc != ENOENT) {
			INFO_LOG(p9_handle->debug, "walk failed to '%s', %s (%d)", dst_canon_path, strerror(rc), rc);
			break;
		} else if (rc == ENOENT || dst_fid->qid.type != P9_QTDIR) {
			/* not a directory, walk to dirname instead */
			dst_relative = path_split(dst_canon_path, &dst_dirname, &dst_basename);
			if (dst_dirname[0] != '\0') {
				rc = p9l_rootwalk(p9_handle, dst_dirname, &dst_fid, 0);
				if (rc) {
					INFO_LOG(p9_handle->debug, "walk failed to '%s', %s (%d)", dst_dirname, strerror(rc), rc);
					break;
				}
			} else {
				dst_fid = dst_relative ? p9_handle->root_fid : p9_handle ->cwd;
			}
		} else {
			/* is a directory, use it. */
			dst_basename = src_basename;
		}

		if (dst_basename[0] == '\0')
			dst_basename = src_basename;

		rc = p9p_renameat(p9_handle, src_fid, src_basename, dst_fid, dst_basename);
		if (rc) {
			INFO_LOG(p9_handle->debug, "renameat failed on dir fid %u (%s), name %s to dir fid %u (%s), name %s, error: %s (%d)", src_fid->fid, src_fid->path, src_basename, dst_fid->fid, dst_fid->path, dst_basename, strerror(rc), rc);
		}
	} while (0);

	p9l_clunk(&src_fid);
	p9l_clunk(&dst_fid);
	free(src_canon_path);
	free(dst_canon_path);
	return rc;
}

int p9l_open(struct p9_handle *p9_handle, struct p9_fid **pfid, char *path, uint32_t mode, uint32_t flags, uint32_t gid) {
	char *canon_path, *dirname, *basename;
	struct p9_fid *fid = NULL;
	struct p9_setattr attr;
	int rc, relative;

	/* sanity checks */
	if (p9_handle == NULL || pfid == NULL || path == NULL)
		return EINVAL;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);

	do {
		rc = p9l_rootwalk(p9_handle, canon_path, &fid, 0);
		if (rc && flags & O_CREAT) {
			/* file doesn't exist */
			relative = path_split(canon_path, &dirname, &basename);
			if (basename[0] == '\0') {
				return EINVAL;
			}

			rc = p9p_walk(p9_handle, relative ? p9_handle->cwd : p9_handle->root_fid, dirname, &fid);
			if (rc) {
				INFO_LOG(p9_handle->debug, "cannot walk into parent dir '%s', %s (%d)", dirname, strerror(rc), rc);
				break;
			}
			rc = p9p_lcreate(p9_handle, fid, basename, mode & p9_handle->umask, flags, gid, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug, "cannot create file '%s' in '%s', %s (%d)", basename, dirname, strerror(rc), rc);
				break;
			}
		} else if (rc) {
			INFO_LOG(p9_handle->debug, "cannot open file '%s', %s (%d)", canon_path, strerror(rc), rc);
			break;
		} else {
			/* file exists, open and eventually truncate */
			rc = p9p_lopen(p9_handle, fid, flags, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug, "cannot open existing file '%s', %s (%d)", canon_path, strerror(rc), rc);
				break;
			}
			if (flags & O_TRUNC) {
				memset(&attr, 0, sizeof(attr));
				attr.valid = P9_SETATTR_SIZE;
				attr.size = 0;
				p9p_setattr(p9_handle, fid, &attr);				
			}
		}
		if (flags & O_APPEND) {
			p9l_fseek(fid, 0, SEEK_END);
		} else {
			p9l_fseek(fid, 0, SEEK_SET);
		}
	} while (0);

	if (rc)
		p9l_clunk(&fid);
	else
		*pfid = fid;

	free(canon_path);
	return rc;
}

int p9l_chown(struct p9_handle *p9_handle, char *path, uint32_t uid, uint32_t gid) {
	int rc;
	struct p9_fid *fid;

	rc = p9l_rootwalk(p9_handle, path, &fid, 0);
	if (!rc) {
		rc = p9l_fchown(fid, uid, gid);
		p9l_clunk(&fid);
	}

	return rc;
}

int p9l_chmod(struct p9_handle *p9_handle, char *path, uint32_t mode) {
	int rc;
	struct p9_fid *fid;

	rc = p9l_rootwalk(p9_handle, path, &fid, 0);
	if (!rc) {
		rc = p9l_fchmod(fid, mode);
		p9l_clunk(&fid);
	}

	return rc;
}

int p9l_stat(struct p9_handle *p9_handle, char *path, struct p9_getattr *attr) {
	int rc;
	struct p9_fid *fid;

	rc = p9l_rootwalk(p9_handle, path, &fid, 0);
	if (!rc) {
		rc = p9l_fstat(fid, attr);
		p9l_clunk(&fid);
	}

	return rc;
}

int p9l_lstat(struct p9_handle *p9_handle, char *path, struct p9_getattr *attr) {
	int rc;
	struct p9_fid *fid;

	rc = p9l_rootwalk(p9_handle, path, &fid, AT_SYMLINK_NOFOLLOW);
	if (!rc) {
		rc = p9l_fstat(fid, attr);
		p9l_clunk(&fid);
	}

	return rc;
}

/* flags = 0 or AT_SYMLINK_NOFOLLOW */
int p9l_fstatat(struct p9_fid *dfid, char *path, struct p9_getattr *attr, int flags) {
	int rc;
	struct p9_fid *fid;

	rc = p9l_walk(dfid->p9_handle, dfid, path, &fid, flags);
	if (!rc) {
		rc = p9l_fstat(fid, attr);
		p9l_clunk(&fid);
	}

	return rc;
}

int p9l_fseek(struct p9_fid *fid, int64_t offset, int whence) {
	int rc = 0;
	struct p9_getattr attr;

	switch(whence) {
		case SEEK_SET:
			if (offset >= 0)
				fid->offset = offset;
			break;
		case SEEK_CUR:
			fid->offset += offset;
			break;
		case SEEK_END:
			attr.valid = P9_GETATTR_SIZE;
			rc = p9p_getattr(fid->p9_handle, fid, &attr);
			if (!rc) {
				fid->offset = attr.size + offset;
			}
			break;
		default:
			rc = EINVAL;
	}
	return rc;
}

uint64_t p9l_ftell(struct p9_fid *fid) {
	return fid->offset;
}

ssize_t p9l_write(struct p9_fid *fid, char *buffer, size_t count) {
	ssize_t rc;
	msk_data_t data;
	size_t sent = 0;

	/* sanity checks */
	if (fid == NULL || buffer == NULL || (fid->openflags & WRFLAG) == 0)
		return -EINVAL;

	if (count < 512*1024) { /* copy the buffer if it's less than 500k */
		do {
			rc = p9p_write(fid->p9_handle, fid, buffer + sent, count - sent, fid->offset);
			if (rc <= 0)
				break;
			fid->offset += rc;
			sent += rc;
		} while (sent < count);
	} else { /* register the whole buffer and send it */
		data.data = buffer;
		data.size = count;
		data.max_size = count;
		p9c_reg_mr(fid->p9_handle, &data);
		do {
			data.size = count - sent;
			data.data = buffer + sent;
			rc = p9pz_write(fid->p9_handle, fid, &data, fid->offset);
			if (rc <= 0)
				break;
			fid->offset += rc;
		} while (sent < count);
		p9c_dereg_mr(fid->p9_handle, &data);
	}
	if (rc < 0) {
		INFO_LOG(fid->p9_handle->debug, "write failed on file %s at offset %"PRIu64", error: %s (%zu)", fid->path, fid->offset, strerror(-rc), -rc);
	} else {
		rc = sent;
	}

	return rc;	
}

ssize_t p9l_writev(struct p9_fid *fid, struct iovec *iov, int iovcnt) {
	ssize_t rc = 0, total = 0;
	int i;

	if (iovcnt < 0) {
		return EINVAL;
	}

	for (i = 0; i < iovcnt; i++) {
		rc = p9l_write(fid, iov[i].iov_base, iov[i].iov_len);
		if (rc < 0)
			break;
		if (rc < iov[i].iov_len) {
			rc += total;
			break;
		}
		total += rc;
	}
	return rc;
}

ssize_t p9l_read(struct p9_fid *fid, char *buffer, size_t count) {
	ssize_t rc, total = 0;

	/* sanity checks */
	if (fid == NULL || buffer == NULL || (fid->openflags & RDFLAG) == 0 )
		return EINVAL;

	do {
		rc = p9p_read(fid->p9_handle, fid, buffer + total, count - total, fid->offset);
		if (rc <= 0)
			break;
		fid->offset += rc;
		total += rc;
	} while (total < count);

	if (rc < 0) {
		INFO_LOG(fid->p9_handle->debug, "read failed on file %s at offset %"PRIu64", error: %s (%zu)", fid->path, fid->offset, strerror(-rc), -rc);
	} else {
		rc = total;
	}

	return rc;
}

ssize_t p9l_readv(struct p9_fid *fid, struct iovec *iov, int iovcnt) {
	ssize_t rc = 0, total = 0;
	int i;

	if (iovcnt < 0) {
		return EINVAL;
	}

	for (i = 0; i < iovcnt; i++) {
		rc = p9l_read(fid, iov[i].iov_base, iov[i].iov_len);
		if (rc < 0)
			break;
		if (rc < iov[i].iov_len) {
			rc += total;
			break;
		}
		total += rc;
	}
	return rc;
}

ssize_t p9l_ls(struct p9_handle *p9_handle, char *arg, p9p_readdir_cb cb, void *cb_arg) {
	int rc = 0;
	struct p9_fid *fid;
	uint64_t offset = 0LL;
	int count;

	rc = p9l_open(p9_handle, &fid, arg, 0, 0, 0);
	if (rc) {
		INFO_LOG(p9_handle->debug, "couldn't open '%s', error: %s (%d)\n", arg, strerror(rc), rc);
		return -rc;
	}

	if (fid->qid.type == P9_QTDIR) {
		do {
			count = p9p_readdir(p9_handle, fid, &offset, cb, cb_arg);
			if (count > 0)
				rc += count;
		} while (count > 0);

		if (count < 0) {
			rc = count;
			INFO_LOG(p9_handle->debug, "readdir failed on fid %u (%s): %s (%d)\n", p9_handle->cwd->fid, p9_handle->cwd->path, strerror(rc), rc);
		}
	} else {
		rc = -ENOTDIR;
	}

	p9p_clunk(p9_handle, &fid);
	return rc;
}
