AM_CFLAGS = -g -Wall -Werror -I$(srcdir)/../../include -I$(srcdir)/..

noinst_PROGRAMS = test_bitmap test_utils find readwrite

test_bitmap_SOURCES = test_bitmap.c
test_bitmap_LDADD = ../lib9p.la

test_utils_SOURCES = test_utils.c
test_utils_LDADD = ../lib9p.la

find_SOURCES = find.c
find_LDADD = ../lib9p.la -lpthread

readwrite_SOURCES = readwrite.c
readwrite_LDADD = ../lib9p.la -lpthread
