MAINTAINERCLEANFILES=Makefile.in openct-ifd.la

INCLUDES=-I$(top_srcdir)/src/ifd -I$(top_builddir)/include

EXTRA_DIST= pcsc.c

if HAVE_PCSCLITE
lib_LTLIBRARIES=openct-ifd.la

openct_ifd_la_SOURCES= pcsc.c ifdhandler.h
openct_ifd_la_LDFLAGS=-module -avoid-version
endif
