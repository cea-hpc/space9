9p
==

9P/RDMA abstraction layer


Dependencies
============

lib9p depends on mooshika, which itself uses libibverbs and librdmacm (with -devel or -dev packages and autoconf/libtool for compiling)

Compiling
=========

Straightforward:

    ./autogen.sh
    ./configure
    make
    make install
