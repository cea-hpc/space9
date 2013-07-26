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

int p9l_mkdir(struct p9_fid *fid, char *path, uint32_t mode) {
	struct p9_handle *p9_handle = fid->p9_handle;
	char *canon_path, *dirname, *basename;
	struct p9_fid *dfid = NULL;
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
		rc = p9l_walk(p9_handle, (relative ? fid : p9_handle->root_fid), dirname, &dfid, 0);
		if (!rc) {
			rc = p9p_mkdir(p9_handle, dfid, basename, (mode ? mode : 0666) & ~p9_handle->umask, 0, NULL);
			p9l_clunk(&dfid);
		}
	} else {
		rc = p9p_mkdir(p9_handle, (relative ? fid : p9_handle->root_fid), basename, (mode ? mode : 0666) & ~p9_handle->umask, 0, NULL);
	}

	free(canon_path);
	return rc;
}


/* static inline int p9l_mkdir(struct p9_handle *p9_handle, char *path, uint32_t mode) {
	return p9l_mkdirat(p9_handle->cwd, path, mode);
} */


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
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", target_canon_path, strerror(rc), rc);
			break;
		}

		strcpy(linkname_canon_path, linkname);
		path_canonicalizer(linkname_canon_path);

		/* check if linkname is a directory first */
		rc = p9l_rootwalk(p9_handle, linkname_canon_path, &linkname_fid, 0);
		if (rc != 0 && rc != ENOENT) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", linkname_canon_path, strerror(rc), rc);
			break;
		} else if (rc == ENOENT || linkname_fid->qid.type != P9_QTDIR) {
			/* not a directory, walk to dirname instead */
			linkname_relative = path_split(linkname_canon_path, &linkname_dirname, &linkname_basename);
			if (linkname_dirname[0] != '\0') {
				rc = p9l_rootwalk(p9_handle, linkname_dirname, &linkname_fid, 0);
				if (rc) {
					INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", linkname_dirname, strerror(rc), rc);
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
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "link failed with target fid %u (%s) to dir fid %u (%s), name %s, error: %s (%d)", target_fid->fid, target_fid->path, linkname_fid->fid, linkname_fid->path, linkname_basename, strerror(rc), rc);
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
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", src_dirname, strerror(rc), rc);
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
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", dst_canon_path, strerror(rc), rc);
			break;
		} else if (rc == ENOENT || dst_fid->qid.type != P9_QTDIR) {
			/* not a directory, walk to dirname instead */
			dst_relative = path_split(dst_canon_path, &dst_dirname, &dst_basename);
			if (dst_dirname[0] != '\0') {
				rc = p9l_rootwalk(p9_handle, dst_dirname, &dst_fid, 0);
				if (rc) {
					INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", dst_dirname, strerror(rc), rc);
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
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "renameat failed on dir fid %u (%s), name %s to dir fid %u (%s), name %s, error: %s (%d)", src_fid->fid, src_fid->path, src_basename, dst_fid->fid, dst_fid->path, dst_basename, strerror(rc), rc);
		}
	} while (0);

	p9l_clunk(&src_fid);
	p9l_clunk(&dst_fid);
	free(src_canon_path);
	free(dst_canon_path);
	return rc;
}

