# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = @CPPFLAGS@ @CFLAGS_PCSC@

if HAVE_PCSCLITE
lib_LTLIBRARIES = openct-ifd.la
endif

openct_ifd_la_SOURCES = pcsc.c
openct_ifd_la_LDFLAGS = -module -avoid-version

noinst_HEADERS = ifdhandler.h
