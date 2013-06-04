ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src

include_HEADERS = include/9p.h

rpm: dist lib9p.spec
	rpmbuild $(RPM_DB_ARGS) $(extra_arg) -tb $(distdir).tar.gz

srpm: dist lib9p.spec
	rpmbuild $(RPM_DB_ARGS) $(extra_arg) -ts $(distdir).tar.gz

rpms: rpm srpm

clean-local:
	rm -rf doxygen

doc:
	doxygen ./doxygen.conf
	cd doxygen/latex; make pdf

EXTRA_DIST=lib9p.spec \
           LICENSE
