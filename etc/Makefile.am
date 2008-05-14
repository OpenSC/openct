MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

CLEANFILES = openct.conf init-script openct.hald reader.conf \
		openct_usb openct_usb.mdev openct_pcmcia openct_serial

SUFFIXES = .in

noinst_SCRIPTS = init-script openct.hald openct_usb.mdev
dist_noinst_DATA = Info.plist openct.udev openct.udev.modalias \
	openct.conf.in init-script.in openct.hald.in reader.conf.in \
	openct_usb.in openct_usb.mdev.in openct_pcmcia.in openct_serial.in openct.fdi

if ENABLE_UDEV
udev_SCRIPTS = openct_usb openct_pcmcia openct_serial
else
dist_noinst_SCRIPTS = openct_usb openct_pcmcia openct_serial
endif

if ENABLE_HOTPLUG
hotplug_DATA = openct.usermap
else
dist_noinst_DATA += openct.usermap
endif


sysconf_DATA=#required in order to create dir
install-exec-hook:	install-sysconfDATA openct.conf
	if [ -f "$(DESTDIR)$(sysconfdir)/openct.conf" ]; then \
		$(INSTALL_DATA) openct.conf "$(DESTDIR)$(sysconfdir)/openct.conf.new"; \
	else \
		$(INSTALL_DATA) openct.conf "$(DESTDIR)$(sysconfdir)/openct.conf"; \
	fi

.in:
	sed \
		-e 's|@bindir[@]|$(bindir)|g' \
		-e 's|@sbindir[@]|$(sbindir)|g' \
		-e 's|@libdir[@]|$(libdir)|g' \
		-e 's|@udevdir[@]|$(udevdir)|g' \
		-e 's|@localstatedir[@]|$(localstatedir)|g' \
		-e 's|@OPENCT_SOCKET_PATH[@]|$(OPENCT_SOCKET_PATH)|g' \
		< "$<" > "$@"
