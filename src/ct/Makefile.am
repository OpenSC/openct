# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = libopenct.pc.in

lib_LTLIBRARIES = libopenct.la

libopenct_la_SOURCES = \
	buffer.c client.c error.c mainloop.c \
	socket.c status.c tlv.c
libopenct_la_LDFLAGS = -versioninfo 0:0

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libopenct.pc
