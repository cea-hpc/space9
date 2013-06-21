%module space9
%{
#include <mooshika.h>
#include "9p.h"
%}
%feature("autodoc", "1");

%include "../include/9p.h"

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
