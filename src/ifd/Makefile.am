# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir)/src/scdl @CPPFLAGS@ @CFLAGS_PCSC@

lib_LTLIBRARIES = libifd.la

sbin_PROGRAMS = ifdhandler ifdproxy

libifd_la_SOURCES = \
	apdu.c atr.c checksum.c conf.c ctbcs.c device.c driver.c \
	init.c locks.c manager.c modules.c process.c protocol.c \
	reader.c serial.c usb.c usb-descriptors.c utils.c \
	\
	ifd-cardman.c ifd-ccid.c ifd-egate.c ifd-etoken.c ifd-eutron.c \
	ifd-gempc.c ifd-ikey2k.c ifd-ikey3k.c ifd-kaan.c \
	ifd-smartboard.c ifd-towitoko.c \
	\
	proto-gbp.c proto-sync.c proto-t0.c proto-t1.c \
	proto-trans.c \
	\
	sys-solaris.c sys-bsd.c sys-linux.c sys-null.c \
	\
	ria.c
libifd_la_LIBADD = ../scdl/libscdl.la ../ct/libopenct.la @LIBDL@ @LIBUSB@
libifd_la_LDFLAGS = -version-info @OPENCT_LT_CURRENT@:@OPENCT_LT_REVISION@:@OPENCT_LT_AGE@

ifdhandler_SOURCES = ifdhandler.c
ifdhandler_LDADD = libifd.la

ifdproxy_SOURCES = ifdproxy.c ria-device.c ria-server.c
ifdproxy_LDADD = libifd.la

noinst_HEADERS = atr.h ctbcs.h ifdhandler.h internal.h ria.h usb-descriptors.h
