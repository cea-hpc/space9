#ifndef P9_SHELL_FUNCTIONS_H
#define P9_SHELL_FUNCTIONS_H

struct current_context {
	struct p9_handle *p9_handle;
	struct p9_fid *cwd;
	struct p9_fid *fids;
};


int p9s_ls(struct current_context *ctx, char *arg);
int p9s_cd(struct current_context *ctx, char *arg);
int p9s_cat(struct current_context *ctx, char *arg);
int p9s_mkdir(struct current_context *ctx, char *arg);

#endif
