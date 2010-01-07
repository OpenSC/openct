dnl -*- mode: m4; -*-

AC_PREREQ(2.60)

define([PACKAGE_VERSION_MAJOR], [0])
define([PACKAGE_VERSION_MINOR], [6])
define([PACKAGE_VERSION_FIX], [19])
define([PACKAGE_SUFFIX], [])

AC_INIT([openct],[PACKAGE_VERSION_MAJOR.PACKAGE_VERSION_MINOR.PACKAGE_VERSION_FIX[]PACKAGE_SUFFIX])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([${PACKAGE_NAME}], [${PACKAGE_VERSION}])

OPENCT_VERSION_MAJOR="PACKAGE_VERSION_MAJOR"
OPENCT_VERSION_MINOR="PACKAGE_VERSION_MINOR"
OPENCT_VERSION_FIX="PACKAGE_VERSION_FIX"

# LT Version numbers, remember to change them just *before* a release.
#   (Code changed:                      REVISION++)
#   (Oldest interface removed:          OLDEST++)
#   (Interfaces added:                  CURRENT++, REVISION=0)
OPENCT_LT_CURRENT="1"
OPENCT_LT_OLDEST="1"
OPENCT_LT_REVISION="0"
OPENCT_LT_AGE="$((${OPENCT_LT_CURRENT}-${OPENCT_LT_OLDEST}))"

AC_CONFIG_SRCDIR(src/ifd/ifdhandler.c)

AC_CANONICAL_HOST

# ct_socket_getcreds::SO_PEERCRED
AC_GNU_SOURCE	

AC_PROG_CC
PKG_PROG_PKG_CONFIG
AC_C_BIGENDIAN

AC_MSG_CHECKING([svn checkout])
if test -e "${srcdir}/packaged"; then
	svn_checkout="no"
else
	svn_checkout="yes"
fi
AC_MSG_RESULT([${svn_checkout}])

dnl Check for some target-specific stuff
case "${host}" in
	*-*-hpux*)
		CPPFLAGS="${CPPFLAGS} -D_HPUX_SOURCE -D_XOPEN_SOURCE_EXTENDED"
	;;
	*-*-solaris*)
		CPPFLAGS="${CPPFLAGS} -I/usr/local/include"
		LDFLAGS="${LDFLAGS} -L/usr/local/lib -R/usr/local/lib"
		need_dash_r="1"
	;;
	*-*-sunos4*)
		CPPFLAGS="${CPPFLAGS} -DSUNOS4"
	;;
	*-*-aix*)
		CPPFLAGS="${CPPFLAGS} -I/usr/local/include"
		LDFLAGS="${LDFLAGS} -L/usr/local/lib"
		if test "${LD}" != "gcc" -a -z "${blibpath}"; then
			blibpath="/usr/lib:/lib:/usr/local/lib"
		fi
	;;
	*-*-osf*)
		CPPFLAGS="${CPPFLAGS} -D_POSIX_PII_SOCKET"
	;;
	*-*-darwin*)
		LIBS="${LIBS} -Wl,-framework,CoreFoundation"
		if test "${GCC}" = "yes"; then
			CFLAGS="${CFLAGS} -no-cpp-precomp"
		fi
	;;
esac

AC_ARG_ENABLE(
	[strict],
	[AS_HELP_STRING([--enable-strict],[enable strict compile mode @<:@disabled@:>@])],
	,
	[enable_strict="no"]
)

AC_ARG_ENABLE(
	[pedantic],
	[AS_HELP_STRING([--enable-pedantic],[enable pedantic compile mode @<:@disabled@:>@])],
	,
	[enable_pedantic="no"]
)

AC_ARG_ENABLE(
	[doc],
	[AS_HELP_STRING([--enable-doc],[enable installation of documents @<:@disabled@:>@])],
	,
	[enable_doc="no"]
)

AC_ARG_ENABLE(
	[api-doc],
	[AS_HELP_STRING([--enable-api-doc],[enable generation and installation of api documents @<:@disabled@:>@])],
	,
	[enable_api_doc="no"]
)

AC_ARG_ENABLE(
	[pcsc],
	[AS_HELP_STRING([--enable-pcsc],[enable pcsc support @<:@disabled@:>@])],
	,
	[enable_pcsc="no"]
)

AC_ARG_ENABLE(
	[usb],
	[AS_HELP_STRING([--enable-usb],[enable usb support @<:@disabled@:>@])],
	,
	[enable_usb="no"]
)

AC_ARG_ENABLE(
	[debug],
	[AS_HELP_STRING([--enable-debug],[enable debug messages @<:@disabled@:>@])],
	[test "${enableval}" = "yes" && AC_DEFINE([DEBUG], [1], [Enable debug messages.])]
)

