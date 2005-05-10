#!/bin/bash

# Main variables.  Replace these defaults with ones suitable for your
# environment.
pagesList=${pagesList:-"TracGuide TracInstall TracUpgrade"}
server=${server:-"https://trac.prelude-ids.org"}
xsl=${xsl:-"export-wiki.xsl"}
tmpDir=${tmpDir:-"tmp"}
docsDir=${docsDir:-"docs"}

#####################################################################

# generated temp variables
tmpIndex="$tmpDir/tmp.xml"
pages=($pagesList)

# create the directories as needed
mkdir $tmpDir
mkdir $docsDir

# process each page
echo "<pages>" > $tmpIndex 
cnt=${#pages[@]}
for (( i = 0 ; i < cnt ; i++ ))
do
    wget -P$tmpDir -E ${server}/wiki/${pages[$i]}   
    java org.apache.xalan.xslt.Process -IN $tmpDir/${pages[$i]}.html -XSL $xsl -OUT $docsDir/${pages[$i]}.html
    echo "<page>${pages[$i]}</page>" >> $tmpIndex
done
echo "</pages>" >> $tmpIndex

# get the css and create the index page
wget -P$docsDir -E ${server}/trac/css/trac.css        
java org.apache.xalan.xslt.Process -IN $tmpIndex -XSL $xsl -OUT $docsDir/wiki-index.html 

