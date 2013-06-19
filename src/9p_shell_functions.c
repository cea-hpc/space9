#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <mooshika.h>
#include "9p.h"
#include "utils.h"
#include "9p_proto.h"
#include "9p_shell_functions.h"

static int ls_callback(void *arg, struct p9_fid *fid, struct p9_qid *qid, uint8_t type, uint16_t namelen, char *name) {
	printf("%*s\n", namelen, name);
	return 0;
}

int p9s_ls(struct current_context *ctx, char *args) {
	int rc = 0;
	uint64_t offset = 0LL;
	uint32_t count, total = 0;

	do {
		count = p9p_readdir(ctx->p9_handle, ctx->cwd, &offset, ls_callback, NULL);
		if (count > 0)
			total += count;
	} while (count > 0);

	if (count < 0) {
		rc = -count;
		printf("readdir failed on fid %u (%s): %s (%d)\n", ctx->cwd->fid, ctx->cwd->path, strerror(rc), rc);
	}
	printf("total: %u entries\n", total);
	return rc;
}

int p9s_cd(struct current_context *ctx, char *arg) {
	int rc;
	char *cur, *end;
	struct p9_fid *fid;

	if (arg[0] == '/') {
		rc = p9p_clunk(ctx->p9_handle, ctx->cwd);
		if (rc) {
			printf("clunk failed on fid %u (%s), error: %s (%d)\n", ctx->cwd->fid, ctx->cwd->path, strerror(rc), rc);
		}

		ctx->cwd = ctx->p9_handle->root_fid;
	}

	cur = arg;
	while (cur) {
		end = strchr(cur, '/');
		if (end) 
			end[0] = '\0';
		rc = p9p_walk(ctx->p9_handle, ctx->cwd, cur, &fid);
		if (rc) {
			printf("walk failed from %s to %s, error: %s (%d)\n", ctx->cwd->path, arg, strerror(rc), rc);
			break;
		}
		if (fid->qid.type != P9_QTDIR) {
			printf("%s is not a directory (qid.type = %u)!\n", fid->path, fid->qid.type);
			p9p_clunk(ctx->p9_handle, fid);
			if (rc) {
				printf("clunk failed on %s, error: %s (%d)\n", fid->path, strerror(rc), rc);
			}
			break;
		}
		if (ctx->cwd != ctx->p9_handle->root_fid) {
			rc = p9p_clunk(ctx->p9_handle, ctx->cwd);
			if (rc) {
				printf("clunk failed on %s, error: %s (%d)\n", ctx->cwd->path, strerror(rc), rc);
			}
		}
		ctx->cwd = fid;

		if (end)
			cur = end+1;
		else
			cur = NULL;
	}

	return rc;
}

int p9s_cat(struct current_context *ctx, char *arg) {
	int rc, tmp, n;
	struct p9_fid *fid;
	char buf[10240];
	uint64_t offset;

	if (strchr(arg, '/') != NULL) {
		printf("Not yet implemented with full path\n");
		return EINVAL;
	}

	rc = p9p_walk(ctx->p9_handle, ctx->cwd, arg, &fid);
	if (rc) {
		printf("walk to %s failed, error: %s (%d)\n", arg, strerror(rc), rc);
		return rc;
	}

	offset = 0LL;
	do {
		rc = p9p_read(ctx->p9_handle, fid, offset, 10240, buf);
		if (rc > 0) {
			n = 0;
			while (n < rc) {
				tmp = write(1, buf, rc);
				if (tmp <= 0)
					break;
				n += tmp;
			}
			offset += rc;
		}
	} while (rc > 0);

	tmp = p9p_clunk(ctx->p9_handle, fid);
	if (tmp) {
		printf("clunk failed on fid %u (%s), error: %s (%d)\n", fid->fid, fid->path, strerror(tmp), tmp);
	}

	return rc;
}
int p9s_mkdir(struct current_context *ctx, char *arg) {
	int rc, clunk, tmp;
	char str[MAXPATHLEN];
	struct p9_fid *fid;

	path_canonicalizer(arg);

	path_dirname(arg, str, MAXPATHLEN);

	clunk = 1;
	if (arg[0] == '/') {
		rc = p9p_walk(ctx->p9_handle, ctx->p9_handle->root_fid, str, &fid);
		if (rc) {
			printf("walk to %s failed, error: %s (%d)\n", str, strerror(rc), rc);
		}
	} else if (strncmp(str, ".", 2)) {
		/* not current directory */
		rc = p9p_walk(ctx->p9_handle, ctx->cwd, str, &fid);
		if (rc) {
			printf("walk from %s to %s failed, error: %s (%d)\n", ctx->cwd->path, str, strerror(rc), rc);
		}
	} else {
		fid = ctx->cwd;
		clunk = 0;
	}

	path_basename(arg, str, MAXPATHLEN);

	rc = p9p_mkdir(ctx->p9_handle, fid, str, 0644, getegid(), NULL);
	if (rc) {
		printf("mkdir failed on fid %u (%s) name %s, error: %s (%d)\n", fid->fid, fid->path, str, strerror(rc), rc);
	}

	if (clunk) {
		tmp = p9p_clunk(ctx->p9_handle, fid);
		if (tmp) {
			printf("clunk failed on fid %u (%s), error: %s (%d)\n", fid->fid, fid->path, strerror(tmp), tmp);
		}
	}

	return rc;
}

