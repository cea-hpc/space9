#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <mooshika.h>

#include "9p.h"
#include "9p_proto.h"
#include "utils.h"
#include "settings.h"

static void *walkthr

int main() {
	int rc;
	struct p9_handle *p9_handle;

        rc = p9_init(&p9_handle, "sample.conf");
        if (rc) {
                ERROR_LOG("Init failure: %s (%d)", strerror(rc), rc);
                return rc;
        }

        INFO_LOG(1, "Init success");
        p9_destroy(&p9_handle);

        return rc;
}
