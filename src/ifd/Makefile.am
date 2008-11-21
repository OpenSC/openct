MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

noinst_LTLIBRARIES = libifd.la
sbin_PROGRAMS = ifdhandler ifdproxy
noinst_HEADERS = atr.h ctbcs.h ifdhandler.h internal.h ria.h usb-descriptors.h

libifd_la_SOURCES = \
	apdu.c atr.c checksum.c conf.c ctbcs.c device.c driver.c \
	init.c locks.c manager.c modules.c pcmcia.c pcmcia-block.c process.c protocol.c \
	reader.c serial.c usb.c usb-descriptors.c utils.c \
	\
	ifd-acr30u.c ifd-cardman.c ifd-ccid.c ifd-cm4000.c ifd-egate.c \
	ifd-etoken.c ifd-etoken64.c ifd-eutron.c ifd-gempc.c ifd-ikey2k.c \
	ifd-ikey3k.c ifd-kaan.c ifd-pertosmart1030.c ifd-pertosmart1038.c \
	ifd-smartboard.c ifd-smph.c ifd-starkey.c ifd-towitoko.c cardman.h \
	ifd-cyberjack.c ifd-rutoken.c ifd-epass3k.c \
	\
	proto-gbp.c proto-sync.c proto-t0.c proto-t1.c \
	proto-trans.c proto-escape.c \
	\
	sys-sunray.c sys-solaris.c sys-bsd.c sys-linux.c sys-null.c sys-osx.c \
	\
	ria.c
# new driver not working yet: ifd-wbeiuu.c
libifd_la_LIBADD = $(top_builddir)/src/ct/libopenct.la $(LTLIB_LIBS) $(OPTIONAL_LIBUSB_LIBS)
libifd_la_CFLAGS = $(AM_CFLAGS) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/include \
	$(OPTIONAL_PCSC_CFLAGS) $(OPTIONAL_LIBUSB_CFLAGS) $(LTLIB_CFLAGS)

ifdhandler_SOURCES = ifdhandler.c
ifdhandler_LDADD = libifd.la
ifdhandler_CFLAGS = $(AM_CFLAGS) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/include

ifdproxy_SOURCES = ifdproxy.c ria-device.c ria-server.c
ifdproxy_LDADD = libifd.la
ifdproxy_CFLAGS = $(AM_CFLAGS) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/include