int p9l_cp(struct p9_handle *p9_handle, char *src, char *dst) {
	char *src_canon_path, *src_dirname, *src_basename;
	char *dst_canon_path;
	struct p9_fid *src_fid = NULL, *dst_fid = NULL, *dst_dir_fid = NULL;
	int rc;
	msk_data_t *data;
	struct p9_setattr attr;
	uint64_t offset;

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
		rc = p9l_open(p9_handle, src, &src_fid, O_RDONLY, 0, 0);
		if (rc) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "open failed on '%s', %s (%d)", src_canon_path, strerror(rc), rc);
			break;
		}
		path_split(src_canon_path, &src_dirname, &src_basename);

		strcpy(dst_canon_path, dst);
		path_canonicalizer(dst_canon_path);

		/* check if dst is a directory first */
		rc = p9l_rootwalk(p9_handle, dst_canon_path, &dst_dir_fid, 0);
		if (rc != 0 && rc != ENOENT) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "walk failed to '%s', %s (%d)", dst_canon_path, strerror(rc), rc);
			break;
		} else if (rc == ENOENT) {
			/* doesn't exist, create it */
			rc = p9l_open(p9_handle, dst_canon_path, &dst_fid, O_WRONLY | O_CREAT | O_TRUNC, 0666, 0);
			if (rc) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "open failed on '%s', %s (%d)", dst_canon_path, strerror(rc), rc);
				break;
			}
		} else if (dst_dir_fid->qid.type != P9_QTDIR) {
			/* exists, open and truncate */
			dst_fid = dst_dir_fid;
			dst_dir_fid = NULL;
			rc = p9p_lopen(p9_handle, dst_fid, O_WRONLY, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "cannot open existing file '%s', %s (%d)", dst_dir_fid->path, strerror(rc), rc);
				break;
			}
			memset(&attr, 0, sizeof(attr));
			attr.valid = P9_SETATTR_SIZE;
			attr.size = 0;
			p9p_setattr(p9_handle, dst_fid, &attr);
		} else {
			/* is a directory, open inside */
			rc = p9l_openat(p9_handle, dst_dir_fid, src_basename, &dst_fid, O_WRONLY | O_CREAT | O_TRUNC, 0666, 0);
			if (rc) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "open failed on '%s', %s (%d)", dst_canon_path, strerror(rc), rc);
				break;
			}
		}

		/* copy stuff */
		offset = 0;
		do {
			rc = p9pz_read(p9_handle, src_fid, p9_handle->msize, offset, &data);
			if (rc < 0) {
				printf("read failed on fid %u (%s) at offset %"PRIu64"\n", src_fid->fid, src_fid->path, offset);
				rc = -rc;
				break;
			}
			if (rc == 0)
				break;
			rc = p9pz_write(p9_handle, dst_fid, data, offset);
			if (rc < 0) {
				printf("write failed on fid %u (%s) at offset %"PRIu64"\n", dst_fid->fid, dst_fid->path, offset);
				rc = -rc;
				break;
			}
			offset += rc;
			p9c_putreply(p9_handle, data);
		} while (rc > 0);

	} while (0);

	p9l_clunk(&src_fid);
	p9l_clunk(&dst_fid);
	p9l_clunk(&dst_dir_fid);
	free(src_canon_path);
	free(dst_canon_path);
	return rc;
}

