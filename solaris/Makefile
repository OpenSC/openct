PACKAGE=OSCopenct
PACKAGE_NAME=openct
VERSION=CVS
PWD=pwd
CONFIGURE_PREFIX=$(PWD:sh)/..
CONFIGURE=${CONFIGURE_PREFIX}/configure
CONFIGURE_ARGS=--prefix=/usr --sysconfdir=/etc/openct
CONFIG_GUESS=${CONFIGURE_PREFIX}/config.guess
UNAME_ARCH=/sbin/uname -p
PLATFORM = $(CONFIG_GUESS:sh)
ARCH = $(UNAME_ARCH:sh)
PKG_NAME = OSCopenct

build:
	@echo "Setup platform specific build directory ${PLATFORM}"
	mkdir -p build-${PLATFORM}
	( cd build-${PLATFORM}; CC=cc ${CONFIGURE} ${CONFIGURE_ARGS}; make )

dist:
	@echo "Setup platform specific dist directory dist-${PLATFORM}"
	mkdir -p dist-${PLATFORM}
	@echo "Performing Installing in dist directory"
	( cd build-${PLATFORM}; make DESTDIR=`pwd`/../dist-${PLATFORM} install )

package:
	@echo "Setup package meta files"
	-cp proto dist-${PLATFORM}
	-sed "s|@ARCH@|${ARCH}|" <checkinstall.in >dist-${PLATFORM}/checkinstall
	-sed -e "s|@ARCH@|${ARCH}|" -e "s|@VERSION@|${VERSION}|" \
		<pkginfo.in >dist-${PLATFORM}/pkginfo
	-cp postinstall dist-${PLATFORM}
	-cp preremove dist-${PLATFORM}
	@echo "Setup package distribution"
	mkdir -p dist-${PLATFORM}/lib/svc/method
	-cp openct-fabric-dist dist-${PLATFORM}/lib/svc/method/openct-fabric
	mkdir -p dist-${PLATFORM}/var/svc/manifest/system/device
	-cp devices-openct-fabric.xml-dist dist-${PLATFORM}/var/svc/manifest/system/device/devices-openct-fabric.xml
	mkdir -p dist-${PLATFORM}/usr/sbin
	-cp openct-hotplug-dist dist-${PLATFORM}/usr/sbin/openct-hotplug
	mkdir -p dist-${PLATFORM}/etc/openct
	-cp openct.conf-dist dist-${PLATFORM}/etc/openct/openct.conf
	@echo "Creating package"
	( \
		cd dist-${PLATFORM}; \
		pkgmk -o -r . -d . -f proto; \
		pkgtrans -s . ../${PKG_NAME}-${VERSION}-${PLATFORM}.pkg ${PKG_NAME} \
	)

clean:
	rm -rf build-${PLATFORM}
	rm -rf dist-${PLATFORM}
