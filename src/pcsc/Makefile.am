# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir)/src/ctapi @CPPFLAGS@ @CFLAGS_PCSC@

if HAVE_PCSC
lib_LTLIBRARIES = openct-ifd.la
endif

openct_ifd_la_SOURCES = pcsc.c
openct_ifd_la_LDFLAGS = -module -avoid-version
openct_ifd_la_LIBADD = ../ctapi/libopenctapi.la

noinst_HEADERS = ifdhandler.h
