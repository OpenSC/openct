MAINTAINERCLEANFILES=Makefile.in

INCLUDES=-I${top_srcdir}/include

LIBADD=-lifd

lib_LTLIBRARIES = libopenctapi.la

libopenctapi_la_SOURCES = ctapi.c ctapi.h
libopenctapi_la_LDFLAGS = -module -avoid-version
libopenctapi_LIBS = -L../ct -lct
