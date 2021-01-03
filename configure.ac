dnl require autoconf 2.60 (AS_ECHO/AS_ECHO_N)
AC_PREREQ([2.68])
define(_CLIENT_VERSION_MAJOR, 3)
define(_CLIENT_VERSION_MINOR, 0)
define(_CLIENT_VERSION_REVISION, 0)
define(_CLIENT_VERSION_BUILD, 0)
define(_COPYRIGHT_YEAR, 2017)
define(_COPYRIGHT_HOLDERS,[The %s developers])
define(_COPYRIGHT_HOLDERS_SUBSTITUTION,[[Shift Devices AG]])
AC_INIT([Digital Bitbox],[_CLIENT_VERSION_MAJOR._CLIENT_VERSION_MINOR._CLIENT_VERSION_REVISION],[https://github.com/jonasschnelli/dbb_cli/issues],[DigitalBitbox])
AC_CONFIG_SRCDIR([src/config])
AC_CONFIG_HEADERS([src/config/dbb-config.h])
AX_PREFIX_CONFIG_H([src/config/_dbb-config.h], [DBB])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux/m4])

DBB_GUI_NAME=dbb-app

AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([no-define subdir-objects foreign])
AM_MAINTAINER_MODE([enable])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AX_CXX_COMPILE_STDCXX([11], [noext], [mandatory])
dnl AX_CXX_COMPILE_STDCXX_11()

dnl Compiler checks (here before libtool).
if test "x${CXXFLAGS+set}" = "xset"; then
  CXXFLAGS_overridden=yes
else
  CXXFLAGS_overridden=no
fi
AC_PROG_CXX
m4_ifdef([AC_PROG_OBJCXX],[AC_PROG_OBJCXX])

dnl By default, libtool for mingw refuses to link static libs into a dll for
dnl fear of mixing pic/non-pic objects, and import/export complications. Since
dnl we have those under control, re-enable that functionality.
case $host in
  *mingw*)
     lt_cv_deplibs_check_method="pass_all"
  ;;
  *darwin*)
  ;;
esac

LT_INIT([pic-only])

AC_PATH_TOOL(AR, ar)
AC_PATH_TOOL(RANLIB, ranlib)
AC_PATH_TOOL(STRIP, strip)
AC_PATH_PROG(PYTHON, python)
AC_PATH_PROG([GIT], [git])

dnl pkg-config check.
PKG_PROG_PKG_CONFIG

if test "x$CXXFLAGS_overridden" = "xno"; then
  CXXFLAGS="$CXXFLAGS -Wall -Wextra -Wformat -Wformat-security -Wno-unused-parameter -Wno-self-assign"
fi
CPPFLAGS="$CPPFLAGS -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS"

AC_LANG_PUSH([C++])

# Enable debug
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
                    [use debug compiler flags and macros (default is no)])],
    [enable_debug=$enableval],
    [enable_debug=no])

if test "x$enable_debug" = xyes; then
    CFLAGS="$CFLAGS -g3 -O0 -DDEBUG"
    CXXFLAGS="$CXXFLAGS -g3 -O0 -DDEBUG"
    AC_DEFINE_UNQUOTED([ENABLE_DEBUG],[1],[Define to 1 to enable debung output])
fi

# Enable net debug
AC_ARG_ENABLE([netdebug],
    [AS_HELP_STRING([--enable-netdebug],
                    [use net debug (default is no)])],
    [enable_netdebug=$enableval],
    [enable_netdebug=no])

if test "x$enable_netdebug" = xyes; then
    AC_DEFINE_UNQUOTED([ENABLE_NETDEBUG],[1],[Define to 1 to enable net debung output])
fi


AC_ARG_ENABLE([daemon],
    [AS_HELP_STRING([--enable-daemon],
    [enable daemon (default is yes)])],
    [enable_daemon=$enableval],
    [enable_daemon=yes])

if test "x$enable_daemon" = xyes; then
  AC_DEFINE_UNQUOTED([ENABLE_DAEMON],[1],[Define to 1 to enable the dbb daemon])
  AC_DEFINE_UNQUOTED([ENABLE_DBB_APP],[1],[Define to 1 to enable the dbb app])
fi

AC_ARG_ENABLE([hid_report_shift],
    AS_HELP_STRING([--enable-hid_report_shift], [enable hid_report_shift (default is yes)]), , [enable_hid_report_shift=yes])

AC_ARG_ENABLE([udev_check],
    AS_HELP_STRING([--enable-udev-check], [enable udev_check (default is yes)]), , [enable_udev_check=yes])

