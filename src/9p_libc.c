#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <mooshika.h>
#include "9p.h"
#include "utils.h"
#include "9p_proto.h"

int p9l_cd(struct p9_handle *p9_handle, char *path) {
	char *canon_path;
	struct p9_fid *fid;
	int rc;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);
	rc = p9p_walk(p9_handle, (canon_path[0] == '/' ? p9_handle->root_fid : p9_handle->cwd), canon_path, &fid);
	if (!rc) {
		p9p_clunk(p9_handle, p9_handle->cwd);
		p9_handle->cwd = fid;
	}

	free(canon_path);
	return rc;
}

int p9l_open(struct p9_handle *p9_handle, struct p9_fid **pfid, char *path, uint32_t mode, uint32_t flags, uint32_t gid) {
	char *canon_path, *dirname, *basename;
	struct p9_fid *fid = NULL;
	struct p9p_setattr attr;
	int rc, relative;

	canon_path = malloc(strlen(path)+1);
	if (!canon_path)
		return ENOMEM;

	strcpy(canon_path, path);
	path_canonicalizer(canon_path);
	relative = canon_path[9] == '/' ? 0 : 1;

	do {
		rc = p9p_walk(p9_handle, (relative ? p9_handle->cwd : p9_handle->root_fid), canon_path, &fid);
		if (rc) {
			/* file doesn't exist */
			path_split(canon_path, &dirname, &basename);
			if (basename == '\0') {
				return EINVAL;
			}

			rc = p9p_walk(p9_handle, (relative ? p9_handle->cwd : p9_handle->root_fid), dirname, &fid);
			if (rc) {
				INFO_LOG(p9_handle->debug, "cannot walk into parent dir '%s'", dirname);
				break;
			}
			rc = p9p_lcreate(p9_handle, fid, basename, mode, flags, gid, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug, "cannot create file '%s' in '%s'", basename, dirname);
				break;
			}
		} else {
			/* file exists, open and eventually truncate */
			rc = p9p_lopen(p9_handle, fid, flags, NULL);
			if (rc) {
				INFO_LOG(p9_handle->debug, "cannot open existing file '%s'", canon_path);
				break;
			}
			if (flags & O_TRUNC) {
				memset(&attr, 0, sizeof(attr));
				attr.valid = P9_SETATTR_SIZE;
				attr.size = 0;
				p9p_setattr(p9_handle, fid, &attr);				
			}
		}
	} while (0);

	if (rc && fid)
		p9p_clunk(p9_handle, fid);
	else
		*pfid = fid;

	free(canon_path);
	return rc;
}
