# Process this file with automake to create Makefile.in

bundledir = @BUNDLEDIR@

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir)/src/ctapi @CPPFLAGS@ @CFLAGS_PCSC@

if HAVE_PCSC
lib_LTLIBRARIES = openct-ifd.la
endif

openct_ifd_la_SOURCES = pcsc.c
openct_ifd_la_LDFLAGS = -module -avoid-version
openct_ifd_la_LIBADD = ../ctapi/libopenctapi.la

noinst_HEADERS = ifdhandler.h

install-exec-local: install-libLTLIBRARIES
if HAVE_BUNDLES
	-${top_srcdir}/macos/libtool-bundle -e $(top_srcdir)/etc/Info.plist \
	$(DESTDIR)$(libdir)/openct-ifd.so $(DESTDIR)$(bundledir)/
endif
