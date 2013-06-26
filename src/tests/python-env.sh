# bash specific source filename...

TESTDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PYTHONPATH=$(eval echo "$TESTDIR/../python/build/lib."*)
LD_LIBRARY_PATH="$TESTDIR/../.libs"

export LD_LIBRARY_PATH PYTHONPATH
