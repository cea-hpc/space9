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

%module(docstring="Space9 userspace 9P library. Everyting starts with a p9_handle('conffile').") space9
%{
#include "9p_internals.h"


struct fid {
	struct p9_fid *ptr;
	PyObject *handle_obj;
};

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
/* from /usr/include/fcntl.h */
# define AT_SYMLINK_NOFOLLOW	0x100	/* Do not follow symbolic links.  */
/* from /usr/include/attr/xattr.h */
#define XATTR_CREATE  0x1       /* set value, fail if attr already exists */
#define XATTR_REPLACE 0x2       /* set value, fail if attr does not exist */


%typemap(out) struct fid * %{
	$result = SWIG_NewPointerObj(SWIG_as_voidptr($1), SWIGTYPE_p_fid, SWIG_POINTER_OWN |  0 );
	$1->handle_obj = self;
	Py_INCREF(self);
%}
%typemap(out) struct fid *fid %{
	$result = SWIG_NewPointerObj(SWIG_as_voidptr($1), SWIGTYPE_p_fid, SWIG_BUILTIN_INIT |  0 );
	$1->handle_obj = obj1;
	Py_INCREF(obj1);
%}
struct fid {
	struct p9_fid *ptr;
	PyObject *handle_obj;
};

/* dummy p9_handle struct (it's in internals, not in space9.h - this lets us redefine "debug" and "umask" as methods) */
struct p9_handle {
	msk_trans_t *trans;
};

%exception {
	errno = 0;
	$action
	if (errno) {
		PyErr_SetFromErrno(PyExc_IOError);
		SWIG_fail;
	}
}

%newobject p9_handle::open;
%newobject p9_handle::walk;

%inline %{
int ls_cb(void *arg, struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	PyObject *list = arg;
	PyObject *str = PyString_FromStringAndSize(name, namelen);
	PyList_Append(list, str);
	Py_DECREF(str); /* one ref from PyString_FromString and one from PyList_Append, we only really keep the later */

	return 0;
}
%}


%feature("autodoc", "p9_handle is the filesystem handle. Use it to open a file, list directories, etc.
Most fd-operations can be done on fids once you have one (walk or open)") p9_handle;
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
	struct fid *walk(char *path, int flags = 0) {
		struct p9_fid *fid;
		if ((errno = p9l_walk($self, path[0] == '/' ? $self->root_fid : $self->cwd, path, &fid, flags)))
			return NULL;

		struct fid *wrap = malloc(sizeof(struct fid));
		wrap->ptr = fid;
		return wrap;
	}
	struct fid *open(char *path, uint32_t flags, uint32_t mode) {
		struct p9_fid *fid;
		if ((errno = p9l_open($self, &fid, path, flags, mode, 0)))
			return NULL;

		struct fid *wrap = malloc(sizeof(struct fid));
		wrap->ptr = fid;
		return wrap;
	}
	void fsync(struct p9_fid *fid) {
		errno = p9l_fsync(fid);
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
		errno = p9l_fchown(fid, uid, gid);
	}
	void chmod(char *path, uint32_t mode) {
		errno = p9l_chmod($self, path, mode);
	}
	void fchmod(struct p9_fid *fid, uint32_t mode) {
		errno = p9l_fchmod(fid, mode);
	}
	PyObject *stat(char *path) {
		struct p9_getattr attr;
		PyObject *ret = NULL;
		attr.valid = P9_GETATTR_BASIC;
		errno = p9l_stat($self, path, &attr);
		if (!errno) {
			ret = Py_BuildValue("{sisisisisisisisisisisisi}",
				"mode", attr.mode, "ino", attr.ino, "nlink", attr.nlink, "uid", attr.uid,
				"gid", attr.gid, "size", attr.size, "blksize", attr.blksize,
				"blocks", attr.blkcount, "atime", attr.atime_sec, "mtime", attr.mtime_sec,
				"ctime", attr.ctime_sec, "rdev", attr.rdev);
				/* closer to real stat_result struct, but useless if the object isn't created
				ret = Py_BuildValue("(iiiiiiiiii)", attr.mode, attr.ino, 0, attr.nlink, attr.uid, attr.gid, attr.size, attr.atime_sec, attr.mtime_sec, attr.ctime_sec); */
		}
		return ret;
	}
	PyObject *lstat(char *path) {
		struct p9_getattr attr;
		PyObject *ret = NULL;
		attr.valid = P9_GETATTR_BASIC;
		errno = p9l_lstat($self, path, &attr);
		if (!errno) {
			ret = Py_BuildValue("{sisisisisisisisisisisisi}",
				"mode", attr.mode, "ino", attr.ino, "nlink", attr.nlink, "uid", attr.uid,
				"gid", attr.gid, "size", attr.size, "blksize", attr.blksize,
				"blocks", attr.blkcount, "atime", attr.atime_sec, "mtime", attr.mtime_sec,
				"ctime", attr.ctime_sec, "rdev", attr.rdev);
		}
		return ret;
	}
	PyObject *fstat(struct p9_fid *fid) {
		struct p9_getattr attr;
		PyObject *ret = NULL;
		attr.valid = P9_GETATTR_BASIC;
		errno = p9l_fstat(fid, &attr);
		if (!errno) {
			ret = Py_BuildValue("{sisisisisisisisisisisisi}",
				"mode", attr.mode, "ino", attr.ino, "nlink", attr.nlink, "uid", attr.uid,
				"gid", attr.gid, "size", attr.size, "blksize", attr.blksize,
				"blocks", attr.blkcount, "atime", attr.atime_sec, "mtime", attr.mtime_sec,
				"ctime", attr.ctime_sec, "rdev", attr.rdev);
		}
		return ret;
	}
	void fseek(struct p9_fid *fid, int64_t offset, int whence) {
		errno = p9l_fseek(fid, offset, whence);
	}
	PyObject *xattrget(char *path, char *field, size_t count) {
		ssize_t rc;
		char *buf = malloc(count);
		PyObject *pystr = NULL;

		rc = p9l_xattrget($self, path, field, buf, count);
		if (rc >= 0) {
			pystr = PyString_FromStringAndSize(buf, MIN(count, rc));
		} else {
			errno = -rc;
		}

		return pystr;
	}
	size_t xattrset(char *path, char *field, char *buf, int flags = 0) {
		ssize_t rc;
		rc = p9l_xattrset($self, path, field, buf, strlen(buf), flags);
		if (rc < 0) {
			errno = -rc;
		}
		return rc;
	}
	PyObject *fxattrget(struct p9_fid *fid, char *field, size_t count) {
		ssize_t rc;
		char *buf = malloc(count);
		PyObject *pystr = NULL;

		rc = p9l_fxattrget(fid, field, buf, count);
		if (rc >= 0) {
			if ((field == NULL || field[0]=='\0') && rc > 0)
				rc--;
			pystr = PyString_FromStringAndSize(buf, MIN(count, rc));
		} else {
			errno = -rc;
		}

		return pystr;
	}
	size_t fxattrset(struct p9_fid *fid, char *field, char *buf, int flags = 0) {
		ssize_t rc;
		rc = p9l_fxattrset(fid, field, buf, strlen(buf), flags);
		if (rc < 0) {
			errno = -rc;
		}
		return rc;
	}
	PyObject *read(struct p9_fid *fid, size_t count) {
		int rc;
		PyObject *pystr = NULL;
		char *zbuf;
		msk_data_t *data;

		rc = p9pz_read($self, fid, &zbuf, count, fid->offset, &data);

		if (rc >= 0) {
			pystr = PyString_FromStringAndSize(zbuf, MIN(count, rc));
			p9c_putreply($self, data);
			fid->offset += rc;
		} else {
			errno = -rc;
		}
		return pystr;
	}
	int write(struct p9_fid *fid, char *buf, size_t count) {
		int rc;
		rc = p9l_write(fid, buf, count);
		if (rc < 0) {
			errno = -rc;
		}
		return rc;
	}
};


%feature("autodoc", "file handle. Use to read/write, etc.
p9_fid(p9_handle, path[, flags, mode])

On creation it's either not opened (if so, open later) or if open flags are set it's opened directly.
flags can be O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND") fid;
%extend fid {
	fid(struct p9_handle *p9_handle, char *path, uint32_t flags = 0, uint32_t mode = 0) {
		struct p9_fid *fid;
		if (flags == 0) {
			errno = p9l_walk(p9_handle, path[0] == '/' ? p9_handle->root_fid : p9_handle->cwd, path, &fid, flags);
		} else {
			errno = p9l_open(p9_handle, &fid, path, flags, mode, 0);
		}
		if (errno)
			return NULL;

		struct fid *wrap = malloc(sizeof(struct fid));
		wrap->ptr = fid;
		return wrap;
	}
	~fid() {
		if ($self->ptr != NULL) {
			errno = p9p_clunk($self->ptr->p9_handle, &$self->ptr);
			Py_DECREF($self->handle_obj);
		}
		free($self);
	}
%exception {
	errno = 0;
	if (arg1->ptr == NULL) {
		PyErr_SetString(PyExc_IOError, "invalid fid");
		SWIG_fail;
	}
	$action
	if (errno) {
		PyErr_SetFromErrno(PyExc_IOError);
		SWIG_fail;
	}
}
	void clunk() {
		errno = p9p_clunk($self->ptr->p9_handle, &$self->ptr);

		/* it's possible clunk failed before sending the message, if so don't decref */
		if ($self->ptr == NULL)
			Py_DECREF($self->handle_obj);
	}
	void unlink() {
		p9l_rm($self->ptr->p9_handle, $self->ptr->path);
	}
       void open(uint32_t flags) {
               if ($self->ptr->openflags == 0)
                       errno = p9p_lopen($self->ptr->p9_handle, $self->ptr, flags, NULL);
	}
	void chown(uint32_t uid, uint32_t gid) {
		errno = p9l_fchown($self->ptr, uid, gid);
	}
	void chmod(uint32_t mode) {
		errno = p9l_fchmod($self->ptr, mode);
	}
	void fsync() {
		errno = p9l_fsync($self->ptr);
	}
	PyObject *stat() {
		struct p9_getattr attr;
		PyObject *ret = NULL;
		attr.valid = P9_GETATTR_BASIC;
		errno = p9l_fstat($self->ptr, &attr);
		if (!errno) {
			ret = Py_BuildValue("{sisisisisisisisisisisisi}",
				"mode", attr.mode, "ino", attr.ino, "nlink", attr.nlink, "uid", attr.uid,
				"gid", attr.gid, "size", attr.size, "blksize", attr.blksize,
				"blocks", attr.blkcount, "atime", attr.atime_sec, "mtime", attr.mtime_sec,
				"ctime", attr.ctime_sec, "rdev", attr.rdev);
		}
		return ret;
	}
%feature("autodoc", "seek(p9_fid self, int64_t offset, int whence)
whence is one of SEEK_SET, SEEK_CUR, SEEK_END") p9_fid::seek;
	void seek(int64_t offset, int whence) {
		errno = p9l_fseek($self->ptr, offset, whence);
	}
	PyObject *read(size_t count) {
		int rc;
		PyObject *pystr = NULL;
		char *zbuf;
		msk_data_t *data;

		rc = p9pz_read($self->ptr->p9_handle, $self->ptr, &zbuf, count, $self->ptr->offset, &data);

		if (rc >= 0) {
			pystr = PyString_FromStringAndSize(zbuf, MIN(count, rc));
			p9c_putreply($self->ptr->p9_handle, data);
			$self->ptr->offset += rc;
		} else {
			errno = -rc;
		}
		return pystr;
	}
	size_t write(char *buf, size_t count) {
		ssize_t rc;
		rc = p9l_write($self->ptr, buf, count);
		if (rc < 0) {
			errno = -rc;
		}
		return rc;
	}
	struct p9_fid *walk(char *path, int flags = 0) {
		struct p9_fid *fid;
		if ((errno = p9l_walk($self->ptr->p9_handle, $self->ptr, path, &fid, flags)))
			return NULL;

		return fid;
	}
	PyObject *xattrget(char *field, size_t count) {
		ssize_t rc;
		char *buf = malloc(count);
		PyObject *pystr = NULL;

		rc = p9l_fxattrget($self->ptr, field, buf, count);
		if (rc >= 0) {
			if ((field == NULL || field[0]=='\0') && rc > 0)
				rc--;
			pystr = PyString_FromStringAndSize(buf, MIN(count, rc));
		} else {
			errno = -rc;
		}

		return pystr;
	}
	size_t xattrset(char *field, char *buf, int flags = 0) {
		ssize_t rc;
		rc = p9l_fxattrset($self->ptr, field, buf, strlen(buf), flags);
		if (rc < 0) {
			errno = -rc;
		}
		return rc;
	}
	char *path() {
		return $self->ptr->path;
	}
	uint64_t offset() {
		return $self->ptr->offset;
	}
	int openflags() {
		return $self->ptr->openflags;
	}
}