int p9l_openat(struct p9_handle *p9_handle, struct p9_fid *dfid, char *path, struct p9_fid **pfid, uint32_t flags, uint32_t mode, uint32_t gid) {
	char *canon_path, *dirname, *basename;
	struct p9_fid *fid = NULL;
	struct p9_setattr attr;
	int rc, relative;

	/* sanity checks */
	if (p9_handle == NULL || dfid == NULL || pfid == NULL || path == NULL)
		return EINVAL;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);

	do {
		rc = p9l_walk(p9_handle, dfid, canon_path, &fid, 0);
		if (rc && flags & O_CREAT) {
			/* file doesn't exist */
			relative = path_split(canon_path, &dirname, &basename);
			if (basename[0] == '\0') {
				return EINVAL;
			}

			rc = p9p_walk(p9_handle, relative ? dfid : p9_handle->root_fid, dirname, &fid);
			if (rc) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "cannot walk into parent dir '%s', %s (%d)", dirname, strerror(rc), rc);
				break;
			}
			rc = p9p_lcreate(p9_handle, fid, basename, flags, (mode ? mode : 0666) & ~p9_handle->umask, gid, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "cannot create file '%s' in '%s', %s (%d)", basename, dirname, strerror(rc), rc);
				break;
			}
		} else if (rc) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "cannot open file '%s', %s (%d)", canon_path, strerror(rc), rc);
			break;
		} else {
			/* file exists, open and eventually truncate */
			rc = p9p_lopen(p9_handle, fid, flags, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "cannot open existing file '%s', %s (%d)", canon_path, strerror(rc), rc);
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

int p9l_open(struct p9_handle *p9_handle, char *path, struct p9_fid **pfid, uint32_t flags, uint32_t mode, uint32_t gid) {
	return p9l_openat(p9_handle, (path[0] != '/' ? p9_handle->cwd : p9_handle->root_fid), path, pfid, flags, mode, gid);
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

struct p9_wrpipe {
	uint16_t tag;
	msk_data_t data;
	uint64_t offset;
};

ssize_t p9l_write(struct p9_fid *fid, char *buffer, size_t count) {
	ssize_t rc = 0;
	size_t sent = 0, subsize;
	struct p9_wrpipe *pipeline;
	int tag_first, tag_last;
	const uint32_t n_pipeline = fid->p9_handle->pipeline;
	const uint32_t chunksize = p9p_write_len(fid->p9_handle, count);

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
		pipeline = malloc(fid->p9_handle->pipeline * sizeof(struct p9_wrpipe));
		if (!pipeline) {
			rc = -ENOMEM;
		}
		pipeline[0].data.data = buffer;
		pipeline[0].data.size = count;
		pipeline[0].data.max_size = count;
		p9c_reg_mr(fid->p9_handle, &pipeline[0].data);
		for (tag_first = 0; tag_first < fid->p9_handle->pipeline; tag_first++) {
			pipeline[tag_first].data.mr = pipeline[0].data.mr;
			pipeline[tag_first].data.size = chunksize;
			pipeline[tag_first].data.max_size = chunksize;
		}

		tag_first = 1 - fid->p9_handle->pipeline;
		tag_last = 0;
		while (rc >= 0) {
			if (count - sent < chunksize)
				pipeline[tag_last % n_pipeline].data.size = count - sent;

			pipeline[tag_last % n_pipeline].data.data = buffer + sent;
			pipeline[tag_last % n_pipeline].offset = fid->offset + sent;
			rc = p9pz_write_send(fid->p9_handle, fid, &pipeline[tag_last % n_pipeline].data, fid->offset + sent, &pipeline[tag_last % n_pipeline].tag);
			if (rc < 0)
				break;
			sent += pipeline[tag_last % n_pipeline].data.size;
			tag_last++;
			if (sent >= count)
				break;
			if (tag_first >= 0) {
				rc = p9pz_write_wait(fid->p9_handle, pipeline[tag_first % n_pipeline].tag);
				if (rc < 0) {
					INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "write failed: %s (%zd)\n", strerror(-rc), -rc);
					break;
				}
				if (rc != pipeline[tag_first % n_pipeline].data.size) {
					INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "not a full write!!! wrote %zu, expected %u\n", rc, pipeline[tag_first % n_pipeline].data.size);
					/* fall back to regular write /!\ NEEDS TESTING /!\ */
					subsize = rc;
					while (subsize < pipeline[tag_first % n_pipeline].data.size) {
						rc = p9p_write(fid->p9_handle, fid, pipeline[tag_first % n_pipeline].data.data + subsize, pipeline[tag_first % n_pipeline].data.size - subsize, pipeline[tag_first % n_pipeline].offset + subsize);
						if (rc < 0)
							break;
						subsize += rc;
					}
				}
			}
			tag_first++;
		}
		/** FIXME wait for writes even if rc < 0, keep rc for return value - first one matters */
		if (tag_first < 0)
			tag_first = 0;
		while (rc >= 0 && tag_first < tag_last) {
			rc = p9pz_write_wait(fid->p9_handle, pipeline[tag_first % n_pipeline].tag);
			if (rc < 0) {
				INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "write failed: %s (%zd)\n", strerror(-rc), -rc);
				break;
			}
			if (rc != pipeline[tag_first % n_pipeline].data.size) {
				INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "not a full write!!! wrote %zu, expected %u\n", rc, pipeline[tag_first % n_pipeline].data.size);
				/* fall back to regular write /!\ NEEDS TESTING /!\ */
				subsize = rc;
				while (subsize < pipeline[tag_first % n_pipeline].data.size) {
					rc = p9p_write(fid->p9_handle, fid, pipeline[tag_first % n_pipeline].data.data + subsize, pipeline[tag_first % n_pipeline].data.size - subsize, pipeline[tag_first % n_pipeline].offset + subsize);
					if (rc < 0)
						break;
					subsize += rc;
				}
			}
			tag_first++;
		}

		p9c_dereg_mr(fid->p9_handle, &pipeline[0].data);
		free(pipeline);
		fid->offset += sent;
	}
	if (rc < 0) {
		INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "write failed on file %s at offset %"PRIu64", error: %s (%zu)", fid->path, fid->offset, strerror(-rc), -rc);
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

