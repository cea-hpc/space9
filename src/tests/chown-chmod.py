#!/usr/bin/python

import os
import space9
rc = 0

handle = space9.p9_handle( os.path.dirname(__file__) + "/../sample.conf")
fid = handle.open('sigmund/chownchmod-test', space9.O_RDWR | space9.O_CREAT, 0644)

oldstat = fid.stat()

fid.chown(1000, 100)
fid.chmod(0246)

stat = fid.stat()
if stat["uid"] != 1000 or stat["gid"] != 100 or (stat["mode"] & 0777) != 0246:
	print "test failed!"
	print "old stat:", oldstat
	print "new stat:", stat
	rc = 1
else:
	print "test success"

fid.unlink()
fid.clunk()

exit(rc)
