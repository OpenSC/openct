dnl -*- mode: m4; -*-
dnl Process this file with autoconf to produce a configure script.

# Require autoconf 2.52
AC_PREREQ(2.52)

AC_INIT(LICENSE)
AM_INIT_AUTOMAKE(openct, CVS)
AM_CONFIG_HEADER(config.h)

AC_CONFIG_SRCDIR(src/ifd/ifdhandler.c)

# LT Version numbers, remember to change them just *before* a release.
#   (Code changed:                      REVISION++)
#   (Interfaces added/removed/changed:  CURRENT++, REVISION=0)
#   (Interfaces added:                  AGE++)
#   (Interfaces removed:                AGE=0)
OPENCT_LT_CURRENT=0
OPENCT_LT_REVISION=1
OPENCT_LT_AGE=0

AC_SUBST(OPENCT_LT_CURRENT)
AC_SUBST(OPENCT_LT_REVISION)
AC_SUBST(OPENCT_LT_AGE)

AC_CANONICAL_HOST
AC_PROG_CC
AC_C_BIGENDIAN

dnl Check for some target-specific stuff
case "$host" in
*-*-hpux*)
	CPPFLAGS="$CPPFLAGS -D_HPUX_SOURCE -D_XOPEN_SOURCE_EXTENDED"
	;;
*-*-solaris*)
	CPPFLAGS="$CPPFLAGS -I/usr/local/include"
	LDFLAGS="$LDFLAGS -L/usr/local/lib -R/usr/local/lib"
	need_dash_r=1
	;;
*-*-sunos4*)
	CPPFLAGS="$CPPFLAGS -DSUNOS4"
	;;
*-*-aix*)
	CPPFLAGS="$CPPFLAGS -I/usr/local/include"
	LDFLAGS="$LDFLAGS -L/usr/local/lib"
	if (test "$LD" != "gcc" && test -z "$blibpath"); then
		blibpath="/usr/lib:/lib:/usr/local/lib"
	fi
	;;
*-*-osf*)
	CPPFLAGS="$CPPFLAGS -D_POSIX_PII_SOCKET"
	;;
*-*-linux*)
	AC_CHECK_MEMBER(struct usbdevfs_ctrltransfer.bRequestType,
		[AC_DEFINE(LINUX_NEWUSB,1,[new usb structure])],
		[],
		[#include <linux/usbdevice_fs.h>])
	;;
*-*-darwin*)
	LIBS="$LIBS -lobjc"
	if test "$GCC" = "yes"; then
		CFLAGS="$CFLAGS -no-cpp-precomp"
	fi
	with_bundles=yes
	;;
esac

AC_ARG_WITH(common-dir,
	[  --with-common-dir=PATH  Specify path for common installation libraries],
	[
		if test "x$withval" != "xno" ; then
			trycommondir=$withval
		fi
	]
)

saved_LDFLAGS="$LDFLAGS"
saved_CPPFLAGS="$CPPFLAGS"
AC_CACHE_CHECK([for common directory], ac_cv_commondir, [
	for commondir in $trycommondir "" /usr/local /usr/pkg /opt ; do
		# Skip directories if they don't exist
		if test ! -z "$commondir" -a ! -d "$commondir/" ; then
			continue;
		fi
		CPPFLAGS="$saved_CPPFLAGS"
		LDFLAGS="$saved_LDFLAGS"

		if test ! -z "$commondir" -a "x$commondir" != "x/usr"; then
			LDFLAGS="-L$commondir/lib $saved_LDFLAGS"
			if test ! -z "$need_dash_r" ; then
				LDFLAGS="-R$commondir/lib $LDFLAGS"
			fi
			CPPFLAGS="-I$commondir/include $saved_CPPFLAGS"
		fi
		break;
	done

	if test -z "$commondir" ; then
		commondir="(system)"
	fi
	ac_cv_commondir=$commondir
])

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

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([errno.h fcntl.h malloc.h stdlib.h string.h strings.h sys/time.h unistd.h getopt.h dlfcn.h sys/poll.h])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME

dnl Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gettimeofday])

dnl C Compiler features
AC_C_INLINE
if test "$GCC" = "yes"; then
	CFLAGS="-Wall $CFLAGS"
fi

