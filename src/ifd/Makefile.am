# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir)/src/scdl @CPPFLAGS@ @CFLAGS_PCSC@

lib_LTLIBRARIES = libifd.la

sbin_PROGRAMS = ifdhandler

DRIVERS	= cardman.c egate.c etoken.c eutron.c ikey2k.c ikey3k.c kaan.c smartboard.c towitoko.c

libifd_la_SOURCES = \
	apdu.c checksum.c conf.c device.c driver.c \
	init.c locks.c manager.c modules.c process.c \
	proto-t0.c proto-t1.c proto-trans.c proto-sync.c \
	protocol.c ctbcs.c reader.c serial.c usb.c utils.c \
	$(DRIVERS) \
	sys-null.c sys-linux.c sys-bsd.c
libifd_la_LIBADD = ../scdl/libscdl.la ../ct/libopenct.la @LIBDL@ @LIBUSB@
libifd_la_LDFLAGS = -version-info @OPENCT_LT_CURRENT@:@OPENCT_LT_REVISION@:@OPENCT_LT_AGE@

ifdhandler_SOURCES = ifdhandler.c
ifdhandler_LDADD = libifd.la

noinst_HEADERS = ctbcs.h ifdhandler.h internal.h
