MAINTAINERCLEANFILES=Makefile.in

INCLUDES=-I$(top_srcdir)/include
LDADD=-L$(top_builddir)/src/ct

lib_LTLIBRARIES = libifd.la

sbin_PROGRAMS = ifdhandler

DRIVERS	= etoken.c kaan.c towitoko.c egate.c
libifd_la_SOURCES= apdu.c checksum.c conf.c confparse.c device.c driver.c \
		   init.c internal.h linux.c locks.c manager.c modules.c \
		   process.c proto-t0.c proto-t1.c proto-trans.c protocol.c \
	           reader.c serial.c usb.c utils.c ${DRIVERS}
libifd_la_LIBADD= -L$(top_builddir)/src/ct -lopenct -ldl -lusb
libifd_la_LDFLAGS= -versioninfo 0:0

print_atr_SOURCES = print-atr.c
print_atr_LDADD = -lifd

ifdhandler_SOURCES = ifdhandler.c ifdhandler.h
ifdhandler_LDADD = -lifd
