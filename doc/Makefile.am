MAINTAINERCLEANFILES=Makefile.in

EXTRA_DIST = openct.xml openct.xsl openct.html openct.css
noinst_DATA = openct.html

%.html: %.xml
	        xsltproc -o $@ openct.xsl $< 
		tidy -im -utf8 -xml $@ || true

