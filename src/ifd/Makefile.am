# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

lib_LTLIBRARIES = libifd.la

sbin_PROGRAMS = ifdhandler

DRIVERS	= cardman.c egate.c etoken.c eutron.c ikey3k.c kaan.c towitoko.c
SYS = linux.c bsd.c

libifd_la_SOURCES = \
	apdu.c checksum.c conf.c confparse.c device.c driver.c \
	init.c locks.c manager.c modules.c process.c \
	proto-t0.c proto-t1.c proto-trans.c proto-sync.c \
	protocol.c ctbcs.c reader.c serial.c usb.c utils.c \
	$(DRIVERS) $(SYS)

libifd_la_LIBADD = ../ct/libopenct.la @LIBDL@
libifd_la_LDFLAGS = -versioninfo 0:0

ifdhandler_SOURCES = ifdhandler.c
ifdhandler_LDADD = libifd.la

noinst_HEADERS = ctbcs.h ifdhandler.h internal.h
