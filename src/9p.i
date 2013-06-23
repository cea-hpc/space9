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

%include "space9.h"

/* dummy p9_handle struct (it's in internals, not in space9.h - this lets us redefine "debug" and "umask" as methods) */
struct p9_handle {
	msk_trans_t *trans;
};

%inline %{
int ls_cb(void *arg, struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
   PyObject *list = arg;
	PyObject *str = PyString_FromString(name);
	PyList_Append(list, str);
	Py_DECREF(str); /* one ref from PyString_FromString and one from PyList_Append, we only really keep the later */

	return 0;
}
%}

%extend p9_handle {
	p9_handle(char *conf) {
		struct p9_handle *handle;
		p9_init(&handle, conf);
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
	uint32_t umask(uint32_t mask) {
		return p9l_umask($self, mask);
	}
	char *pwd() {
		return $self->cwd->path;
	}
	int cd(char *path) {
		return p9l_cd($self, path);
	}
	struct p9_fid *open(char *path, uint32_t mode, uint32_t flags) {
		struct p9_fid *fid;
		if (p9l_open($self, &fid, path, mode, flags, 0))
			return NULL;
		return fid;
	}
	PyObject *ls(char *path) {
		PyObject *list = PyList_New(0);
		p9l_ls($self, path, ls_cb, list);
		return list;
	}
	int mv(char *src, char *dst) {
		return p9l_mv($self, src, dst);
	}
	int rm(char *path) {
		return p9l_rm($self, path);
	}
	int mkdir(char *path, uint32_t mode) {
		return p9l_mkdir($self, path, mode);
	}
	int link(char *target, char *linkname) {
		return p9l_link($self, target, linkname);
	}
	int symlink(char *target, char *linkname) {
		return p9l_symlink($self, target, linkname);
	}
	int chown(char *path, uint32_t uid, uint32_t gid) {
		return p9l_chown($self, path, uid, gid);
	}
	int fchown(struct p9_fid *fid, uint32_t uid, uint32_t gid) {
		return p9l_fchown($self, fid, uid, gid);
	}
	int chmod(char *path, uint32_t mode) {
		return p9l_chmod($self, path, mode);
	}
	int fchmod(struct p9_fid *fid, uint32_t mode) {
		return p9l_fchmod($self, fid, mode);
	}
	int read(struct p9_fid *fid, char *buf, size_t count, uint64_t offset) {
		return p9l_read($self, fid, buf, count, offset);
	}
	int write(struct p9_fid *fid, char *buf, size_t count, uint64_t offset) {
		return p9l_write($self, fid, buf, count, offset);
	}
};
