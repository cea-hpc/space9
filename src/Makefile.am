SUBDIRS = . tests #build . before tests

AM_CFLAGS = -g -D_REENTRANT -Wall -Wimplicit -Wformat -Wmissing-braces -Wno-pointer-sign -Werror -I$(srcdir)/../include

lib_LTLIBRARIES = lib9p.la
lib9p_la_SOURCES = 9p_callbacks.c  9p_core.c  9p_init.c  9p_proto.c
lib9p_la_LDFLAGS = -version-info 2:0:0
lib9p_la_LIBADD = -lrdmacm -libverbs -lpthread -lrt

bin_PROGRAMS = 9p_shell
9p_shell_SOURCES = 9p_shell.c
9p_shell_LDADD = -lpthread
9p_shell_LDADD += lib9p.la

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA = lib9p.pc


