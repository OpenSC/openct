# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = libopenct.pc.in

lib_LTLIBRARIES = libopenct.la

libopenct_la_SOURCES = \
	buffer.c client.c error.c mainloop.c \
	socket.c status.c tlv.c
libopenct_la_LDFLAGS = -version-info @OPENCT_LT_CURRENT@:@OPENCT_LT_REVISION@:@OPENCT_LT_AGE@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libopenct.pc
