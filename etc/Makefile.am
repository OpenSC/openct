# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

DISTCLEANFILES = openct.conf hotplug.openct init-script

EXTRA_DIST = Info.plist openct.conf.in hotplug.openct.in init-script.in \
		openct.usermap

noinst_DATA = openct.conf openct.usermap

noinst_SCRIPTS = init-script hotplug.openct

%: %.in
	sed -e "s#SBINDIR#${sbindir}#g" $^ > $@

install-data-local: hotplug.openct
	@set -e; if test -d $(DESTDIR)/etc/hotplug/usb; then \
		echo install -m 644 openct.usermap $(DESTDIR)/etc/hotplug/usb; \
		install -m 644 openct.usermap $(DESTDIR)/etc/hotplug/usb; \
		echo install -m 755 hotplug.openct $(DESTDIR)/etc/hotplug/usb/openct; \
		install -m 755 hotplug.openct $(DESTDIR)/etc/hotplug/usb/openct; \
	else \
		echo "*** Please install hotplug scripts and map manually" >&2; \
	fi
