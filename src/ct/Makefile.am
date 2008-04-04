MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

lib_LTLIBRARIES = libopenct.la
pkgconfig_DATA = libopenct.pc

libopenct_la_SOURCES = \
	buffer.c client.c error.c mainloop.c path.c \
	socket.c status.c tlv.c
libopenct_la_CFLAGS = $(AM_CFLAGS) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/include
libopenct_la_LDFLAGS = \
	-version-info @OPENCT_LT_CURRENT@:@OPENCT_LT_REVISION@:@OPENCT_LT_AGE@ \
	-no-undefined
