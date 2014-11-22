# virt-bmap
# Copyright (C) 2014 Red Hat Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

AC_INIT([virt-bmap],[0.1])
AM_INIT_AUTOMAKE([foreign])

dnl Check for basic C environment.
AC_PROG_CC_STDC
AM_PROG_CC_C_O
AC_PROG_CPP

AC_C_PROTOTYPES
test "x$U" != "x" && AC_MSG_ERROR([Compiler not ANSI compliant])

dnl C++ environment.
AC_PROG_CXX
AC_PROG_CXX_C_O

AC_PROG_INSTALL

dnl Define GNU_SOURCE etc.
AC_USE_SYSTEM_EXTENSIONS

dnl Need libtool for creating the shared library.
AC_PROG_LIBTOOL

dnl Check if __attribute__((cleanup(...))) works.
dnl XXX It would be nice to use AC_COMPILE_IFELSE here, but gcc just
dnl emits a warning for attributes that it doesn't understand.
AC_MSG_CHECKING([if __attribute__((cleanup(...))) works with this compiler])
AC_RUN_IFELSE([
AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>

void
freep (void *ptr)
{
  exit (EXIT_SUCCESS);
}

void
test (void)
{
  __attribute__((cleanup(freep))) char *ptr = malloc (100);
}

int
main (int argc, char *argv[])
{
  test ();
  exit (EXIT_FAILURE);
}
]])
    ],[
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_ATTRIBUTE_CLEANUP],[1],[Define to 1 if '__attribute__((cleanup(...)))' works with this compiler.])
    ],[
    AC_MSG_WARN(
['__attribute__((cleanup(...)))' does not work.

You may not be using a sufficiently recent version of GCC or CLANG, or
you may be using a C compiler which does not support this attribute,
or the configure test may be wrong.

The code will still compile, but is likely to leak memory and other
resources when it runs.])])

dnl Check new enough libguestfs.
PKG_CHECK_MODULES([GUESTFS], [libguestfs >= 1.28])

dnl We need some new APIs which were added specifically for virt-bmap:
AC_CHECK_LIB([guestfs], [guestfs_blockdev_setra], [], [
    AC_MSG_ERROR([you need libguestfs >= 1.29.10 which has `guestfs_blockdev_setra'])
])
AC_CHECK_LIB([guestfs], [guestfs_bmap], [], [
    AC_MSG_ERROR([you need libguestfs >= 1.29.10 which has `guestfs_bmap'])
])

dnl Check nbdkit is installed.
AC_CHECK_PROG([NBDKIT], [nbdkit], [nbdkit], [no])
if test "$NBDKIT" = "xno"; then
    AC_MSG_ERROR([nbdkit was not found])
fi
AC_CHECK_HEADER([nbdkit-plugin.h],[],[
    AC_MSG_ERROR([you need to install the nbdkit development package])
])

dnl Check for C++ boost library with interval_map.hpp
AC_LANG_PUSH([C++])
AC_CHECK_HEADERS([boost/icl/interval_map.hpp], [],
    [AC_MSG_ERROR(You need the Boost libraries.)])
AC_LANG_POP([C++])

dnl Check for pod2man (from Perl, for the manual).
AC_CHECK_PROG([POD2MAN], [pod2man], [pod2man], [no])
if test "x$POD2MAN" = "xno"; then
    AC_MSG_ERROR([pod2man was not found.  This is needed to build man pages.])
fi

dnl Produce output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([run], [chmod +x,-w run])
AC_CONFIG_FILES([virt-bmap], [chmod +x,-w virt-bmap])

AC_OUTPUT
