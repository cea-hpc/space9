#!/usr/bin/python

import os
import space9

handle = space9.p9_handle( os.path.dirname(__file__) + "/../sample.conf")

fid = handle.open('sigmund/xattr-test', space9.O_RDWR | space9.O_CREAT, 0644)

print "list of file xattrs:", fid.xattrlist(100)

i = fid.xattrset("security.foo", "foobar")
if i != 6:
	print "xattrset failed, should have been 6:", i

print "same with security.foo on top:", fid.xattrlist(100)
print "and its content:", fid.xattrget("security.foo", 100)

fid.xattrset("security.foo", "")

print "and deleted security.foo:", fid.xattrlist(100)

fid.unlink()
fid.clunk()
