# Process this file with automake to create Makefile.in

bundledir = @BUNDLEDIR@

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir)/src/ctapi @CPPFLAGS@ @PCSC_CFLAGS@

if HAVE_PCSC
lib_LTLIBRARIES = openct-ifd.la
endif

openct_ifd_la_SOURCES = pcsc.c
openct_ifd_la_LDFLAGS = -module -avoid-version
openct_ifd_la_LIBADD = ../ctapi/libopenctapi.la

noinst_HEADERS = ifdhandler.h

# Don't use HAVE_BUNDLES for this case, pcsc-lite loads
# USB drivers from MacOS X like bundles for all platforms
install-exec-local: install-libLTLIBRARIES
if HAVE_PCSC
	$(mkinstalldirs) $(DESTDIR)$(bundledir)
	${top_srcdir}/macos/libtool-bundle -e $(top_srcdir)/etc/Info.plist \
	$(DESTDIR)$(libdir)/openct-ifd.so $(DESTDIR)$(bundledir)/
endif

uninstall-local: uninstall-libLTLIBRARIES
if HAVE_PCSC
	rm -rf $(DESTDIR)$(bundledir)/openct-ifd.bundle/
endif
