# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

lib_LTLIBRARIES = libopenctapi.la

libopenctapi_la_SOURCES = ctapi.c ctapi.h
libopenctapi_la_LDFLAGS = -module -avoid-version
libopenctapi_la_LIBADD = ../ct/libopenct.la ../ifd/libifd.la

includedir = @includedir@/openct
include_HEADERS = ctapi.h
