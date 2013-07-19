#!/usr/bin/python

import os, sys
import space9

handle = space9.p9_handle( os.path.dirname(__file__) + "/../sample.conf")

paths = ["bigtree"]

if len(sys.argv) >= 2:
	paths = sys.argv[1:]

for path in paths:
	print "removing " + path
	handle.rmrf(path)

