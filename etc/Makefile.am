# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

DISTCLEANFILES = openct.conf hotplug.openct init-script

EXTRA_DIST = Info.plist openct.usermap \
	openct.conf.in hotplug.openct.in init-script.in

noinst_DATA = openct.conf

noinst_SCRIPTS = hotplug.openct init-script

%: %.in
	sed -e "s#SBINDIR#${sbindir}#g" $^ > $@

install-data-local: hotplug.openct
	@set -e; if test -d $(DESTDIR)$(sysconfdir)/hotplug/usb; then \
		echo install -m 644 openct.usermap $(DESTDIR)$(sysconfdir)/hotplug/usb; \
		install -m 644 openct.usermap $(DESTDIR)$(sysconfdir)/hotplug/usb; \
		echo install -m 755 hotplug.openct $(DESTDIR)$(sysconfdir)/hotplug/usb/openct; \
		install -m 755 hotplug.openct $(DESTDIR)$(sysconfdir)/hotplug/usb/openct; \
	else \
		echo "*** Please install hotplug scripts and map manually" >&2; \
	fi
