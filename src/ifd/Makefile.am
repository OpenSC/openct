MAINTAINERCLEANFILES=Makefile.in

INCLUDES=-I$(top_srcdir)/include
LDADD=-L$(top_builddir)/src/ct

lib_LTLIBRARIES = libifd.la

sbin_PROGRAMS = ifdhandler

DRIVERS	= cardman.c egate.c etoken.c eutron.c ikey3k.c kaan.c towitoko.c
libifd_la_SOURCES= apdu.c checksum.c conf.c confparse.c device.c driver.c \
		   init.c internal.h linux.c locks.c manager.c modules.c \
		   process.c proto-t0.c proto-t1.c proto-trans.c proto-sync.c \
		   protocol.c \
	           ctbcs.c ctbcs.h reader.c serial.c usb.c utils.c bsd.c \
		   ${DRIVERS} 
libifd_la_LIBADD= -L$(top_builddir)/src/ct -lopenct @LIBDL@
libifd_la_LDFLAGS= -versioninfo 0:0

ifdhandler_SOURCES = ifdhandler.c ifdhandler.h
ifdhandler_LDADD = -lifd
