#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>      // gethostbyname
#include <sys/socket.h> // gethostbyname
#include <mooshika.h>
#include "9p.h"
#include "9p_proto.h"
#include "utils.h"
#include "settings.h"

int main() {
        struct p9_handle *p9_handle;
        int ret;

        ret = p9_init(&p9_handle, "sample.conf");
        if (ret) {
                ERROR_LOG("Init failure: %s (%d)", strerror(ret), ret);
                return ret;
        }

        INFO_LOG(1, "Init success");

        getc(stdin);

        p9_destroy(&p9_handle);

        return 0;
}
