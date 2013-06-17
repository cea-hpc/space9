AM_CFLAGS = -g -Wall -Werror -I$(srcdir)/../../include -I$(srcdir)/..

check_PROGRAMS = test_bitmap test_utils test_parallel

test_bitmap_SOURCES = test_bitmap.c
test_bitmap_LDADD = ../lib9p.la

test_utils_SOURCES = test_utils.c
test_utils_LDADD = ../lib9p.la

test_parallel_SOURCES = test_parallel.c
test_parallel_LDADD = ../lib9p.la -lpthread