int p9s_pwd(struct current_context *ctx, char *arg) {
	printf("%s\n", ctx->cwd->path);
	return 0;
}

int p9s_xwrite(struct current_context *ctx, char *arg) {
	int rc, tmp;
	struct p9_fid *fid;
	char *filename;
	uint32_t count;
	struct p9p_setattr attr;

	fid = NULL;
	filename = arg;
	arg = strchr(filename, ' ');
	if (arg == NULL) {
		printf("nothing to write, creating empty file or emptying it if it exists\n");
	} else {
		arg[0] = '\0';
		arg++;
		count = strlen(arg) + 1;
		arg[count-1]='\n';
		arg[count]='\0';
	}

	rc = p9p_walk(ctx->p9_handle, ctx->cwd, filename, &fid);
	if (rc == ENOENT) {
		rc = p9p_walk(ctx->p9_handle, ctx->cwd, NULL, &fid);
		if (rc) {
			printf("walk failed to duplicate fid %u (%s), error: %s (%d)\n", ctx->cwd->fid, ctx->cwd->path, strerror(rc), rc);
			return rc;
		}
		rc = p9p_lcreate(ctx->p9_handle, fid, filename, O_WRONLY, 0640, getegid(), NULL);
		if (rc) {
			printf("lcreate failed on dir %s, filename %s, error: %s (%d)\n", ctx->cwd->path, filename, strerror(rc), rc);
			return rc;
		}
	} else if (rc) {
		printf("walk failed from %s to %s, error: %s (%d)\n", ctx->cwd->path, filename, strerror(rc), rc);
		return rc;
	} else {
		rc = p9p_lopen(ctx->p9_handle, fid, O_WRONLY | O_TRUNC, NULL);
		if (rc) {
			printf("lopen failed on file %s, error: %s (%d)\n", fid->path, strerror(rc), rc);
			return rc;
		}
	}

	memset(&attr, 0, sizeof(attr));
	attr.valid = P9_SETATTR_SIZE;
	attr.size = 0;
	rc = p9p_setattr(ctx->p9_handle, fid, &attr);
	if (rc) {
		printf("setattr failed on file %s, error: %s (%d)\n", fid->path, strerror(rc), rc);
		return rc;
	}

	if (arg) {
/*		rc = p9p_write(ctx->p9_handle, fid, 0, count, arg); */
		msk_data_t data;
		data.data = arg;
		data.size = strlen(arg);
		data.max_size = data.size;
		p9c_reg_mr(ctx->p9_handle, &data);
		rc = p9pz_write(ctx->p9_handle, fid, 0, &data);
		p9c_dereg_mr(&data);
		if (rc < 0) {
			printf("write failed on file %s, error: %s (%d)\n", fid->path, strerror(-rc), -rc);
		}
		printf("wrote %d bytes\n", rc);
	}

	tmp = p9p_clunk(ctx->p9_handle, fid);
	if (tmp) {
		printf("clunk failed on fid %u (%s), error: %s (%d)\n", fid->fid, fid->path, strerror(tmp), tmp);
	}	
	return rc;
}

int p9s_rm(struct current_context *ctx, char *arg) {
	int rc;

	rc = p9p_unlinkat(ctx->p9_handle, ctx->cwd, arg, 0);
	if (rc) {
		printf("unlinkat failed on dir fid %u (%s), name %s, error: %s (%d)\n", ctx->cwd->fid, ctx->cwd->path, arg, strerror(rc), rc);
	}

	return rc;
}
int p9s_mv(struct current_context *ctx, char *arg) {
	int rc;
	char *dest;

	dest = strchr(arg, ' ');
	if (!dest) {
		printf("no dest?");
		return EINVAL;
	}

	dest[0]='\0';
	dest++;

	rc = p9p_renameat(ctx->p9_handle, ctx->cwd, arg, ctx->cwd, dest);
	if (rc) {
		printf("renameat failed on dir fid %u (%s), name %s to name %s, error: %s (%d)\n", ctx->cwd->fid, ctx->cwd->path, arg, dest, strerror(rc), rc);
	}

	return rc;
}

