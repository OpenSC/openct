MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

if ENABLE_DOC
SUBDIRS = nonpersistent
endif
DIST_SUBDIRS = nonpersistent

dist_doc_DATA = README
dist_noinst_DATA = $(srcdir)/doxygen-footer.html $(srcdir)/*.gif
if ENABLE_API_DOC
dist_apidoc_DATA=api.out/html/*
else
dist_noinst_DATA+=api.out/html/*
endif

if SVN_CHECKOUT
api.out/html/*:	api.out
api.out:	$(top_srcdir)/src/include/openct/*.h \
		$(srcdir)/*.gif \
		doxygen.conf
	-rm -fr api.out
	$(DOXYGEN) doxygen.conf
	cp "$(srcdir)"/*.gif api.out/html
else
api.out/html/*:     $(abs_builddir)/api.out
$(abs_builddir)/api.out:
	$(LN_S) "$(srcdir)/api.out" api.out
endif

clean-local:
	-rm -fr api.out
