dnl -*- mode: m4; -*-
dnl Process this file with autoconf to produce a configure script.

# Require autoconf 2.52
AC_PREREQ(2.52)

AC_INIT(LICENSE)
AM_INIT_AUTOMAKE(openct, CVS)
AM_CONFIG_HEADER(config.h)

AC_CONFIG_SRCDIR(src/ifd/ifdhandler.c)

AC_CANONICAL_HOST
case "$host" in
	*linux*)
		AC_CHECK_MEMBER(struct usbdevfs_ctrltransfer.bRequestType,
				[AC_DEFINE(LINUX_NEWUSB,1,[new usb structure])],
				[],
				[#include <linux/usbdevice_fs.h>])
	;;
esac

dnl Checks for programs.
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_LEX
AC_PATH_PROG(TEST_MINUS_S_SH, bash)
AC_PATH_PROG(TEST_MINUS_S_SH, ksh)
AC_PATH_PROG(TEST_MINUS_S_SH, sh)

dnl Add libtool support.
AM_PROG_LIBTOOL
dnl Automatically update the libtool script if it becomes out-of-date.
#AC_SUBST(LIBTOOL_DEPS)

PCSC_MSG=no
pcsc_path=/usr
AC_SUBST(CFLAGS_PCSC)
AC_SUBST(LIBPCSC)

case "$host" in
*-*-darwin*)
	PCSC_MSG=yes
	CFLAGS_PCSC=""
	LIBPCSC="-Wl,-framework,PCSC"
	;;
esac

saved_LIBS="$LIBS"
saved_LDFLAGS="$LDFLAGS"
saved_CPPFLAGS="$CPPFLAGS"
AC_ARG_WITH(pcsclite,
	[  --with-pcsclite=PATH    use PC/SC Lite in PATH],
	[pcsc_path=$withval])
if test "x$pcsc_path" = "xno"; then
	PCSC_MSG="no"
fi
if test "x$pcsc_path" != "xno" -a "x$PCSC_MSG" != "xyes"; then
	AC_MSG_CHECKING(for PC/SC-Lite support)
	for pcscdir in /pcsc ""; do
		CPPFLAGS="$saved_CPPFLAGS"
		LDFLAGS="$saved_LDFLAGS"
		LIBS="-lpcsclite $saved_LIBS"
		CFLAGS_PCSC=""

		for pcsc_libdir in $pcsc_path/lib$pcscdir \
			      $pcsc_path$pcscdir/lib \
			      $pcsc_path$pcscdir; do
			if test -d $pcsc_libdir; then
				if test -n "${need_dash_r}"; then
					LDFLAGS="-R${pcsc_libdir}/ ${LDFLAGS}"
				fi
				LDFLAGS="-L${pcsc_libdir} ${LDFLAGS}"
			fi
		done

		for pcsc_incdir in $pcsc_path/include$pcscdir \
			      $pcsc_path$pcscdir/include \
			      $pcsc_path$pcscdir; do
		      if test -d $pcsc_incdir; then
				CFLAGS_PCSC="-I${pcsc_incdir}"
				break;
		      fi
		done

		CPPFLAGS="${CFLAGS_PCSC} ${CPPFLAGS}"
		AC_TRY_LINK([#include <stdlib.h>
#include <winscard.h>],[SCardEstablishContext(0, NULL, NULL, NULL);], ac_cv_lib_pcsclite_SCardEstablishContext=yes)
		if test "x$ac_cv_lib_pcsclite_SCardEstablishContext" = "xyes"; then
			PCSC_MSG=yes
			break;
		fi
		CPPFLAGS="$saved_CPPFLAGS"
		LDFLAGS="$saved_LDFLAGS"
		LIBS="$saved_LIBS"
		CFLAGS_PCSC=""
	done
	AC_MSG_RESULT($PCSC_MSG)
	if test "x$PCSC_MSG" = "xyes" ; then
		LIBPCSC="-lpcsclite"
		CPPFLAGS="$saved_CPPFLAGS"
		LIBS="$saved_LIBS"
	fi
fi
AM_CONDITIONAL(HAVE_PCSCLITE, test "x$PCSC_MSG" = "xyes")
if test "x$PCSC_MSG" = "xyes"; then
	AC_DEFINE(HAVE_PCSCLITE, 1, [Have PC/SC Lite])
else
	LIBPCSC=""
	CFLAGS_PCSC=""
fi

AC_SUBST(LIBDL)
AC_CHECK_LIB(dl, dlopen,
	     [LIBDL="$LIBDL -ldl" ac_cv_func_dlopen_ldl=yes],
	     ac_cv_func_dlopen_ldl=no)

USB_MSG=no
AC_SUBST(LIBUSB)
AC_CHECK_LIB(usb, usb_init,
	[
	      	LIBUSB="$LIBUSB -lusb"
		AC_DEFINE(HAVE_LIBUSB,1, [Libusb is available])
		USB_MSG=yes
	])

AC_OUTPUT([
Makefile
aclocal/Makefile
doc/Makefile
etc/Makefile
include/Makefile
include/openct/Makefile
src/Makefile
src/control/Makefile
src/ct/Makefile
src/ct/libopenct.pc
src/ctapi/Makefile
src/ifd/Makefile
src/pcsc/Makefile
src/tools/Makefile
])

echo ""
echo "OpenCT has been configured with the following options"

echo ""
echo "User binaries:       ${bindir}"
echo "Configuration files: ${sysconfdir}"

echo ""
echo "Host:                ${host}"
echo "Compiler:            ${CC}"
echo "Compiler flags:      ${CFLAGS}"
echo "Preprocessor flags:  ${CPPFLAGS}"
echo "Linker flags:        ${LDFLAGS}"
echo "Libraries:           ${LIBS}"

echo ""
echo "PC/SC support:       ${PCSC_MSG}"
echo "Libusb used:         ${USB_MSG}"

if test "${USB_MSG}" != "yes"
then
	echo "without libusb coldplugging will not work."
	echo "to use usb devices, your hotplugging needs to be"
	echo "configured and you need to plug in any device "
	echo "after the system has started (i.e. the init script ran)"
fi