struct p9_rdpipe {
	uint16_t tag;
	uint32_t size;
	uint64_t offset;
	char       *buf;
	msk_data_t *data;
};


ssize_t p9l_read(struct p9_fid *fid, char *buffer, size_t count) {
	ssize_t rc = 0;
	size_t total = 0, subsize;
	struct p9_rdpipe *pipeline;
	int tag_first, tag_last;
	const uint32_t n_pipeline = fid->p9_handle->pipeline;
	const uint32_t chunksize = p9p_read_len(fid->p9_handle, count);

	/* sanity checks */
	if (fid == NULL || buffer == NULL || (fid->openflags & RDFLAG) == 0 )
		return -EINVAL;

	pipeline = malloc(fid->p9_handle->pipeline * sizeof(struct p9_rdpipe));

	for (tag_first = 0; tag_first < fid->p9_handle->pipeline; tag_first++) {
		pipeline[tag_first].size = chunksize;
	}

	tag_first = 1 - fid->p9_handle->pipeline;
	tag_last = 0;
	do {
		if (count - total < chunksize)
			pipeline[tag_last % n_pipeline].size = count - total;

		pipeline[tag_last % n_pipeline].buf = buffer + total;
		pipeline[tag_last % n_pipeline].offset = fid->offset + total;
		rc = p9pz_read_send(fid->p9_handle, fid, pipeline[tag_last % n_pipeline].size, fid->offset + total, &pipeline[tag_last % n_pipeline].tag);
		if (rc < 0)
			break;
		total += pipeline[tag_last % n_pipeline].size;
		tag_last++;
		if (total >= count)
			break;
		if (tag_first >= 0) {
			rc = p9pz_read_wait(fid->p9_handle, &pipeline[tag_first % n_pipeline].data, pipeline[tag_first % n_pipeline].tag);
			if (rc < 0) {
				INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "write failed: %s (%zd)\n", strerror(-rc), -rc);
				break;
			}
			memcpy(pipeline[tag_first % n_pipeline].buf, pipeline[tag_first % n_pipeline].data->data, rc);
			p9pz_read_put(fid->p9_handle, pipeline[tag_first % n_pipeline].data);
			if (rc != pipeline[tag_first % n_pipeline].size) {
				INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "not a full read!!! read %zu, expected %u\n", rc, pipeline[tag_first % n_pipeline].size);
				/* fall back to regular read /!\ NEEDS TESTING /!\ */
				subsize = rc;
				while (subsize < pipeline[tag_first % n_pipeline].size) {
					rc = p9p_read(fid->p9_handle, fid, pipeline[tag_first % n_pipeline].buf + subsize, pipeline[tag_first % n_pipeline].size - subsize, pipeline[tag_first % n_pipeline].offset + subsize);
					if (rc < 0)
						break;
					subsize += rc;
				}
			}
		}
		tag_first++;
	} while (count > total);
	/** FIXME wait for writes even if rc < 0, keep rc for return value - first one matters */
	if (tag_first < 0)
		tag_first = 0;
	while (rc >= 0 && tag_first < tag_last) {
		rc = p9pz_read_wait(fid->p9_handle, &pipeline[tag_first % n_pipeline].data, pipeline[tag_first % n_pipeline].tag);
		if (rc < 0) {
			INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "write failed: %s (%zd)\n", strerror(-rc), -rc);
			break;
		}
		memcpy(pipeline[tag_first % n_pipeline].buf, pipeline[tag_first % n_pipeline].data->data, rc);
		p9pz_read_put(fid->p9_handle, pipeline[tag_first % n_pipeline].data);
		if (rc != pipeline[tag_first % n_pipeline].size) {
			INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "not a full read!!! read %zu, expected %u\n", rc, pipeline[tag_first % n_pipeline].size);
			/* fall back to regular read /!\ NEEDS TESTING /!\ */
			subsize = rc;
			while (subsize < pipeline[tag_first % n_pipeline].size) {
				rc = p9p_read(fid->p9_handle, fid, pipeline[tag_first % n_pipeline].buf + subsize, pipeline[tag_first % n_pipeline].size - subsize, pipeline[tag_first % n_pipeline].offset + subsize);
				if (rc < 0)
					break;
				subsize += rc;
			}
		}
		tag_first++;
	}

	if (rc < 0) {
		INFO_LOG(fid->p9_handle->debug & P9_DEBUG_LIBC, "read failed on file %s at offset %"PRIu64", error: %s (%zu)", fid->path, fid->offset, strerror(-rc), -rc);
	} else {
		fid->offset += total;
		rc = total;
	}

	free(pipeline);

	return rc;
}

