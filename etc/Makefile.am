MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

SUFFIXES = .in

sources = openct.udev.in openct.udev.modalias.in openct.udev.compat openct_usb.mdev.in \
	openct.conf.in openct.hald.in reader.conf.in \
	init-script.in openct.hald.in openct_usb.mdev.in \
	openct_usb.in openct_pcmcia.in openct_serial.in
generated_data = openct.udev openct.udev.modalias openct_usb.mdev \
	openct.conf openct.hald reader.conf 
noinst_generated_scripts = init-script openct.hald openct_usb.mdev
udev_generated_scripts = openct_usb openct_pcmcia openct_serial

CLEANFILES = $(generated_data) $(noinst_generated_scripts) $(udev_generated_scripts)

nodist_noinst_SCRIPTS = $(noinst_generated_scripts)
dist_noinst_DATA = Info.plist openct.fdi openct-policy.fdi openct-disable.fdi $(sources)
nodist_noinst_DATA = $(generated_data)

if ENABLE_UDEV
nodist_udev_SCRIPTS = $(udev_generated_scripts)
else
nodist_noinst_SCRIPTS += $(udev_generated_scripts)
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
		-e 's|@OPENCT_SOCKET_PATH[@]|@OPENCT_SOCKET_PATH@|g' \
		-e 's|@ENABLE_NON_PRIVILEGED[@]|@ENABLE_NON_PRIVILEGED@|g' \
		-e 's|@daemon_user[@]|@daemon_user@|g' \
		-e 's|@daemon_groups[@]|@daemon_groups@|g' \
		< "$<" > "$@"
