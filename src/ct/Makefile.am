MAINTAINERCLEANFILES=Makefile.in

EXTRA_DIST=libopenct.pc.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libopenct.pc

INCLUDES=-I${top_srcdir}/include

lib_LTLIBRARIES=libopenct.la

libopenct_la_SOURCES= buffer.c client.c error.c mainloop.c socket.c \
		      status.c tlv.c
libopenct_la_LDFLAGS= -versioninfo 0:0