AC_SUBST(LIBDL)
AC_CHECK_LIB(dl, dlopen, [LIBDL="$LIBDL -ldl" ac_cv_func_dlopen_ldl=yes], ac_cv_func_dlopen_ldl=no)

#
#  We check for various libraries
#  - SysVr4 style of "-lsocket" at first (unless in libc)
#    The hallmark is  connect()  routine (we presume)
#
AC_SUBST(LIBSOCKET)dnl
ac_cv_libsocket_both=1
AC_CHECK_FUNC(connect, ac_cv_libsocket_both=0)
AC_CHECK_FUNC(gethostbyname, ac_cv_libsocket_both=0)
if test "$ac_cv_libsocket_both" = 1 ; then
  # Check cache
  if test "$ac_cv_func_socket_lsocket" = yes ; then
    AC_MSG_RESULT([need -lsocket library (cached)])
    LIBSOCKET="-lsocket"
    if test "$ac_cv_func_gethostbyname_lnsl" = yes ; then
	LIBSOCKET="-lsocket -lnsl"
    fi
  else
    # Well, will this work ?  SysVR4, but not Sun Solaris ?
    AC_CHECK_LIB(socket, connect, [LIBSOCKET="-lsocket"
				   ac_cv_func_socket_lsocket=yes],
				   ac_cv_func_socket_lsocket=no)
    if test "$ac_cv_func_socket_lsocket" = yes ; then
      t_oldLibs="$LIBS"
      LIBS="$LIBS -lsocket $LIBRESOLV"
      AC_TRY_LINK([],[gethostbyname();], ,[
	  LIBS="$LIBS -lnsl" # Add this Solaris library..
	  AC_TRY_LINK([],[gethostbyname();],[
			LIBSOCKET="-lsocket -lnsl"
			ac_cv_func_gethostbyname_lnsl=yes
		], [
		   AC_MSG_ERROR([Weird, '$LIBS' not enough to find  gethostbyname() ?!])
		])
	])
      LIBS="$t_oldLibs"
    fi
  fi
fi

