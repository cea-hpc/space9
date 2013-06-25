#!/bin/sh

TESTDIR="$(readlink -m `dirname $0`)"
PYTHONPATH=$(eval echo "$TESTDIR/../python/build/lib."*)

LD_LIBRARY_PATH="$TESTDIR/../.libs" PYTHONPATH="$PYTHONPATH" exec ipython "$@"
