MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

CLEANFILES = openct.conf init-script openct.hald reader.conf \
		openct_usb openct_pcmcia openct_serial

SUFFIXES = .in

noinst_DATA = openct.conf reader.conf
noinst_SCRIPTS = init-script openct.hald
dist_noinst_DATA = Info.plist openct.udev openct.udev.modalias \
	openct.conf.in init-script.in openct.hald.in reader.conf.in \
	openct_usb.in openct_pcmcia.in openct_serial.in openct.fdi

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


.in:
	$(SED) -e "s#SBINDIR#$(sbindir)#g" -e "s#LIBDIR#$(libdir)#g" < "$^" > "$@"