AC_ARG_ENABLE(
	[sunray],
	[AS_HELP_STRING([--enable-sunray],[enable sunray support @<:@disabled@:>@])],
	[test "${enableval}" = "yes" && AC_DEFINE([sunray], [1], [Enable sunray support.])]
)

AC_ARG_ENABLE(
	[sunrayclient],
	[AS_HELP_STRING([--enable-sunrayclient],[enable sunray client support @<:@disabled@:>@])],
	[test "${enableval}" = "yes" && AC_DEFINE(sunrayclient, 1, [Enable sunray client support.])]
)

AC_ARG_ENABLE(
	[non-privileged],
	[AS_HELP_STRING([--enable-non-privileged],[enable non privileged mode @<:@disabled@:>@])],
	[test "${enableval}" = "no" && ENABLE_NON_PRIVILEGED="#"],
	[ENABLE_NON_PRIVILEGED="#"]
)

AC_ARG_WITH(
	[bundle],
        [AS_HELP_STRING([--with-bundle=PATH],[install bundle file for pcsc-lite to PATH @<:@not installed@:>@])],
        [
		if test "${withval}" = "yes"; then
			bundledir="\$(libdir)"
		else
			bundledir="${withval}"
		fi
	],
	[bundledir=""]
)

AC_ARG_WITH(
	[udev],
        [AS_HELP_STRING([--with-udev=PATH],[install udev files into PATH @<:@not installed@:>@])],
        [
		if test "${withval}" = "yes"; then
			udevdir="\$(libdir)/udev"
		else
			udevdir="${withval}"
		fi
	],
	[udevdir=""]
)

AC_ARG_WITH(
	[hotplug],
        [AS_HELP_STRING([--with-hotplug=PATH],[install hotplug files into PATH @<:@not installed@:>@])],
        [
		if test "${withval}" = "yes"; then
			hotplugdir="\$(sysconfdir)/hotplug/usb"
		else
			hotplugdir="${withval}"
		fi
	],
	[hotplugdir=""]
)

AC_ARG_WITH(
	[ifddir],
	[AS_HELP_STRING([--with-ifddir],[ifd directory @<:@/usr/lib/ifd@:>@])],
	[ifddir="${withval}"],
	[ifddir="\${libdir}/ifd"]
)

AC_ARG_WITH(
	[apidocdir],
	[AS_HELP_STRING([--with-apidocdir],[put API documents at this directory @<:@HTMLDIR/api@:>@])],
	[apidocdir="${with_apidocdir}"],
	[apidocdir="\$(htmldir)/api"]
)

AC_ARG_WITH(
	[daemon-user],
	[AS_HELP_STRING([--with-daemon-user=USER],[use this user for daemon @<:@openctd@:>@])],
	[daemon_user="${withval}"],
	[daemon_user="openctd"]
)

AC_ARG_WITH(
	[daemon-groups],
	[AS_HELP_STRING([--with-daemon-groups=GROUPS],[use these comma separated group list for daemon @<:@usb@:>@])],
	[daemon_groups="${withval}"],
	[daemon_groups="usb"]
)

dnl Checks for programs.
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_PROG_SED
AC_PROG_MAKE_SET

AM_PROG_CC_C_O

dnl Add libtool support.
ifdef(
	[LT_INIT],
	[
		LT_INIT
	],
	[
		AC_PROG_LIBTOOL
	]
)

dnl These required for svn checkout
AC_ARG_VAR([XSLTPROC], [xsltproc utility])
AC_ARG_VAR([SVN], [subversion utility])
AC_ARG_VAR([WGET], [wget utility])
AC_ARG_VAR([WGET_OPTS], [wget options])
AC_ARG_VAR([TR], [tr utility])
AC_ARG_VAR([DOXYGEN], [tr utility])
AC_CHECK_PROGS([XSLTPROC],[xsltproc])
AC_CHECK_PROGS([SVN],[svn])
AC_CHECK_PROGS([WGET],[wget])
AC_CHECK_PROGS([TR],[tr])
AC_CHECK_PROGS([DOXYGEN],[doxygen])

test -z "${WGET_OPTS}" && WGET_OPTS="-nv"

