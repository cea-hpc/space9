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
%}
%feature("autodoc", "1");

%include "../include/space9.h"

%extend p9_handle {
	p9_handle(char *conf) {
		struct p9_handle *handle;
		p9_init(&handle, conf);
		return handle;
	}
	~p9_handle() {
		p9_destroy(&$self);
	}
	int setdebug(int dbg) {
		int t = $self->debug;
		$self->debug = dbg;
		return t;
	}
	uint32_t setumask(uint32_t mask) {
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
	int ls(char *path) {
		return p9s_ls($self, path);
	}
};
