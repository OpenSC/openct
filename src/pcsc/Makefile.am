MAINTAINERCLEANFILES=Makefile.in ifdhandler.la

INCLUDES=-I$(top_srcdir)/src/ifd -I$(top_builddir)/include

EXTRA_DIST= pcsc.c

if HAVE_PCSCLITE
lib_LTLIBRARIES=openct-ifd.la

ifdhandler_la_SOURCES= pcsc.c ifdhandler.h
ifdhandler_la_LDFLAGS=-module -avoid-version
endif
