# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

lib_LTLIBRARIES = libopenctapi.la

libopenctapi_la_SOURCES = ctapi.c ctapi.h
libopenctapi_la_LDFLAGS = -module -avoid-version
libopenctapi_LIBS = ../ct/libopenct.la ../ifd/libifd.la
