#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <mooshika.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "9p.h"
#include "utils.h"
#include "settings.h"

#include "9p_shell_functions.h"

#define BUF_SIZE 1024

struct functions {
	char *name;
	char *description;
	int (*func)(struct current_context *, char *);
};

static int run_threads;


static int print_help(struct current_context *, char *arg);

static struct functions functions[] = {
	{ "help", "help [<topic>]: this text", print_help },
	{ "pwd", "pwd: print current directory", p9s_pwd },
	{ "ls", "ls [-l] [<dir>]: List files in a directory", p9s_ls },
	{ "cd", "cd <dir>: navigates to directory", p9s_cd },
	{ "cat", "cat <file>: cat files...", p9s_cat },
	{ "xwrite", "xwrite <file> <content>: writes content into file (truncates)", p9s_xwrite },
	{ "mkdir", "mkdir <dir>: creates directory", p9s_mkdir },
	{ "rm", "rm <file>: removes file", p9s_rm },
	{ "mv", "mv <from> <to>: moves from to to, only cwd/no slash allowed atm", p9s_mv },
	{ "rmdir", "rm <dir>: removes a dir. Actually rm.", p9s_rm },
	{ "ln", "ln [-s] target [linkname]: links or symlinks", p9s_ln },
	{ NULL, NULL, NULL }
};

static int print_help(struct current_context *unused_ctx, char *arg) {
	struct functions *fn;
	for (fn=functions; fn->name != NULL; fn++) {
		if (strncmp(fn->name, arg, strlen(fn->name)))
			continue;

		printf("%s\n", fn->description);
		return 0;
	}

	for (fn=functions; fn->name != NULL; fn++) {
		printf("%s\n", fn->description);
	}

	return 0;
}

static void panic(int signal) {
	if (run_threads)
		run_threads = 0;
	else
		exit(1);
}

int main() {
	char *line;
	char *s;
	struct functions *fn;
	struct current_context ctx;
        int rc, len;

        rc = p9_init(&ctx.p9_handle, "sample.conf");
        if (rc) {
                ERROR_LOG("Init failure: %s (%d)", strerror(rc), rc);
                return rc;
        }

	run_threads = 1;
	signal(SIGINT, panic);

        INFO_LOG(1, "Init success");

	p9p_walk(ctx.p9_handle, ctx.p9_handle->root_fid, NULL, &ctx.cwd);

#ifdef HAVE_READLINE
	using_history();
#else
	line = malloc(BUF_SIZE);
#endif

	while (run_threads) {
#ifdef HAVE_READLINE
		line = readline("> ");

		/* EOF */
		if (!line)
			break;

		s = line;

		rc = history_expand(s, &line);

		add_history(line);
		if (rc == -1) {
			printf("Expansion error?\n");
			continue;
		} else if (rc == 2) { /* :p or something, print but no execute history */
			printf("%s\n", line);
			continue;
		}
#else		
		printf("> ");

		if (!line)
			break;

		if (fgets(line, BUF_SIZE, stdin) == NULL)
			break;
		if (line[0] == '\n')
			continue;

		s = strchr(line, '\n');
		if (!s)
			break;
		s[0] = '\0';
#endif

		if (!run_threads)
			break;

		for (fn=functions; fn->name != NULL; fn++) {
			len = strlen(fn->name);

			if (strncmp(fn->name, line, len))
				continue;

			if (line[len] == ' ')
				rc = fn->func(&ctx, line + len + 1);
			else if (line[len] == '\0')
				rc = fn->func(&ctx, line + len);
			else /* wasn't really this command */
				continue;

			break;
		}

		if (fn->name == NULL)
			printf("No such command: %s\n", line);

#ifdef HAVE_READLINE
		free(s);
#endif
	}

	p9p_clunk(ctx.p9_handle, ctx.cwd);

        p9_destroy(&ctx.p9_handle);

        return rc;
}