dnl svn checkout dependencies
if test "${svn_checkout}" = "yes"; then
	AC_MSG_CHECKING([XSLTPROC requirement])
	if test -n "${XSLTPROC}"; then
		AC_MSG_RESULT([ok])
	else
		if test "${enable_doc}" = "yes"; then
			AC_MSG_ERROR([Missing XSLTPROC svn build with doc])
		else
			AC_MSG_WARN(["make dist" will not work])
		fi
	fi

	AC_MSG_CHECKING([DOXYGEN requirement])
	if test -n "${DOXYGEN}"; then
		AC_MSG_RESULT([ok])
	else
		if test "${enable_api_doc}" = "yes"; then
			AC_MSG_ERROR([Missing DOXYGEN svn build with api doc])
		else
			AC_MSG_WARN(["make dist" will not work])
		fi
	fi

	AC_MSG_CHECKING([svn doc build dependencies])
	if test -n "${SVN}" -a -n "${TR}" -a -n "${WGET}"; then
		AC_MSG_RESULT([ok])
	else
		if test "${enable_doc}" = "yes"; then
			AC_MSG_ERROR([Missing SVN, TR or WGET for svn doc build])
		else
			AC_MSG_WARN(["make dist" will not work])
		fi
	fi
fi

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([ \
	errno.h fcntl.h malloc.h stdlib.h string.h \
	strings.h sys/time.h unistd.h getopt.h \
	dlfcn.h sys/poll.h
])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME

dnl Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gettimeofday daemon])

dnl C Compiler features
AC_C_INLINE

dnl See if socket() is found from libsocket
AC_CHECK_LIB(
	[socket],
	[socket],
	[
		LIBS="${LIBS} -lsocket"
		AC_CHECK_LIB(
			[resolv],
			[res_query],
			[LIBS="${LIBS} -lresolv"]
		)
	]
)

AC_ARG_VAR([LTLIB_CFLAGS], [C compiler flags for libltdl])
AC_ARG_VAR([LTLIB_LIBS], [linker flags for libltdl])
if test -z "${LTLIB_LIBS}"; then
	AC_CHECK_LIB(
		[ltdl],
		[lt_dlopen],
		[LTLIB_LIBS="-lltdl"],
		[AC_MSG_ERROR([ltdl not found, please install libltdl and/or libtool])]
	)
fi
saved_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} ${LTLIB_CFLAGS}"
AC_CHECK_HEADER(
	[ltdl.h],
	,
	[AC_MSG_ERROR([ltdl.h not found, please install libltdl and/or libtool])]
)
CFLAGS="${saved_CFLAGS}"

AC_MSG_CHECKING([struct sockaddr_storage::ss_family])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[
#include <sys/types.h>
#include <sys/socket.h>
		]],
		[[
struct sockaddr_storage s;
s.ss_family = 1;
		]]
	)],
	[AC_MSG_RESULT([ok])],
	[AC_TRY_COMPILE(
		[
#include <sys/types.h>
#include <sys/socket.h>
		],
		[
struct sockaddr_storage s;
s.__ss_family = 1;
		],
		[
			AC_MSG_RESULT([hack])
			AC_DEFINE_UNQUOTED([ss_family], [__ss_family], [sockaddr_storage.ss_family wrapper])
		],
		[AC_MSG_ERROR([Invalid struct sockaddr_storage])]
	)]
)

dnl see if poll() is found from libpoll
AC_CHECK_LIB([poll], [poll], [LIBS="$LIBS -lpoll"])

if test "${enable_usb}" = "yes"; then
	PKG_CHECK_MODULES(
		[LIBUSB],
		[libusb],
		[AC_DEFINE([ENABLE_LIBUSB], [1], [Have libusb libraries and header files])],
		[AC_MSG_ERROR([Cannot detect libusb])]
	)
fi

if test "${enable_pcsc}" = "yes"; then
	case "$host" in
		*-*-darwin*)
			if test -z "$PCSC_LIBS" -a -z "$PCSC_LIBS"; then
				PCSC_MSG=yes
				PCSC_CFLAGS=""
				PCSC_LIBS="-Wl,-framework,PCSC"
			fi
		;;
		*)
			PKG_CHECK_MODULES(
				[PCSC],
				[libpcsclite],
				,
				[AC_MSG_ERROR([Cannot locate pcsc-lite])]
			)
		;;
	esac
fi

if test "${enable_usb}" = "yes"; then
	OPTIONAL_LIBUSB_CFLAGS="${LIBUSB_CFLAGS}"
	OPTIONAL_LIBUSB_LIBS="${LIBUSB_LIBS}"
fi

