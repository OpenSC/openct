MAINTAINERCLEANFILES=Makefile.in

DISTCLEANFILES=openct.conf hotplug.openct init-script

EXTRA_DIST = openct.conf.in hotplug.openct.in init-script.in

sysconf_DATA = openct.conf

sbin_SCRIPTS = hotplug.openct

initdir=@sysconfdir@/init.d

init_SCRIPTS = init-script

%: %.in
	sed -e "s#SBINDIR#${sbindir}#g" $(srcdir)/$^ > $@
