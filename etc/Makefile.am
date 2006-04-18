# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

DISTCLEANFILES = openct.conf hotplug.openct init-script openct.hald reader.conf

EXTRA_DIST = Info.plist openct.usermap openct.udev \
	openct.conf.in hotplug.openct.in init-script.in \
	openct.hald.in reader.conf.in

noinst_DATA = openct.conf reader.conf

noinst_SCRIPTS = hotplug.openct init-script openct.hald

hotplug.openct: hotplug.openct.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@
	chmod 755 hotplug.openct

reader.conf: reader.conf.in
	sed -e "s#LIBDIR#${libdir}#g" < $^ > $@

init-script: init-script.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@
	chmod 755 init-script

%: %.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@

install-data-local: hotplug.openct
	@set -e; if test -w $(DESTDIR)$(sysconfdir)/hotplug/usb; then \
		echo install -m 644 openct.usermap $(DESTDIR)$(sysconfdir)/hotplug/usb; \
		install -m 644 openct.usermap $(DESTDIR)$(sysconfdir)/hotplug/usb; \
		echo install -m 755 hotplug.openct $(DESTDIR)$(sysconfdir)/hotplug/usb/openct; \
		install -m 755 hotplug.openct $(DESTDIR)$(sysconfdir)/hotplug/usb/openct; \
	else \
		echo "*** Please install hotplug scripts and map manually" >&2; \
	fi
