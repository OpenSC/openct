MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

if ENABLE_PCSC
lib_LTLIBRARIES = openct-ifd.la
endif

openct_ifd_la_SOURCES = pcsc.c
openct_ifd_la_LDFLAGS = -module -avoid-version -no-undefined
openct_ifd_la_LIBADD = $(PCSC_LIBS) $(top_builddir)/src/ctapi/libopenctapi.la
openct_ifd_la_CFLAGS = $(AM_CFLAGS) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/include \
	-I$(top_srcdir)/src/ctapi \
	$(PCSC_CFLAGS)

# Don't use ENABLE_BUNDLES for this case, pcsc-lite loads
# USB drivers from MacOS X like bundles for all platforms
install-exec-local: install-libLTLIBRARIES
if ENABLE_PCSC
if ENABLE_BUNDLES
	$(MKDIR_P) "$(DESTDIR)$(bundledir)"
	"$(top_srcdir)/macos/libtool-bundle" -e "$(top_srcdir)/etc/Info.plist" \
		"$(DESTDIR)$(libdir)/openct-ifd.so" "$(DESTDIR)$(bundledir)"
endif
endif

uninstall-local: uninstall-libLTLIBRARIES
if ENABLE_PCSC
if ENABLE_BUNDLES
	rm -rf "$(DESTDIR)$(bundledir)/openct-ifd.bundle/"
endif
endif