ssize_t p9l_readv(struct p9_fid *fid, struct iovec *iov, int iovcnt) {
	ssize_t rc = 0, total = 0;
	int i;

	if (iovcnt < 0) {
		return -EINVAL;
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

ssize_t p9l_ls(struct p9_handle *p9_handle, char *path, p9p_readdir_cb cb, void *cb_arg) {
	int rc = 0;
	struct p9_fid *fid;
	uint64_t offset = 0LL;
	int count;

	rc = p9l_open(p9_handle, path, &fid, 0, 0, 0);
	if (rc) {
		INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "couldn't open '%s', error: %s (%d)\n", path, strerror(rc), rc);
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
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "readdir failed on fid %u (%s): %s (%d)\n", p9_handle->cwd->fid, p9_handle->cwd->path, strerror(rc), rc);
		}
	} else {
		rc = -ENOTDIR;
	}

	p9p_clunk(p9_handle, &fid);
	return rc;
}

/* field = NULL or "" -> get the list.
returned size is the size of the xattr, NOT the number of bytes written.
if value > count, string was truncated and buf has been forcibly NUL-terminated. */
ssize_t p9l_xattrget(struct p9_handle *p9_handle, char *path, char *field, char *buf, size_t count) {
	ssize_t rc;
	struct p9_fid *fid = NULL;

	/* Sanity check */
	if (p9_handle == NULL || path == NULL)
		return -EINVAL;

	do {
		rc = p9l_rootwalk(p9_handle, path, &fid, 0);
		if (rc)
			break;

		rc = p9l_fxattrget(fid, field, buf, count);
	} while (0);

	p9p_clunk(p9_handle, &fid);

	return rc;
}

/* flags: XATTR_CREATE (fail if exist), XATTR_REPLACE (fails if not exist)
 attribute will be removed if buf is NULL or "" */
ssize_t p9l_xattrset(struct p9_handle *p9_handle, char *path, char *field, char *buf, size_t count, int flags) {
	ssize_t rc;
	struct p9_fid *fid = NULL;

	/* Sanity check */
	if (p9_handle == NULL || path == NULL)
		return -EINVAL;

	do {
		rc = p9l_rootwalk(p9_handle, path, &fid, 0);
		if (rc)
			break;

		rc = p9l_fxattrset(fid, field, buf, count, flags);
	} while (0);

	p9p_clunk(p9_handle, &fid);

	return rc;
}

