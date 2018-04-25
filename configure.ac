dnl Copyright © 2017 Collabora Ltd

dnl This file is part of libcapsule.

dnl libcapsule is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU Lesser General Public License as
dnl published by the Free Software Foundation; either version 2.1 of the
dnl License, or (at your option) any later version.

dnl libcapsule is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU Lesser General Public License for more details.

dnl You should have received a copy of the GNU Lesser General Public
dnl License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.60])

AC_INIT([libcapsule],
        m4_esyscmd([build-aux/git-version-gen .tarball-version]),
        [], [], [https://gitlab.collabora.com/vivek/libcapsule])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_ARG_VAR([PERL])
AC_PATH_PROG([PERL], [perl], [perl])

m4_pattern_forbid([^AX_], [Please install GNU autoconf-archive])

dnl don't kvetch about gnu makefile syntax
dnl and don't require ChangeLog &co if they don't exist
AM_INIT_AUTOMAKE([-Wno-portability foreign])
AM_SILENT_RULES([yes])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
# So we can install x86_64-linux-gnu-capsule-symbols etc.
AC_CANONICAL_HOST

LT_INIT

AX_IS_RELEASE([dash-version])
dnl -Wno-pointer-arith: We do a lot of arithmetic on void *, relying on gcc
dnl     making (void * + int) the same as (char * + int)
dnl -Wno-declaration-after-statement: Stylistic choice, this is C99
AX_COMPILER_FLAGS([], [], [], [], [ \
    -Wno-pointer-arith \
    -Wno-declaration-after-statement \
])

dnl make sure we've got the _right_ libelf (doesn't always ship a .pc)
PKG_CHECK_MODULES([LIBELF], [libelf >= 0.152], [have_libelf=yes], [
    AC_MSG_WARN([libelf.pc not found])
    AC_MSG_WARN([Set LIBELF_CFLAGS, LIBELF_LIBS if -lelf is insufficient])
    save_LIBS="$LIBS"
    AC_CHECK_TYPE([Elf_Cmd], [], [], [#include <libelf.h>])
    AC_CHECK_LIB([elf], [elf_begin], [],
                 [AC_MSG_ERROR([libelf not found])])
    AC_MSG_CHECKING([Elf_Cmd ELF_C_READ_MMAP])
    AC_RUN_IFELSE([AC_LANG_PROGRAM([#include <libelf.h>],
                                   [Elf_Cmd x = ELF_C_READ_MMAP;])],
                  [AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])]
                  [AC_MSG_ERROR([ELF_C_READ_MMAP missing: libelfg vs libelf0?])])
    LIBS="$save_LIBS"
    LIBELF_LIBS=-lelf
])

PKG_CHECK_MODULES([GLIB], [glib-2.0], [have_glib=yes], [have_glib=no])
AM_CONDITIONAL([HAVE_GLIB], [test "x$have_glib" = xyes])

AC_ARG_ENABLE([host-prefix],
  [AS_HELP_STRING([--enable-host-prefix=PREFIX/yes/no],
    [install link to tools with a prefix [default=$host-]])],
  [],
  [enable_host_prefix=yes])

AM_CONDITIONAL([ENABLE_HOST_PREFIX], [test "x$enable_host_prefix" != xno])
AS_IF([test "x$enable_host_prefix" = xyes],
  [enable_host_prefix="$host-"])
AS_IF([test "x$enable_host_prefix" = xno],
  [enable_host_prefix=""])
AC_SUBST([HOST_PREFIX], [$enable_host_prefix])

AC_ARG_ENABLE([tools-rpath],
  [AS_HELP_STRING([--enable-tools-rpath=RPATH/yes/no],
    [install link to tools with a RPATH ready to override with chrpath])],
  [],
  [enable_tools_rpath=no])

AS_IF([test "x$enable_tools_rpath" = xyes],
  [enable_tools_rpath="/_ORIGIN_/__/lib/$host"])
AS_IF([test "x$enable_tools_rpath" = xno],
  [enable_tools_rpath=""])
AC_SUBST([enable_tools_rpath])
AM_CONDITIONAL([ENABLE_TOOLS_RPATH], [test "x$enable_tools_rpath" != x])

AC_SEARCH_LIBS([dlmopen], [dl])
AC_PATH_PROG([XSLTPROC], [xsltproc])
GTK_DOC_CHECK
AC_CONFIG_FILES([Makefile data/libcapsule.pc data/libcapsule-uninstalled.pc data/libcapsule-tools.pc data/libcapsule-tools-uninstalled.pc data/dirconf.txt])
AC_OUTPUT
