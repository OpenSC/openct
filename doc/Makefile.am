MAINTAINERCLEANFILES=Makefile.in

EXTRA_DIST = openct.xml openct.xsl 
noinst_DATA = openct.html openct.css

%.html: %.xml
	        xsltproc -o $@ openct.xsl $< 
		tidy -im -utf8 -xml $@ || true

