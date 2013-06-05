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

#include "9p_shell_functions.h"

#define BUF_SIZE 1024

struct functions {
	char *name;
	char *description;
	void (*func)(struct current_context *, char *);
};

static int run_threads;


static void print_help(struct current_context *, char *arg);

static struct functions functions[] = {
	{ "help", "help [<topic>]: this text", print_help },
	{ "ls", "ls [-l] [<dir>]: List files in a directory", p9s_ls },
	{ "cd", "cd <dir>: navigates to directory", p9s_cd },
	{ "cat", "cat <file>: cat files...", p9s_cat },
	{ "mkdir", "mkdir <dir>: creates directory", p9s_mkdir },
	{ NULL, NULL, NULL }
};

static void print_help(struct current_context *unused_ctx, char *arg) {
	struct functions *fn;
	for (fn=functions; fn->name != NULL; fn++) {
		if (strncmp(fn->name, arg, strlen(fn->name)))
			continue;

		printf("%s\n", fn->description);
		return;
	}

	for (fn=functions; fn->name != NULL; fn++) {
		printf("%s\n", fn->description);
	}
}

static void panic(int signal) {
	if (run_threads)
		run_threads = 0;
	else
		exit(1);
}

int main() {
	char line[BUF_SIZE];
	struct functions *fn;
	struct current_context ctx;
        int ret, len;

        ret = p9_init(&ctx.p9_handle, "sample.conf");
        if (ret) {
                ERROR_LOG("Init failure: %s (%d)", strerror(ret), ret);
                return ret;
        }

	run_threads = 1;
	signal(SIGINT, panic);

        INFO_LOG(1, "Init success");

	while (run_threads) {
		printf("> ");
		if (fgets(line, BUF_SIZE, stdin) == NULL)
			break;

		for (fn=functions; fn->name != NULL; fn++) {
			len = strlen(fn->name);

			if (strncmp(fn->name, line, len))
				continue;

			if (line[len] == '\n' || line[len] == ' ')
				fn->func(&ctx, line + strlen(fn->name) + 1);
			else /* wasn't really this command */
				continue;

			break;
		}
	}

        p9_destroy(&ctx.p9_handle);

        return 0;
}