AC_SUBST(LIBRESOLV)dnl
if test "x$LIBRESOLV" = "x"; then
  # Ok, No  -lresolv,  is this enough for the  _res  to appear ?
  t_oldLibs="$LIBS"
  LIBS="$LIBS $LIBSOCKET"
  ac_cv_var__res_options=no
  # This following is for IRIX6.4, and I sincerely hope it
  # will not fail on other systems...  It did! It did!
  # Many systems don't have idemponent headers, they need specific
  # includes before latter ones, or the latter ones won't be successful...
  AC_TRY_LINK([#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>],
	      [_res.options = RES_INIT;],
		ac_cv_var__res_options=yes);
  if test "$ac_cv_var__res_options" != "yes"; then
    LIBRESOLV="-lresolv"
  fi
  LIBS="$t_oldLibs"
fi

if test "x$LIBRESOLV" = "x"; then
  # glibc 2.2.4 and libresolv is difficult
  #  -	gethostbyname won't require lresolv because all
  #	queries are made through the nsswitch
  #  -	checking for the variable _res will not require
  #	lresolv because _res is defined somewhere
  AC_CHECK_LIB(resolv, res_query, [LIBRESOLV=-lresolv])
  LIBS="$t_oldLibs"
fi

# See about the routines that possibly exist at the libraries..
LIBS="$t_oldLibs $LIBSOCKET"
AC_CHECK_FUNCS(socket socketpair)
LIBS="$t_oldLibs"

if test "$ac_cv_func_socket" = no -a "$LIBSOCKET" != ""; then
  LIBS="$LIBS $LIBSOCKET"
  AC_TRY_LINK([],[socket();], ac_cv_func_socket=yes)
  if test $ac_cv_func_socket = yes; then
    AC_DEFINE(HAVE_SOCKET, 1, [Have socket])
  fi
  LIBS="$t_oldLibs"
fi
if test "$ac_cv_func_socketpair" = no -a "$LIBSOCKET" != ""; then
  LIBS="$LIBS $LIBSOCKET"
  AC_TRY_LINK([],[socketpair();], ac_cv_func_socketpair=yes)
  if test $ac_cv_func_socketpair = yes; then
    AC_DEFINE(HAVE_SOCKETPAIR, 1, [Have socketpair])
  fi
  LIBS="$t_oldLibs"
fi

# Always link lib{socket,nsl,resolv) if found, sigh.
LIBS="$LIBS $LIBSOCKET $LIBRESOLV"

AC_CHECK_LIB(poll, poll, [LIBS="$LIBS -lpoll" ac_cv_func_poll_lpoll=yes], ac_cv_func_poll_lpoll=no)

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

USB_MSG=no
AC_SUBST(LIBUSB)
AC_CHECK_LIB(usb, usb_init,
	[
	      	LIBUSB="$LIBUSB -lusb"
		AC_DEFINE(HAVE_LIBUSB,1, [Libusb is available])
		USB_MSG=yes
	])

if test "x$prefix" = xNONE; then
  prefix=$ac_default_prefix
fi
if test "x$exec_prefix" = xNONE; then
  exec_prefix=$prefix
fi

dnl Enable/disable debugging messages.
AC_ARG_ENABLE(debug,
[  --enable-debug          enable debug messages. [default=no]],
	, enable_debug=no)
if (test x$enable_debug = xyes); then
	AC_DEFINE(DEBUG, 1, [Enable debug messages.])
fi

CPPFLAGS="-I\${top_builddir}/src/include -I\${top_srcdir}/src/include $CPPFLAGS"
AM_CONDITIONAL(HAVE_BUNDLES, test "x$with_bundles" = "xyes")

openct_etc_path=`eval echo ${sysconfdir}` ; openct_etc_path=`eval echo ${openct_etc_path}`
AC_DEFINE_UNQUOTED(OPENCT_ETC_PATH, "$openct_etc_path", [etc path for libopenct])
openct_conf_path=`eval echo ${openct_etc_path}/openct.conf`
AC_DEFINE_UNQUOTED(OPENCT_CONF_PATH, "$openct_conf_path", [default config file for libopenct])

openct_modules_path=`eval echo ${libdir}` ; openct_modules_path=`eval echo ${openct_modules_path}/ifd`
AC_DEFINE_UNQUOTED(OPENCT_MODULES_PATH, "$openct_modules_path", [modules path for libopenct])

a=`eval echo $localstatedir`; b=`eval echo ${prefix}/var`
if test "$a" = "$b"; then localstatedir="/var"; fi	# No one has touched the default settings, so use /var
openct_socket_path=`eval echo ${localstatedir}` ; openct_socket_path=`eval echo ${openct_socket_path}/run/openct`
AC_DEFINE_UNQUOTED(OPENCT_SOCKET_PATH, "$openct_socket_path", [socket path for libopenct])

openct_status_path=`eval echo ${openct_socket_path}` ; openct_status_path=`eval echo ${openct_status_path}/status`
AC_DEFINE_UNQUOTED(OPENCT_STATUS_PATH, "$openct_status_path", [status path for libopenct])

openct_ifdhandler_path=`eval echo ${sbindir}` ; openct_ifdhandler_path=`eval echo ${openct_ifdhandler_path}/ifdhandler`
AC_DEFINE_UNQUOTED(OPENCT_IFDHANDLER_PATH, "$openct_ifdhandler_path", [ifdhandler path for libopenct])

pkgdata=`eval echo ${datadir}/${PACKAGE}` ; pkgdata=`eval echo ${pkgdata}`
AC_SUBST(pkgdata)

sysincludedir="${exec_prefix}/include"
AC_SUBST(sysincludedir)

AC_OUTPUT([
Makefile
aclocal/Makefile
doc/Makefile
etc/Makefile
src/Makefile
src/control/Makefile
src/ct/Makefile
src/ct/libopenct.pc
src/ctapi/Makefile
src/ifd/Makefile
src/include/Makefile
src/include/openct/Makefile
src/pcsc/Makefile
src/tools/Makefile
])

AC_CREATE_STDINT_H(src/include/openct/types.h)

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
echo ""

if test "${USB_MSG}" != "yes"; then
  echo "Without libusb coldplugging will not work."
  echo "To use usb devices, your hotplugging needs to be"
  echo "configured and you need to plug in any device "
  echo "after the system has started (i.e. the init script ran)"
fi
