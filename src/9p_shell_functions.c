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

static int ls_callback(void *arg, struct p9_handle *p9_handle, struct p9_fid *fid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	char filetype;
	if (qid->type == P9_QTDIR)
		filetype='/';
	else if (qid->type == P9_QTSYMLINK)
		filetype='@';
	else
		filetype='\0';

	if (filetype)
		printf("%.*s%c\n", namelen, name, filetype);
	else
		printf("%.*s\n", namelen, name);

	return 0;
}

static int ll_callback(void *arg, struct p9_handle *p9_handle, struct p9_fid *dfid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	int rc;
	struct p9_getattr attr;
	char filetype;
	char *target;
	msk_data_t *data = NULL;
	struct p9_fid *fid;

	if (arg)
		fid = arg;
	else {
		rc = p9p_walk(p9_handle, dfid, name, &fid);
		if (rc) {
			printf("couldn't walk to '%s' in '%s', error: %s (%d)", name, dfid->path, strerror(rc), rc);
		}
	}

	if (fid) {
		attr.valid = P9_GETATTR_BASIC;
		rc = p9p_getattr(p9_handle, fid, &attr);
		if (rc) {
			printf("couldn't getattr '%s', error: %s (%d)\n", fid->path, strerror(rc), rc);
		} else if (qid->type == P9_QTDIR) {
			filetype='/';
			printf("%#o %"PRIu64" %d %d %"PRIu64" %"PRIu64" %s%c\n", attr.mode, attr.nlink, attr.uid, attr.gid, attr.size, attr.mtime_sec, name, filetype);
		} else if (qid->type == P9_QTSYMLINK) {
			filetype='@';
			rc = p9p_lopen(p9_handle, fid, O_RDONLY, NULL);
			if (rc) {
				target = "couldn't open";
			} else {
				rc = p9pz_readlink(p9_handle, fid, &target, &data);
				if (rc < 0) {
					target = "couldn't readlink";
				} else {
					rc = 0;
				}
			}
			printf("%#o %"PRIu64" %d %d %"PRIu64" %"PRIu64" %s -> %s\n", attr.mode, attr.nlink, attr.uid, attr.gid, attr.size, attr.mtime_sec, name, target);
			if (data)
				p9c_putreply(p9_handle, data);
		} else if (attr.mode & S_IXUSR) {
			filetype='*';
			printf("%#o %"PRIu64" %d %d %"PRIu64" %"PRIu64" %s%c\n", attr.mode, attr.nlink, attr.uid, attr.gid, attr.size, attr.mtime_sec, name, filetype);
		} else {
			filetype=' ';
			printf("%#o %"PRIu64" %d %d %"PRIu64" %"PRIu64" %s\n", attr.mode, attr.nlink, attr.uid, attr.gid, attr.size, attr.mtime_sec, name);
		}

		if (!arg)
			p9p_clunk(p9_handle, &fid);
	}
	return rc;
}

int p9s_ls(struct p9_handle *p9_handle, char *arg) {
	int rc = 0;
	struct p9_fid *fid;
	p9p_readdir_cb cb = ls_callback;

	if (strncmp(arg, "-l", 2) == 0) {
		if (arg[2] == ' ') {
			cb = ll_callback;
			arg += 3;
		} else if (arg[2] == '\0') {
			cb = ll_callback;
			arg += 2;
		}
	}	

	rc = p9l_ls(p9_handle, arg, cb, NULL);
	if (rc == -ENOTDIR) {
		rc = p9l_open(p9_handle, arg, &fid, 0, 0, 0);
		if (!rc) {
			cb(fid, p9_handle, fid, &fid->qid, 0, strlen(arg), arg);
			p9p_clunk(p9_handle, &fid);
		}
	} else if (rc < 0) {
		printf("ls failed: %s (%d)\n", strerror(-rc), -rc);
	} else {
		printf("total: %u entries\n", rc);
	}

	return rc;
}

int p9s_cd(struct p9_handle *p9_handle, char *arg) {
	int rc;
	rc = p9l_cd(p9_handle, arg);
	if (rc)
		printf("cd to %s failed, error: %s (%d)\n", arg, strerror(rc), rc);

	return rc;
}

