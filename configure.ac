AC_INIT(src/ifd/init.c)
AC_CONFIG_AUX_DIR(tools)
AM_INIT_AUTOMAKE(openct, CVS)
AM_CONFIG_HEADER(include/config.h)

AM_PROG_LIBTOOL

PCSC_MSG=no
pcsc_path=/usr
AC_SUBST(CFLAGS_PCSC)
AC_SUBST(LIBPCSC)
found_pcsclite=no
saved_LIBS="$LIBS"
saved_LDFLAGS="$LDFLAGS"
saved_CPPFLAGS="$CPPFLAGS"
AC_ARG_WITH(pcsclite,
        [  --with-pcsclite=PATH    use PC/SC Lite in PATH],
        [pcsc_path=$withval])

if test "$pcsc_path" != "no"; then
        AC_MSG_CHECKING(for PC/SC-Lite support)
        for pcscdir in /pcsc ""; do
                LDFLAGS="$saved_LDFLAGS"
                CPPFLAGS="$saved_CPPFLAGS"
                CFLAGS_PCSC=
                LIBS="-lpcsclite -lpthread $saved_LIBS"

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
                        AC_DEFINE(HAVE_PCSCLITE, 1, [Have PC/SC Lite])
                        PCSC_MSG=yes
                        break;
                fi
                CPPFLAGS="$saved_CPPFLAGS"
                LDFLAGS="$saved_LDFLAGS"
                LIBS="$saved_LIBS"
        done
        AC_MSG_RESULT($PCSC_MSG)
fi
AM_CONDITIONAL(HAVE_PCSCLITE, test "x$ac_cv_lib_pcsclite_SCardEstablishContext" = "xyes")

AC_SUBST(LIBDL)
AC_CHECK_LIB(dl, dlopen,
	     [LIBDL="$LIBDL -ldl" ac_cv_func_dlopen_ldl=yes],
	     ac_cv_func_dlopen_ldl=no)

AC_SUBST(LIBUSB)
AC_CHECK_LIB(usb, usb_init,
	[
	      	LIBUSB="$LIBUSB -lusb"
		AC_DEFINE(HAVE_LIBUSB,1, [Libusb is available])
	])

AC_OUTPUT([
Makefile
tools/Makefile
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

