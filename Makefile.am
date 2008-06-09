AUTOMAKE_OPTIONS = foreign 1.10
ACLOCAL_AMFLAGS = -I m4

MAINTAINERCLEANFILES = \
	config.log config.status \
	$(srcdir)/Makefile.in \
	$(srcdir)/config.h.in $(srcdir)/config.h.in~ $(srcdir)/configure \
	$(srcdir)/install-sh $(srcdir)/ltmain.sh $(srcdir)/missing \
	$(srcdir)/depcomp $(srcdir)/aclocal.m4 $(srcdir)/compile \
	$(srcdir)/config.guess $(srcdir)/config.sub \
	$(srcdir)/m4/ltsugar.m4 $(srcdir)/m4/libtool.m4 \
	$(srcdir)/m4/ltversion.m4 $(srcdir)/m4/lt~obsolete.m4 \
	$(srcdir)/m4/ltoptions.m4 \
	$(srcdir)/packaged
EXTRA_DIST = svnignore

SUBDIRS = etc macos src doc

dist_noinst_SCRIPTS = bootstrap
dist_noinst_DATA = LGPL-2.1 TODO \
	solaris/Makefile.client solaris/Makefile.server solaris/README \
	solaris/checkinstall.in solaris/devices-openct-fabric.xml-dist \
	solaris/openct-fabric-dist solaris/openct-hotplug-dist \
	solaris/openct.conf-dist solaris/pkginfo.in solaris/postinstall \
	solaris/preremove solaris/proto
dist_doc_DATA = NEWS

# Allow detection of packaged tarball
dist-hook:
	echo > "$(distdir)/packaged"
