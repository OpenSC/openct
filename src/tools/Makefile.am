MAINTAINERCLEANFILES=Makefile.in

INCLUDES=-I$(top_srcdir)/include

bin_PROGRAMS=openct-tool

openct_tool_SOURCES= openct-tool.c
openct_tool_LDADD= -L$(top_builddir)/src/ct -lopenct
