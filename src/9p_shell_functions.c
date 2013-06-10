#include <sys/types.h>
#include <unistd.h>

#include <mooshika.h>
#include "9p.h"
#include "utils.h"
#include "9p_proto.h"
#include "9p_shell_functions.h"

int p9s_ls(struct current_context *ctx, char *args) {
	return 0;
}

int p9s_cd(struct current_context *ctx, char *arg) {
	int rc;
	char *end;
	struct p9_fid *fid;

	if (arg[0] == '/') {
		rc = p9p_clunk(ctx->p9_handle, ctx->cwd);
		if (rc) {
			printf("clunk failed on %s, error: %s (%d)\n", ctx->cwd->path, strerror(rc), rc);
		}

		ctx->cwd = ctx->p9_handle->root_fid;
	}

	do {
		end = strchr(arg, '/');
		if (end)
			end[0] = '\0';
		rc = p9p_walk(ctx->p9_handle, ctx->cwd, arg, &fid);
		if (rc) {
			printf("walk failed from %s to %s, error: %s (%d)\n", ctx->cwd->path, arg, strerror(rc), rc);
			break;
		}
		rc = p9p_clunk(ctx->p9_handle, ctx->cwd);
		if (rc) {
			printf("clunk failed on %s, error: %s (%d)\n", ctx->cwd->path, strerror(rc), rc);
		}
		ctx->cwd = fid;
	} while (end);

	return rc;
}

int p9s_cat(struct current_context *ctx, char *arg) {
	return 0;
}
int p9s_mkdir(struct current_context *ctx, char *arg) {
	int rc, clunk;
	char str[MAXPATHLEN];
	struct p9_fid *fid;

	path_canonicalizer(arg);

	path_dirname(arg, str, MAXPATHLEN);

	clunk = 1;
	if (arg[0] == '/') {
		rc = p9p_walk(ctx->p9_handle, ctx->p9_handle->root_fid, str, &fid);
		if (rc) {
			ERROR_LOG("walk to %s failed, error: %s (%d)", str, strerror(rc), rc);
		}
	} else if (strncmp(str, ".", 2)) {
		/* not current directory */
		rc = p9p_walk(ctx->p9_handle, ctx->cwd, str, &fid);
		if (rc) {
			ERROR_LOG("walk from %s to %s failed, error: %s (%d)", ctx->cwd->path, str, strerror(rc), rc);
		}
	} else {
		clunk = 0;
	}

	path_basename(arg, str, MAXPATHLEN);

	rc = p9p_mkdir(ctx->p9_handle, fid, str, 0644, getegid(), NULL);
	if (clunk)
		rc = p9p_clunk(ctx->p9_handle, fid);
	return rc;
}

int p9s_pwd(struct current_context *ctx, char *arg) {
	printf("%s\n", ctx->cwd->path);
	return 0;
}
