#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

AC_PREREQ([2.63])
AC_INIT([intel-gpu-tools],
        [1.0.2],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [intel-gpu-tools])

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.10 foreign dist-bzip2])
AM_MAINTAINER_MODE([enable])

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

PKG_CHECK_MODULES(DRM, [libdrm_intel >= 2.4.23], have_drm=yes, have_drm=no)
if test "x$have_drm" = "xyes"; then
	AC_DEFINE([HAVE_DRM], 1, [Define to 1 if we have DRM support])
fi
AM_CONDITIONAL(HAVE_DRM, test "x$have_drm" = "xyes")

PKG_CHECK_MODULES(PCIACCESS, [pciaccess >= 0.10])

PKG_CHECK_MODULES(CAIRO, cairo, [HAVE_CAIRO=yes], [HAVE_CAIRO=no])
if test "x$HAVE_CAIRO" = xyes; then
	AC_DEFINE(HAVE_CAIRO, 1, [Have cairo support])
fi
AM_CONDITIONAL(HAVE_CAIRO, [test "x$HAVE_CAIRO" = xyes])

PKG_CHECK_MODULES(LIBUDEV, libudev, [HAVE_LIBUDEV=yes], [HAVE_LIBUDEV=no])
if test "x$HAVE_LIBUDEV" = xyes; then
   	AC_DEFINE(HAVE_LIBUDEV, 1, [Have libudev support])
fi
AM_CONDITIONAL(HAVE_LIBUDEV, [test "x$HAVE_LIBUDEV" = xyes])

PKG_CHECK_MODULES(GLIB, glib-2.0, [HAVE_GLIB=yes], [HAVE_GLIB=no])
if test "x$HAVE_GLIB" = xyes; then
   	AC_DEFINE(HAVE_GLIB, 1, [Have glib support])
fi
AM_CONDITIONAL(HAVE_GLIB, [test "x$HAVE_GLIB" = xyes])

dnl Use lots of warning flags with GCC

WARN_CFLAGS=""

if test "x$GCC" = "xyes"; then
	WARN_CFLAGS="-Wall -Wpointer-arith -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs -fno-strict-aliasing"
fi

AC_SUBST([WARN_CFLAGS])

AC_ARG_ENABLE(shader-debugger,
	      AS_HELP_STRING([--enable-shader-debugger],
			     [Enable shader debugging support [default=no]]),
	      [SHADER_DEBUGGER="$enableval"],
	      [SHADER_DEBUGGER=no])
AM_CONDITIONAL(HAVE_SHADER_DEBUGGER, [test "x$SHADER_DEBUGGER" = xyes])
if test "x$SHADER_DEBUGGER" = xyes; then
	AC_DEFINE(HAVE_SHADER_DEBUGGER, 1, [Have shader debugging support])
fi

m4_ifndef([XORG_MACROS_VERSION], [AC_FATAL([must install xorg-macros 1.3 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.3)
XORG_DEFAULT_OPTIONS

AC_CONFIG_FILES([
	Makefile
	benchmarks/Makefile
	lib/Makefile
	man/Makefile
	scripts/Makefile
	tests/Makefile
	tools/Makefile
	debugger/Makefile
])
AC_OUTPUT