dnl Check for pthread compile/link requirements
AX_PTHREAD

use_pkgconfig=yes
case $host in
  *mingw*)

     AC_MSG_NOTICE([------- USING MINGW])
     #pkgconfig does more harm than good with MinGW
     use_pkgconfig=no

     TARGET_OS=windows
     AC_CHECK_LIB([mingwthrd],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([kernel32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([user32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([gdi32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([comdlg32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([winspool],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([winmm],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([shell32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([comctl32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([ole32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([oleaut32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([uuid],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([rpcrt4],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([advapi32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([ws2_32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([mswsock],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([shlwapi],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([iphlpapi],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([crypt32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([setupapi],      [main],, AC_MSG_ERROR(lib missing))

     #required for multimedia
     AC_CHECK_LIB([strmiids],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([dmoguids],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([msdmo],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([quartz],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([mfplat],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([opengl32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([glu32],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([d3d9],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([mf],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([evr],      [main],, AC_MSG_ERROR(lib missing))
     AC_CHECK_LIB([dxva2],      [main],, AC_MSG_ERROR(lib missing))

     AC_CHECK_LIB([curl],      [main],, AC_MSG_ERROR(lib missing))
     
     # -static is interpreted by libtool, where it has a different meaning.
     # In libtool-speak, it's -all-static.
     AX_CHECK_LINK_FLAG([[-static]],[LIBTOOL_APP_LDFLAGS="$LIBTOOL_APP_LDFLAGS -all-static -static-libstdc++ -static-libgcc"])

     LIBTOOL_APP_LDFLAGS="$LIBTOOL_APP_LDFLAGS -all-static -static-libstdc++ -static-libgcc"

     AC_PATH_PROG([MAKENSIS], [makensis], none)
     if test x$MAKENSIS = xnone; then
       AC_MSG_WARN("makensis not found. Cannot create installer.")
     fi

     AC_PATH_TOOL(WINDRES, windres, none)
     if test x$WINDRES = xnone; then
       AC_MSG_ERROR("windres not found")
     fi

     CPPFLAGS="$CPPFLAGS -D_MT -DWIN32 -D_WINDOWS -DCURL_STATICLIB"
     LEVELDB_TARGET_FLAGS="TARGET_OS=OS_WINDOWS_CROSSCOMPILE"
     if test "x$CXXFLAGS_overridden" = "xno"; then
       CXXFLAGS="$CXXFLAGS -w"
     fi
     
     case $host in
       i?86-*) WINDOWS_BITS=32 ;;
       x86_64-*) WINDOWS_BITS=64 ;;
       *) AC_MSG_ERROR("Could not determine win32/win64 for installer") ;;
     esac
     AC_SUBST(WINDOWS_BITS)

     dnl libtool insists upon adding -nostdlib and a list of objects/libs to link against.
     dnl That breaks our ability to build dll's with static libgcc/libstdc++/libssp. Override
     dnl its command here, with the predeps/postdeps removed, and -static inserted. Postdeps are
     dnl also overridden to prevent their insertion later.
     dnl This should only affect dll's.
     archive_cmds_CXX="\$CC -shared \$libobjs \$deplibs \$compiler_flags -static -o \$output_objdir/\$soname \${wl}--enable-auto-image-base -Xlinker --out-implib -Xlinker \$lib"
     postdeps_CXX=

     ;;
  *darwin*)
     TARGET_OS=darwin
     LEVELDB_TARGET_FLAGS="TARGET_OS=Darwin"
     if  test x$cross_compiling != xyes; then
       BUILD_OS=darwin
       AC_CHECK_PROG([PORT],port, port)
       if test x$PORT = xport; then
         dnl add default macports paths
         CPPFLAGS="$CPPFLAGS -isystem /opt/local/include"
         LIBS="$LIBS -L/opt/local/lib"
         if test -d /opt/local/include/db48; then
           CPPFLAGS="$CPPFLAGS -I/opt/local/include/db48"
           LIBS="$LIBS -L/opt/local/lib/db48"
         fi
       fi

       AC_CHECK_PROG([BREW],brew, brew)
       if test x$BREW = xbrew; then
         dnl These Homebrew packages may be keg-only, meaning that they won't be found
         dnl in expected paths because they may conflict with system files. Ask
         dnl Homebrew where each one is located, then adjust paths accordingly.
         dnl It's safe to add these paths even if the functionality is disabled by
         dnl the user (--without-wallet or --without-gui for example).

         openssl_prefix=`$BREW --prefix openssl 2>/dev/null`
         qt5_prefix=`$BREW --prefix qt5 2>/dev/null`
         if test x$openssl_prefix != x; then
           PKG_CONFIG_PATH="$openssl_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
           export PKG_CONFIG_PATH
         fi
         if test x$qt5_prefix != x; then
           PKG_CONFIG_PATH="$qt5_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
           export PKG_CONFIG_PATH
         fi
       fi
     else
       case $build_os in
         *darwin*)
           BUILD_OS=darwin
           ;;
         *)
           CXXFLAGS="$CXXFLAGS -std=c++11 -stdlib=libc++"
           AC_PATH_TOOL([INSTALLNAMETOOL], [install_name_tool], install_name_tool)
           AC_PATH_TOOL([OTOOL], [otool], otool)
           AC_PATH_PROGS([GENISOIMAGE], [genisoimage mkisofs],genisoimage)

           dnl libtool will try to strip the static lib, which is a problem for
           dnl cross-builds because strip attempts to call a hard-coded ld,
           dnl which may not exist in the path. Stripping the .a is not
           dnl necessary, so just disable it.
           old_striplib=
           ;;
       esac
     fi

     AX_CHECK_LINK_FLAG([[-Wl,-headerpad_max_install_names]], [LDFLAGS="$LDFLAGS -Wl,-headerpad_max_install_names"])
     CXXFLAGS="$CXXFLAGS -framework Security -framework CoreServices -framework Foundation -framework ApplicationServices -framework AppKit -framework IOKit"
     CPPFLAGS="$CPPFLAGS -DMAC_OSX"
     OBJCXXFLAGS="$CXXFLAGS"
     ;;
   *linux*)
     TARGET_OS=linux
     LINUX_LIBS=
     AC_SUBST(LINUX_LIBS)
     ;;
   *)
     ;;
esac

AC_CHECK_DECLS([le16toh, le32toh, le64toh, htole16, htole32, htole64, be16toh, be32toh, be64toh, htobe16, htobe32, htobe64],,,
		[#if HAVE_ENDIAN_H
                 #include <endian.h>
                 #elif HAVE_SYS_ENDIAN_H
                 #include <sys/endian.h>
                 #endif])

CXXFLAGS_TEMP="$CXXFLAGS"
LIBS_TEMP="$LIBS"
CXXFLAGS="$CXXFLAGS $SSL_CFLAGS $CRYPTO_CFLAGS"
LIBS="$LIBS $SSL_LIBS $CRYPTO_LIBS"
CXXFLAGS="$CXXFLAGS_TEMP"
LIBS="$LIBS_TEMP"

AC_CHECK_LIB(crypto, main)
AC_CHECK_LIB(ssl, SSL_version, , AC_MSG_WARN([SSL Disabled]) )

LIBCURL_CHECK_CONFIG(, 7.15.2, , [AC_MSG_ERROR([Missing required libcurl >= 7.15.2])])

if test x$use_reduce_exports = xyes; then
    CXXFLAGS="$CXXFLAGS $RE_CXXFLAGS"
    AX_CHECK_LINK_FLAG([[-Wl,--exclude-libs,ALL]], [RELDFLAGS="-Wl,--exclude-libs,ALL"])
fi

if test x$TARGET_OS != xwindows; then
  # All windows code is PIC, forcing it on just adds useless compile warnings
  AX_CHECK_COMPILE_FLAG([-fPIC],[PIC_FLAGS="-fPIC"])
fi

#######################################################################
# we reuse bitcoin core QT m4 and don't change the prefix (BITCOIN_)  #
# so we can copy upstreams m4 directly without merging                #
#######################################################################
BITCOIN_QT_INIT
dnl sets $bitcoin_enable_qt, $bitcoin_enable_qt_test, $bitcoin_enable_qt_dbus
BITCOIN_QT_CONFIGURE([$use_pkgconfig], [qt5])

dnl --enable-thread-safety
AC_ARG_ENABLE([thread-safety], [AS_HELP_STRING([--enable-thread-safety], [make the library thread-safe. [default=yes]])],
 [], [enable_thread_safety=yes])

if test x$enable_thread_safety = xyes; then
        AC_CHECK_LIB([pthread], [pthread_mutex_init], [AC_SUBST([LIBPTHREAD], [-lpthread])])
fi
AM_CONDITIONAL([HAVE_LIBPTHREAD], [test "x$ac_cv_lib_pthread_pthread_mutex_init" = "xyes" ])
# FIXME: isn't it able to integrate the followings to AC_CHECK_LIB?
if test x$ac_cv_lib_pthread_pthread_mutex_init = xyes ; then
        AC_DEFINE([HAVE_LIBPTHREAD], [1], [Define to 1 if using pthread is enabled.])
        CFLAGS="$CFLAGS -pthread"
        CPPFLAGS="$CPPFLAGS -pthread"
fi


if test x$bitcoin_enable_qt = xyes; then
  if test x$qt_enable_multimedia != xno; then
    AC_DEFINE([USE_MULTIMEDIA],[1],[Define if QtMultimedia support should be compiled in])
  fi
AC_DEFINE_UNQUOTED([ENABLE_QT],[1],[Define to 1 to enable Qt Gui])
AC_DEFINE_UNQUOTED([ENABLE_DBB_APP],[1],[Define to 1 to enable the dbb app])
fi

if test x$bitcoin_enable_qt = xno; then
  if test x$enable_daemon = xno; then
  AM_CONDITIONAL([ENABLE_DBB_APP],[test x$enable_daemon = xyes])
  else
  AM_CONDITIONAL([ENABLE_DBB_APP],[test x$enable_daemon = xyes])
  fi
else
AM_CONDITIONAL([ENABLE_DBB_APP],[test x$bitcoin_enable_qt = xyes])
fi
AM_CONDITIONAL([ENABLE_QT],[test x$bitcoin_enable_qt = xyes])
AM_CONDITIONAL([ENABLE_DAEMON],[test x$enable_daemon = xyes])

AM_CONDITIONAL([USE_MULTIMEDIA],[test x$qt_enable_multimedia = xyes])

if test "x$enable_hid_report_shift" != xno; then
  AC_DEFINE_UNQUOTED([ENABLE_HID_REPORT_SHIFT],[1],[Define to 1 to enable the HID REPORT 1 byte shift])
fi

if test "x$enable_udev_check" != xno; then
  AC_DEFINE_UNQUOTED([ENABLE_UDEV_CHECK],[1],[Define to 1 to enable the udev rule check])
fi

BITCOIN_QT_CHECK([AC_CHECK_LIB([qrencode], [main],[QR_LIBS=-lqrencode], AC_MSG_ERROR(qrencode missing))])
BITCOIN_QT_CHECK([AC_CHECK_HEADER([qrencode.h],, AC_MSG_ERROR(qrencode missing))])
AC_SUBST(QR_LIBS)
AC_SUBST(RELDFLAGS)
AM_CONDITIONAL([TARGET_WINDOWS], [test x$TARGET_OS = xwindows])
AM_CONDITIONAL([TARGET_LINUX], [test x$TARGET_OS = xlinux])
AM_CONDITIONAL([TARGET_OSX], [test x$TARGET_OS = xdarwin])
AC_SUBST(HARDENED_CXXFLAGS)
AC_SUBST(HARDENED_CPPFLAGS)
AC_SUBST(HARDENED_LDFLAGS)
AC_SUBST(LIBTOOL_APP_LDFLAGS)
AC_SUBST(PIC_FLAGS)
AC_SUBST(PIE_FLAGS)
AC_SUBST(BUILD_TEST)
AC_SUBST(BUILD_QT)
AC_SUBST(CLIENT_VERSION_MAJOR, _CLIENT_VERSION_MAJOR)
AC_SUBST(CLIENT_VERSION_MINOR, _CLIENT_VERSION_MINOR)
AC_SUBST(CLIENT_VERSION_REVISION, _CLIENT_VERSION_REVISION)
AC_SUBST(CLIENT_VERSION_BUILD, _CLIENT_VERSION_BUILD)
AC_SUBST(DBB_GUI_NAME)
AC_DEFINE([TARGET_OS], [$TARGET_OS], [Define to 1 if using pthread is enabled.])
export CXXFLAGS
AC_CONFIG_SUBDIRS([src/libbtc src/univalue src/hidapi])

AC_PROG_CXX
AC_CONFIG_FILES([Makefile src/Makefile win/setup.nsi mac/appfolder/Contents/Info.plist])
AC_OUTPUT

echo ""
echo "Options used to compile and link:"
echo "  TARGET_OS     = $TARGET_OS"
echo "  NET_DEBUG     = $enable_netdebug"
echo "  CC            = $CC"
echo "  CFLAGS        = $CFLAGS"
echo "  CPPFLAGS      = $CPPFLAGS"
echo "  CXX           = $CXX"
echo "  CXXFLAGS      = $CXXFLAGS"
echo "  LDFLAGS       = $LDFLAGS"
echo "  HIDRPSHFT     = $enable_hid_report_shift"
echo "  QRCODE-READER = $qt_enable_multimedia"
echo "  UDEV_CHECK    = $enable_udev_check"
echo ""