ssize_t p9l_fxattrget(struct p9_fid *fid, char *field, char *buf, size_t count) {
	ssize_t rc;
	uint64_t size;
	size_t realcount;
	struct p9_handle *p9_handle;
	struct p9_fid *attrfid = NULL;

	if (fid == NULL || buf == NULL || count == 0)
		return -EINVAL;

	p9_handle = fid->p9_handle;

	do {
		rc = p9p_xattrwalk(p9_handle, fid, &attrfid, field, &size);
		if (rc) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "xattrwalk failed: %s (%zd)", strerror(rc), rc);
			rc = -rc;
			break;
		}

		realcount = MIN(size, count-1);

		rc = p9l_read(attrfid, buf, realcount);
		if (rc < 0) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "read failed: %s (%zd)", strerror(-rc), -rc);
			break;
		} else if (rc != realcount) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "read screwup, didn't read everything (expected %zu, got %zu)", realcount, rc);
			buf[rc] = '\0';
			//rc = -EIO;
			break;
		}

		buf[realcount] = '\0';
	} while (0);

	p9p_clunk(p9_handle, &attrfid);

	return (rc < 0 ? rc : size);
}
ssize_t p9l_fxattrset(struct p9_fid *fid, char *field, char *buf, size_t count, int flags) {
	ssize_t rc;
	struct p9_handle *p9_handle;
	struct p9_fid *attrfid = NULL;

	if (fid == NULL || field == NULL || (count != 0 && buf == NULL))
		return -EINVAL;

	p9_handle = fid->p9_handle;

	do {
		rc = p9p_walk(p9_handle, fid, NULL, &attrfid);
		if (rc) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "clone walk failed: %s (%zd)", strerror(rc), rc);
			rc = -rc;
			break;
		}

		rc = p9p_xattrcreate(p9_handle, attrfid, field, count, flags);
		if (rc) {
			INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "xattrcreate failed: %s (%zd)", strerror(rc), rc);
			rc = -rc;
			break;
		}

		if (count) {
			rc = p9l_write(attrfid, buf, count);
			if (rc < 0) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "write failed: %s (%zd)", strerror(-rc), -rc);
				break;
			} else if (rc != count) {
				INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "write screwup, didn't write everything (expected %zu, got %zu)", count, rc);
				rc = -EIO;
				break;
			}
		}
	} while (0);

	p9p_clunk(p9_handle, &attrfid);

	return rc;
};


static ssize_t p9l_createtree_fid(struct p9_fid *fid, int depth, int dwidth, int fwidth) {
	ssize_t nentries = 0, rc;
	int i;
	struct p9_handle *p9_handle = fid->p9_handle;
	struct p9_fid *newfid;
	char name[MAXNAMLEN];

	if (depth > 0) {
		for (i = 0; i < dwidth; i++) {
			snprintf(name, MAXNAMLEN, "dir.%i", i);
			rc = p9l_mkdir(fid, name, 0);
			if (rc && rc != EEXIST) {
				printf("couldn't create directory %s in %s, error %s (%zd)\n", name, fid->path, strerror(rc), rc);
				break;
			} else if (rc == 0) {
				nentries++;
			}

			rc = p9l_walk(p9_handle, fid, name, &newfid, 0);
			if (rc) {
				printf("couldn't go to directory %s in %s, error: %s (%zd)\n", name, fid->path, strerror(rc), rc);
				break;
			}
			rc = p9l_createtree_fid(newfid, depth-1, dwidth, fwidth);
			p9l_clunk(&newfid);
			if (rc < 0) {
				nentries = -rc;
				break;
			}
			nentries += rc;
		}
		fwidth -= dwidth;
	}
	for (i = 0; i < fwidth; i++) {
		snprintf(name, MAXNAMLEN, "file.%i", i);
		rc = p9l_openat(p9_handle, fid, name, &newfid, O_CREAT | O_WRONLY, 0, 0);
		p9l_clunk(&newfid);
		if (rc && rc != EEXIST) {
			printf("couldn't create file %s in %s, error: %s (%zd)\n", name, fid->path, strerror(rc), rc);
			break;
		}
	}
	nentries += i;

	return nentries;
}

ssize_t p9l_createtree(struct p9_handle *p9_handle, char *path, int depth, int dwidth, int fwidth) {
	ssize_t rc;
	struct p9_fid *fid = NULL;
	int n = 0;

	rc = p9l_mkdir(p9_handle->cwd, path, 0);
	if (rc && rc != EEXIST) {
		INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "couldn't create base directory %s in %s, error %s (%zd)", path, p9_handle->cwd->path, strerror(rc), rc);
		return -rc;
	} else {
		n = 1;
	}

	rc = p9l_rootwalk(p9_handle, path, &fid, 0);
	if (rc) {
		INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "couldn't walk to base directory %s in %s, error %s (%zd)", path, p9_handle->cwd->path, strerror(rc), rc);
		return -rc;
	}

	rc = p9l_createtree_fid(fid, depth, dwidth, fwidth);

	p9l_clunk(&fid);

	/* add 1 if we created the base dir */
	return (rc < 0 ? rc : rc + n);
}


