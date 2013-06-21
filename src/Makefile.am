SUBDIRS = . tests python #build . before tests

AM_CFLAGS = -g -D_REENTRANT -Wall -Wimplicit -Wformat -Wmissing-braces -Wno-pointer-sign -Werror -I$(srcdir)/../include

lib_LTLIBRARIES = libspace9.la
libspace9_la_SOURCES = 9p_callbacks.c 9p_core.c 9p_init.c 9p_proto.c 9p_utils.c 9p_libc.c 9p_shell_functions.c 9p_tcp.c
libspace9_la_LDFLAGS = -version-info 2:0:0
libspace9_la_LIBADD = -lpthread -lrt

if HAVE_MOOSHIKA
libspace9_la_LIBADD += -lmooshika -lrdmacm -libverbs
endif

bin_PROGRAMS = 9p_shell
9p_shell_SOURCES = 9p_shell.c
9p_shell_LDADD = libspace9.la
if ENABLE_READLINE
9p_shell_LDADD += -lreadline -lhistory
endif

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA = libspace9.pc

EXTRA_DIST = 9p.i bitmap.h bucket.h utils.h settings.h 9p_proto_internals.h 9p_tcp.h

sh: 9p_shell
	rlwrap ./9p_shell
