# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

lib_LTLIBRARIES = openct-ctapi.la

openct_ctapi_la_SOURCES = ctapi.c ctapi.h
openct_ctapi_la_LDFLAGS = -module -avoid-version
openct_ctapi_LIBS = ../ct/libopenct.la ../ifd/libifd.la
