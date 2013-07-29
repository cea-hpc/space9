9p
==

9P/RDMA abstraction layer


Dependencies
============

lib9p depends on mooshika, which itself uses libibverbs and librdmacm
(with -devel or -dev packages and autoconf/libtool for compiling)

Compiling
=========

Straightforward:

    ./autogen.sh
    ./configure <options>
    make
    make install


Configuration
=============

space9 init function will take a configuration file in argument - by
default src/sample.conf in our test programs, but there is no
systemwide default.


Test programs/examples
======================

You can find programs using the library in src/tests

Python
======

If you want to try the python modules without installing, you can
source src/tests/python-env.sh that will set the correct PYTHON_PATH
and LD_LIBRARY_PATH for you.

You can then start python or launch the python scripts in src/tests
