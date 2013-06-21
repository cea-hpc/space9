## Makefile for the python bindings
## Copyright (C) 2004  Matteo Vescovi <matteo.vescovi@yahoo.co.uk>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2, or (at your option)
## any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

if HAVE_SWIG
if ENABLE_PYTHON_BINDING

SWIG_INTERFACE =	../9p.i

BUILT_SOURCES =		space9_wrap.c space9.py
CLEANFILES =		space9_wrap.c space9.py

all-local:	space9_wrap.c space9.py
	$(PYTHON) setup.py build

install-exec-local:
	$(PYTHON) setup.py install --prefix=$(prefix)

clean-local:
	rm -rf build

space9_wrap.c space9.py:	$(SWIG_INTERFACE)
	$(SWIG) -python -I$(top_srcdir)/include -I$(top_srcdir)/src -o space9_wrap.c -outdir . $(srcdir)/$(SWIG_INTERFACE)

endif # ENABLE_PYTHON_BINDING
endif # HAVE_SWIG
