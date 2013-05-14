#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <dirent.h>     // MAXNAMLEN
#include <sys/param.h>  // MAXPATHLEN
#include <mooshika.h>
#include "log.h"
#include "settings.h"

void callback_recv(msk_trans_t *trans, msk_data_t *data, void *arg) {
}

void callback_send(msk_trans_t *trans, msk_data_t *data, void *arg) {
}

struct p9_conf {
	char aname[MAXPATHLEN];
	char user[MAXNAMLEN];
	char uname[MAXNAMLEN];
	int recv_num;
	uint32_t msize;
	struct msk_trans_attr trans_attr;
};

enum conftype { 
	INT,
	IP,
	IP6,
	PORT,
	PORT6,
	STRING,
	SIZE
};

struct conf { char *token; enum conftype type; int offset; };

#define offsetof(type, member)  __builtin_offsetof (type, member)

static struct conf conf_array[] = {
	{ "server", IP, 0 },
	{ "port", PORT, 0 },
	{ "server6", IP6, 0 },
	{ "port6", PORT6, 0 },
	{ "msize", SIZE, offsetof(struct p9_conf, msize) },
	{ "recv_num", INT, offsetof(struct p9_conf, recv_num) },
	{ "sq_depth", INT, offsetof(struct p9_conf, trans_attr) + offsetof(struct msk_trans_attr, sq_depth) },
	{ "rq_depth", INT, offsetof(struct p9_conf, trans_attr) + offsetof(struct msk_trans_attr, rq_depth) },
	{ "aname", STRING, offsetof(struct p9_conf, aname) },
	{ "uname", STRING, offsetof(struct p9_conf, uname) },
	{ "user", STRING, offsetof(struct p9_conf, user) },
	{ NULL, 0, 0 }
};

static inline int set_size(int *val, char *unit) {
	switch(unit[0]) {
		case 'k':
		case 'K':
			*val *= 1024;
			break;
		case 'm':
		case 'M':
			*val *= 1024 * 1024;
			break;
		case 'g':
		case 'G':
			*val *= 1024 * 1024 * 1024;
			break;
		default:
			ERROR_LOG("unknown unit '%c'", unit[0]);
			return EINVAL;
	}	

	return 0;
}

int parser(char *conf_file, struct p9_conf *p9_conf) {
	FILE *fd;
	char line[2*MAXPATHLEN];
	int i, ret;
	struct hostent *host;
	char buf_s[MAXNAMLEN];
	int buf_i;
	void *ptr;

	fd = fopen(conf_file, "r");

	if (fd == NULL) {
		i = errno;
		ERROR_LOG("Could not open %s: %s (%d)", conf_file, strerror(i), i);
		return i;
	}

	// fill default values.
	memset(p9_conf, 0, sizeof(struct p9_conf));
	p9_conf->trans_attr.server = -1;
	strcpy(p9_conf->uname, DEFAULT_UNAME);
	strcpy(p9_conf->user, DEFAULT_USER);
	p9_conf->recv_num = DEFAULT_RECV_NUM;
	p9_conf->msize = DEFAULT_MSIZE;

	while (fgets(line, 2*MAXPATHLEN, fd)) {
		// skip comments
		if (line[0] == '#' || line[0] == '\n')
			continue;

		for (i=0; conf_array[i].token != NULL; i++) {
			if (strncmp(conf_array[i].token, line, strlen(conf_array[i].token)))
				continue;

			// we have a match
			switch(conf_array[i].type) {
				case INT:
					ptr = (char*)p9_conf + conf_array[i].offset;
					if (sscanf(line, "%*s = %i", (int*)ptr) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						return EINVAL;
					}
					INFO_LOG("Read %s: %i", conf_array[i].token, *(int*)ptr);
					break;
				case STRING:
					ptr = (char*)p9_conf + conf_array[i].offset;
					if (sscanf(line, "%*s = %s", (char*)ptr) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						return EINVAL;
					}
					INFO_LOG("Read %s: %s", conf_array[i].token, (char*)ptr);
					break;
				case SIZE:
					ptr = (char*)p9_conf + conf_array[i].offset;
					ret = sscanf(line, "%*s = %i %[a-zA-Z] %i", (int*)ptr, buf_s, &buf_i);
					if (ret >= 2) {
						if (set_size((int*)ptr, buf_s))
							return EINVAL;
						if (ret == 3)
							*(int*)ptr += buf_i;
					} else if (ret != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						return EINVAL;
					}
					INFO_LOG("Read %s: %i", conf_array[i].token, *(int*)ptr);
					break;
				case IP:
					if (sscanf(line, "%*s = %s", buf_s) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						return EINVAL;
					}
					host = gethostbyname(buf_s);
					//FIXME: if (host->h_addrtype == AF_INET6) {
					p9_conf->trans_attr.addr.sa_in.sin_family = AF_INET;
					memcpy(&p9_conf->trans_attr.addr.sa_in.sin_addr, host->h_addr_list[0], 4);

					// Sanity check: we got an IP
					p9_conf->trans_attr.server = 0;

					// Default port. depends on the sin family
					((struct sockaddr_in*) &p9_conf->trans_attr.addr)->sin_port = htons(DEFAULT_PORT);

					INFO_LOG("Read %s: %s", conf_array[i].token, buf_s);
					break;
				case PORT:
					if (sscanf(line, "%*s = %i", &buf_i) != 1) {
						ERROR_LOG("scanf error on line: %s", line);
						return EINVAL;
					}
					((struct sockaddr_in*) &p9_conf->trans_attr.addr)->sin_port = htons(buf_i);
					INFO_LOG("Read %s: %i", conf_array[i].token, buf_i);
					break;
				default:
					ERROR_LOG("token %s not yet implemented", conf_array[i].token);
			}
			break;
		}

		// no match found
		if (conf_array[i].token == NULL) {
			ERROR_LOG("Unknown configuration entry: %s", line);
		}
	}

	return 0;	
}

