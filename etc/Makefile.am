MAINTAINERCLEANFILES=Makefile.in

DISTCLEANFILES=openct.conf hotplug.openct init-script

EXTRA_DIST = openct.conf.in hotplug.openct.in init-script.in

noinst_DATA = openct.conf

sbin_SCRIPTS = hotplug.openct

noinst_SCRIPTS = init-script

%: %.in
	sed -e "s#SBINDIR#${sbindir}#g" $(srcdir)/$^ > $@