OPENCT_SOCKET_PATH='${localstatedir}/run/openct'
CPPFLAGS_OPENCT_SOCKET_PATH='-DOPENCT_SOCKET_PATH="\"$(OPENCT_SOCKET_PATH)\""'
CPPFLAGS_OPENCT_CONF_PATH='-DOPENCT_CONF_PATH="\"$(sysconfdir)/openct.conf\""'
CPPFLAGS_OPENCT_MODULES_PATH='-DOPENCT_MODULES_PATH="\"$(ifddir)\""'
CPPFLAGS_OPENCT_IFDHANDLER_PATH='-DOPENCT_IFDHANDLER_PATH="\"$(sbindir)/ifdhandler\""'
CPPFLAGS="${CPPFLAGS} \
	${CPPFLAGS_OPENCT_CONF_PATH} \
	${CPPFLAGS_OPENCT_SOCKET_PATH} \
	${CPPFLAGS_OPENCT_MODULES_PATH} \
	${CPPFLAGS_OPENCT_IFDHANDLER_PATH}"

AC_CREATE_STDINT_H([src/include/openct/types.h])

if test "${enable_pedantic}" = "yes"; then
	enable_strict="yes";
	CFLAGS="${CFLAGS} -pedantic"
fi
if test "${enable_strict}" = "yes"; then
	CFLAGS="${CFLAGS} -Wall -Wextra"
fi

pkgconfigdir="\$(libdir)/pkgconfig"
openctincludedir="\$(includedir)/openct"

AC_SUBST([pkgconfigdir])
AC_SUBST([openctincludedir])
AC_SUBST([apidocdir])
AC_SUBST([bundledir])
AC_SUBST([ifddir])
AC_SUBST([udevdir])
AC_SUBST([hotplugdir])
AC_SUBST([daemon_user])
AC_SUBST([daemon_groups])
AC_SUBST([OPENCT_VERSION_MAJOR])
AC_SUBST([OPENCT_VERSION_MINOR])
AC_SUBST([OPENCT_VERSION_FIX])
AC_SUBST([OPENCT_LT_CURRENT])
AC_SUBST([OPENCT_LT_REVISION])
AC_SUBST([OPENCT_LT_AGE])
AC_SUBST([OPENCT_LT_OLDEST])
AC_SUBST([OPTIONAL_LIBUSB_CFLAGS])
AC_SUBST([OPTIONAL_LIBUSB_LIBS])
AC_SUBST([OPENCT_SOCKET_PATH])
AC_SUBST([ENABLE_NON_PRIVILEGED])

AM_CONDITIONAL([SVN_CHECKOUT], [test "${svn_checkout}" = "yes"])
AM_CONDITIONAL([ENABLE_DOC], [test "${enable_doc}" = "yes"])
AM_CONDITIONAL([ENABLE_API_DOC], [test "${enable_api_doc}" = "yes"])
AM_CONDITIONAL([ENABLE_BUNDLES], [test -n "${bundledir}"])
AM_CONDITIONAL([ENABLE_UDEV], [test -n "${udevdir}"])
AM_CONDITIONAL([ENABLE_HOTPLUG], [test -n "${hotplugdir}"])
AM_CONDITIONAL([ENABLE_PCSC], [test "${enable_pcsc}" = "yes"])

AC_CONFIG_FILES([
	Makefile
	doc/Makefile
	doc/doxygen.conf
	doc/nonpersistent/Makefile
	etc/Makefile
	macos/Makefile
	src/Makefile
	src/ct/Makefile
	src/ct/libopenct.pc
	src/ctapi/Makefile
	src/ifd/Makefile
	src/include/Makefile
	src/include/openct/Makefile
	src/pcsc/Makefile
	src/tools/Makefile
	src/tools/openct-tool.1
])
AC_OUTPUT

cat <<EOF

OpenCT has been configured with the following options:


Version:                 ${PACKAGE_VERSION}
Libraries:               $(eval eval eval echo "${libdir}")

doc support:             ${enable_doc}
api doc support:         ${enable_api_doc}
usb support:             ${enable_usb}
pcsc support:            ${enable_pcsc}
non-privileged support:  ${ENABLE_NON_PRIVILEGED}

Host:                    ${host}
Compiler:                ${CC}
Preprocessor flags:      ${CPPFLAGS}
Compiler flags:          ${CFLAGS}
Linker flags:            ${LDFLAGS}
Libraries:               ${LIBS}

LIBUSB_CFLAGS:           ${LIBUSB_CFLAGS}
LIBUSB_LIBS:             ${LIBUSB_LIBS}
PCSC_CFLAGS:             ${PCSC_CFLAGS}
PCSC_LIBS:               ${PCSC_LIBS}

bundledir:               ${bundledir}
ifddir:                  ${ifddir}
udevdir:                 ${udevdir}
hotplugdir:              ${hotplugdir}

daemon user:             ${daemon_user}
daemon groups:           ${daemon_groups}

EOF
