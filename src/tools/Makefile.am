# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

bin_PROGRAMS = openct-tool
sbin_PROGRAMS = openct-control

openct_tool_SOURCES = openct-tool.c
openct_tool_LDADD = ../ct/libopenct.la

openct_control_SOURCES = openct-control.c
openct_control_LDADD = ../ifd/libifd.la ../ct/libopenct.la