int p9s_ln(struct p9_handle *p9_handle, char *arg) {
	int rc;
	int symlink = 0;
	char *dst;

	if (strncmp(arg, "-s", 2) == 0) {
		if (arg[2] == ' ') {
			arg += 3;
			symlink = 1;
		} else if (arg[2] == '\0') {
			printf("need a target and destination?\n");
			return EINVAL;
		}
	}

	dst = strchr(arg, ' ');
	if (dst == NULL || dst[1] == '\0') {
		if (arg == dst) {
			printf("need a target and destination?\n");
			return EINVAL;
		}
		if (dst)
			dst[0] = '\0';

		path_basename(arg, &dst);
	} else {
		dst[0] = '\0';
		dst++;
	}

	if (symlink) {
		rc = p9l_symlink(p9_handle, arg, dst);
		if (rc)
			printf("symlink %s %s failed, error: %s (%d)\n", arg, dst, strerror(rc), rc);
	} else {
		rc = p9l_link(p9_handle, arg, dst);
		if (rc)
			printf("link %s %s failed, error: %s (%d)\n", arg, dst, strerror(rc), rc);
	}

	return rc;
}

int p9s_cat(struct p9_handle *p9_handle, char *arg) {
	int rc, tmp, n;
	struct p9_fid *fid;
	char buf[10240];
	uint64_t offset;

	if (strchr(arg, '/') != NULL) {
		printf("Not yet implemented with full path\n");
		return EINVAL;
	}

	rc = p9l_open(p9_handle, arg, &fid, O_RDONLY, 0, 0);
	if (rc) {
		printf("open %s failed, error: %s (%d)\n", arg, strerror(rc), rc);
		return rc;
	}

	offset = 0LL;
	do {
		rc = p9p_read(p9_handle, fid, buf, 10240, offset);
		if (rc > 0) {
			n = 0;
			while (n < rc) {
				tmp = write(1, buf, rc-n);
				if (tmp <= 0)
					break;
				n += tmp;
			}
			offset += rc;
		}
	} while (rc > 0);

	tmp = p9p_clunk(p9_handle, &fid);
	if (tmp) {
		printf("clunk failed on fid %u (%s), error: %s (%d)\n", fid->fid, fid->path, strerror(tmp), tmp);
	}

	return rc;
}
int p9s_mkdir(struct p9_handle *p9_handle, char *arg) {
	int rc;
	rc = p9l_mkdir(p9_handle, arg, 0666);
	if (rc)
		printf("mkdir %s failed, error: %s (%d)\n", arg, strerror(rc), rc);

	return rc;
}

int p9s_pwd(struct p9_handle *p9_handle, char *arg) {
	printf("%s\n", p9_handle->cwd->path);
	return 0;
}

int p9s_xwrite(struct p9_handle *p9_handle, char *arg) {
	int rc, tmp;
	struct p9_fid *fid;
	char *filename;
	char *buf = NULL;
	uint32_t count;

	fid = NULL;
	filename = arg;
	arg = strchr(filename, ' ');
	if (arg == NULL) {
		printf("nothing to write, creating empty file or emptying it if it exists\n");
	} else {
		arg[0] = '\0';
		arg++;
		count = strlen(arg) + 1;
		buf = malloc(count+1);
		if (!buf) {
			printf("couldn't allocate copy buffer\n");
			return ENOMEM;
		}
		strncpy(buf, arg, count);
		buf[count-1]='\n';
		buf[count]='\0';
	}

	rc = p9l_open(p9_handle, filename, &fid, O_WRONLY|O_CREAT|O_TRUNC, 0666, 0);
	if (rc) {
		printf("open failed on %s, error: %s (%d)\n", filename, strerror(rc), rc);
		return rc;
	}

	if (buf) {
		rc = p9l_write(fid, buf, count);
		if (rc < 0) {
			printf("write failed on file %s, error: %s (%d)\n", fid->path, strerror(-rc), -rc);
		}
		printf("wrote %d bytes\n", rc);
		free(buf);
	}

	tmp = p9p_clunk(p9_handle, &fid);
	if (tmp) {
		printf("clunk failed on fid %u (%s), error: %s (%d)\n", fid->fid, fid->path, strerror(tmp), tmp);
	}	
	return rc;
}

int p9s_rm(struct p9_handle *p9_handle, char *arg) {
	int rc;
	rc = p9l_rm(p9_handle, arg);
	if (rc)
		printf("rm %s failed, error: %s (%d)\n", arg, strerror(rc), rc);

	return rc;
}
int p9s_mv(struct p9_handle *p9_handle, char *arg) {
	char *dest;

	dest = strchr(arg, ' ');
	if (!dest) {
		printf("no dest?");
		return EINVAL;
	}

	dest[0]='\0';
	dest++;

	return p9l_mv(p9_handle, arg, dest);
}

