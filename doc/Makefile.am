MAINTAINERCLEANFILES=Makefile.in

EXTRA_DIST = openct.xml openct.html

STYLESHEET=/usr/share/sgml/docbook/stylesheet/xsl/nwalsh/xhtml/docbook.xsl

%.html: %.xml
	        xsltproc -o $@ $(STYLESHEET) $< 

