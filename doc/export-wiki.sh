#!/bin/bash

set -e

export PATH=/opt/j2sdk1.4.2/bin:$PATH
export CLASSPATH=/usr/share/java/xalan2.jar
export SERVER=http://www.opensc.org
export WIKI=openct/wiki
export XSL=export-wiki.xsl

mkdir tmp

wget -P tmp $SERVER/$WIKI/TitleIndex

grep "\"/$WIKI/[^\"]*\"" tmp/TitleIndex \
        |sed -e "s#.*\"/$WIKI/\([^\"]*\)\".*#\1#g" \
	> tmp/WikiWords
sed -e /^Trac/d -e /^Wiki/d -e /^TitleIndex/d -e /^RecentChanges/d \
	-e /^CamelCase/d -e /^SandBox/d -i tmp/WikiWords

for A in WikiStart `cat tmp/WikiWords`
do
	wget -P tmp $SERVER/$WIKI/$A 
	java org.apache.xalan.xslt.Process -IN tmp/$A -XSL $XSL \
		-OUT $A.html
	sed -e "s#<a href=\"/$WIKI/\([^\"]*\)\"#<a href=\"\1.html\"#g" \
		-i $A.html
done

mv WikiStart.html index.html

wget http://www.opensc.org/trac/css/trac.css