struct p9_handle {
	msk_trans_t *trans;
	struct ibv_mr *mr;
	char *rdmabuf;
	msk_data_t *rdata;
	msk_data_t *wdata;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	uint16_t *tags;
	uint16_t *fids;	
};

void p9_destroy(struct p9_handle **pp9_handle) {
	struct p9_handle *p9_handle = *pp9_handle;
	if (p9_handle) {
		if (p9_handle->wdata) {
			free(p9_handle->wdata);
			p9_handle->wdata = NULL;
		}
		if (p9_handle->rdata) {
			free(p9_handle->rdata);
			p9_handle->rdata = NULL;
		}
		if (p9_handle->mr) {
			msk_dereg_mr(p9_handle->mr);
			p9_handle->mr = NULL;
		}
		if (p9_handle->rdmabuf) {
			free(p9_handle->rdmabuf);
			p9_handle->rdmabuf = NULL;
		}
	}
}

int p9_init(struct p9_handle **pp9_handle, char *conf_file) {
	struct p9_conf p9_conf;
	struct p9_handle *p9_handle;
	int ret;

	ret = parser("sample.conf", &p9_conf);
	if (ret) {
		ERROR_LOG("parsing error");
		return ret;
	}
	if (p9_conf.aname[0] == '\0' || p9_conf.trans_attr.server == -1) {
		ERROR_LOG("You need to set at least aname and server");
		return EINVAL;
	}

	p9_handle = malloc(sizeof(struct p9_handle));
	if (p9_handle == NULL) {
		ERROR_LOG("Could not allocate p9_handle");
		return ENOMEM;
	}

	ret = msk_init(&p9_handle->trans, &p9_conf.trans_attr);
	if (ret) {
		ERROR_LOG("msk_init failed: %s (%d)", strerror(ret), ret);
		return ret;
	}

	ret = msk_connect(p9_handle->trans);
	if (ret) {
		ERROR_LOG("msk_connect failed: %s (%d)", strerror(ret), ret);
		return ret;
	}

	// alloc buffers, post receive buffers
	p9_handle->rdmabuf = malloc(2 * p9_conf.recv_num * p9_conf.msize);
	if (p9_handle->rdmabuf == NULL) {
		ERROR_LOG("Could not allocate data buffer (%iMB)",
			2 * p9_conf.recv_num * p9_conf.msize / 1024 / 1024);
		return ENOMEM;
	}
	p9_handle->mr = msk_reg_mr(p9_handle->trans, p9_handle->rdmabuf, 2 * p9_conf.recv_num * p9_conf.msize, IBV_ACCESS_LOCAL_WRITE);
	if (p9_handle->mr == NULL) {
		ERROR_LOG("Could not register memory buffer");
		return EIO;
	}

	

	//msk_finalize_connect(p9_handle->trans);
	return 0;
}

int main() {
	struct p9_conf p9_conf;

       	parser("sample.conf", &p9_conf);

	return 0;
}
