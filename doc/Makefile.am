# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = openct.xml openct.xsl openct.html openct.css
noinst_DATA = openct.html

%.html: %.xml
	@if [ -f /usr/share/sgml/docbook/stylesheet/xsl/nwalsh/xhtml/docbook.xsl ]; then \
	        echo xsltproc -o $@ openct.xsl $<; \
	        xsltproc -o $@ openct.xsl $<; \
		echo tidy -im -utf8 -xml $@; \
		tidy -im -utf8 -xml $@ || true; \
	else \
		echo "docbook.xsl not installed, cannot generate $@ docs" >&2; \
		touch $@; \
	fi
