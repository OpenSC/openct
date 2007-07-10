# Process this file with automake to create Makefile.in

MAINTAINERCLEANFILES = Makefile.in

DISTCLEANFILES = openct.conf init-script openct.hald reader.conf \
		openct_usb openct_pcmcia openct_serial

EXTRA_DIST = Info.plist openct.usermap openct.udev openct.udev.modalias \
	openct.conf.in init-script.in openct.hald.in reader.conf.in \
	openct_usb.in openct_pcmcia.in openct_serial.in

noinst_DATA = openct.conf reader.conf

noinst_SCRIPTS = openct_usb openct_pcmcia openct_serial init-script openct.hald

openct_usb: openct_usb.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@
	chmod 755 $@

openct_pcmcia: openct_pcmcia.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@
	chmod 755 $@

openct_serial: openct_serial.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@
	chmod 755 $@

reader.conf: reader.conf.in
	sed -e "s#LIBDIR#${libdir}#g" < $^ > $@

init-script: init-script.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@
	chmod 755 $@

%: %.in
	sed -e "s#SBINDIR#${sbindir}#g" < $^ > $@

install-data-local: openct_usb openct_pcmcia openct_serial
	@set -e; if test -w $(DESTDIR)/lib/udev; then \
		echo install -m 755 openct_usb $(DESTDIR)/lib/udev/openct_usb; \
		install -m 755 openct_usb $(DESTDIR)/lib/udev/openct_usb; \
		echo install -m 755 openct_pcmcia $(DESTDIR)/lib/udev/openct_pcmcia; \
		install -m 755 openct_pcmcia $(DESTDIR)/lib/udev/openct_pcmcia; \
		echo install -m 755 openct_serial $(DESTDIR)/lib/udev/openct_serial; \
		install -m 755 openct_serial $(DESTDIR)/lib/udev/openct_serial; \
	else \
		echo "*** Please install udev scripts manually" >&2; \
	fi
	@set -e; if test -w $(DESTDIR)/$(sysconfdir)/hotplug/usb; then \
		echo install -m 644 openct.usermap $(DESTDIR)$(sysconfdir)/hotplug/usb; \
		install -m 644 openct.usermap $(DESTDIR)$(sysconfdir)/hotplug/usb; \
	else \
		echo "*** Please install hotplug map file manually, if needed" >&2; \
	fi