/* rm -rf */

struct nlist {
	char name[MAXNAMLEN];
	struct p9_fid *pfid;
	struct nlist *next;
};

struct cb_arg {
	int debug;
	ssize_t count;
	bucket_t *buck;
	struct nlist *tail;
};

static int rd_cb(void *arg, struct p9_handle *p9_handle, struct p9_fid *dfid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	struct cb_arg *cb_arg = arg;
	struct nlist *n;

	/* skip . and .. */
	if (strncmp(name, ".", namelen) == 0 || strncmp(name, "..", namelen) == 0)
		return 0;

	if (cb_arg->debug)
		printf("%.*s\n", namelen, name);

	if (qid->type == P9_QTDIR) {
		n = bucket_get(cb_arg->buck);
		if (n == NULL)
			return ENOMEM;

		strncpy(n->name, name, MIN(MAXNAMLEN, namelen));
		if (namelen < MAXNAMLEN) {
			n->name[namelen] = '\0';
		} else {
			n->name[MAXNAMLEN-1] = '\0';
		}
		n->pfid = dfid;

		n->next = cb_arg->tail;
		cb_arg->tail = n;
	} else {
		p9p_unlinkat(p9_handle, dfid, name, 0);
		cb_arg->count++;
	}

	return 0;
}


ssize_t p9l_rmrf(struct p9_handle *p9_handle, char *path) {
	struct p9_fid *fid;
	int rc;
	uint64_t offset;
	bucket_t *buck;
	struct cb_arg cb_arg;
	struct nlist *nlist;

	rc = p9l_rootwalk(p9_handle, path, &fid, 0);
	if (rc) {
		INFO_LOG(p9_handle->debug & P9_DEBUG_LIBC, "couldn't walk to base directory %s in %s, error %s (%d)", path, p9_handle->cwd->path, strerror(rc), rc);
		return rc;
	}

	buck = bucket_init(100, sizeof(struct nlist));
	cb_arg.buck = buck;
	cb_arg.count = 0;
	cb_arg.debug = p9_handle->debug & 0x100;
	cb_arg.tail = bucket_get(buck);
	strncpy(cb_arg.tail->name, path, MAXNAMLEN);
	cb_arg.tail->next = NULL;
	cb_arg.tail->pfid = p9_handle->cwd;

	while (rc == 0 && cb_arg.tail != NULL) {
		if (cb_arg.tail->name[0] == '\0') {
			fid = cb_arg.tail->pfid;
		} else {
			rc = p9l_walk(p9_handle, cb_arg.tail->pfid, cb_arg.tail->name, &fid, AT_SYMLINK_NOFOLLOW);
			if (rc) {
				printf("walk failed, rc: %s (%d)\n", strerror(rc), rc);
				break;
			}

			if (fid->qid.type != P9_QTDIR) {
				printf("%s not a directory?", fid->path);
				break;
			}

			rc = p9p_lopen(p9_handle, fid, O_RDONLY, NULL);
			if (rc) {
				printf("open failed, rc: %s (%d)\n", strerror(rc), rc);
				break;
			}

			cb_arg.tail->name[0] = '\0';
			cb_arg.tail->pfid = fid;
		}

		offset = 0LL;
		rc = p9p_readdir(p9_handle, fid, &offset, rd_cb, &cb_arg);

		if (rc == 2) {
			rc = 0;
			p9l_rm(p9_handle, fid->path);
			cb_arg.count++;
			p9p_clunk(p9_handle, &fid);
			nlist = cb_arg.tail;
			cb_arg.tail = cb_arg.tail->next;
			bucket_put(buck, (void **)&nlist);
		} else if (rc > 0) {
			rc = 0;
		} else if (rc < 0) {
			rc = -rc;
			printf("readdir failed, rc: %s (%d)\n", strerror(rc), rc);
			break;
		} else {
			printf("readdir returned %d, not possible!\n", rc);
			rc = EFAULT;
		}
	}

	return (rc ? -rc : cb_arg.count);
}
