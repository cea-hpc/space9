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

%module space9
%{
#include "9p_internals.h"
int ls_cb(void *arg, struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name);
%}
%feature("autodoc", "1");

%include "stdint.i"
%include "space9.h"

/* useful constants */
/* from /usr/include/linux/fs.h */
#define SEEK_SET	0	/* seek relative to beginning of file */
#define SEEK_CUR	1	/* seek relative to current file position */
#define SEEK_END	2	/* seek relative to end of file */
/* from /usr/include/bits/fcntl.h */
#define O_RDONLY             00
#define O_WRONLY             01
#define O_RDWR               02
#define O_CREAT            0100 /* not fcntl */
#define O_TRUNC           01000 /* not fcntl */
#define O_APPEND          02000


/* dummy p9_handle struct (it's in internals, not in space9.h - this lets us redefine "debug" and "umask" as methods) */
struct p9_handle {
	msk_trans_t *trans;
};
struct p9_fid {
	int open;
};

%inline %{
int ls_cb(void *arg, struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	PyObject *list = arg;
	PyObject *str = PyString_FromStringAndSize(name, namelen);
	PyList_Append(list, str);
	Py_DECREF(str); /* one ref from PyString_FromString and one from PyList_Append, we only really keep the later */

	return 0;
}
%}

%exception {
	errno = 0;
	$action
	if (errno) {
		PyErr_SetFromErrno(PyExc_IOError);
		SWIG_fail;
	}
}

%extend p9_handle {
	p9_handle(char *conf) {
		struct p9_handle *handle;
		errno = p9_init(&handle, conf);
		return handle;
	}
	~p9_handle() {
		p9_destroy(&$self);
	}
	int debug(int dbg) {
		int t = $self->debug;
		$self->debug = dbg;
		return t;
	}
	int full_debug(int dbg) {
		int t = $self->full_debug;
		$self->full_debug = dbg;
		return t;
	}
	uint32_t umask(uint32_t mask) {
		return p9l_umask($self, mask);
	}
	char *pwd() {
		return $self->cwd->path;
	}
	void cd(char *path) {
		errno = p9l_cd($self, path);
	}
	struct p9_fid *open(char *path, uint32_t mode, uint32_t flags) {
		struct p9_fid *fid;
		if ((errno = p9l_open($self, &fid, path, mode, flags, 0)))
			return NULL;

		return fid;
	}
	void close(struct p9_fid *fid) {
		errno = p9p_clunk($self, fid);
	}
	void fsync(struct p9_fid *fid) {
		errno = p9p_fsync($self, fid);
	}
	PyObject *ls(char *path = "") {
		PyObject *list = PyList_New(0);
		errno = p9l_ls($self, path, ls_cb, list);
		if (errno < 0) {
			errno = -errno;
			return NULL;
		}
		errno = 0;
		return list;
	}
	void mv(char *src, char *dst) {
		errno = p9l_mv($self, src, dst);
	}
	void rm(char *path) {
		errno = p9l_rm($self, path);
	}
	void mkdir(char *path, uint32_t mode) {
		errno = p9l_mkdir($self, path, mode);
	}
	void link(char *target, char *linkname) {
		errno = p9l_link($self, target, linkname);
	}
	void symlink(char *target, char *linkname) {
		errno = p9l_symlink($self, target, linkname);
	}
	void chown(char *path, uint32_t uid, uint32_t gid) {
		errno = p9l_chown($self, path, uid, gid);
	}
	void fchown(struct p9_fid *fid, uint32_t uid, uint32_t gid) {
		errno = p9l_fchown($self, fid, uid, gid);
	}
	void chmod(char *path, uint32_t mode) {
		errno = p9l_chmod($self, path, mode);
	}
	void fchmod(struct p9_fid *fid, uint32_t mode) {
		errno = p9l_fchmod($self, fid, mode);
	}
	void fseek(struct p9_fid *fid, int64_t offset, int whence) {
		errno = p9l_fseek($self, fid, offset, whence);
	}
	PyObject *read(struct p9_fid *fid, size_t count) {
		int rc;
		PyObject *pystr = NULL;
		char *buf = malloc(count);
		rc = p9l_read($self, fid, buf, count);
		if (rc < 0) {
			errno = -rc;
		} else {
			pystr = PyString_FromStringAndSize(buf, rc);
		}
		free(buf);
		return pystr;
	}
	int write(struct p9_fid *fid, char *buf, size_t count) {
		int rc;
		rc = p9l_write($self, fid, buf, count);
		if (rc < 0) {
			errno = -rc;
		}
		return rc;
	}
};
