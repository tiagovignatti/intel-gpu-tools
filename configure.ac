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

AC_PREREQ(2.57)
AC_INIT([intel-gpu-tools],
        1.0.1,
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        intel-gpu-tools)

AC_CONFIG_SRCDIR([Makefile.am])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE

AC_CONFIG_FILES([
	shave
	shave-libtool
])

# Checks for programs.
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
DOLT
AC_PROG_CC
AM_PROG_CC_C_O

PKG_CHECK_MODULES(DRM, [libdrm_intel >= 2.4.6])
PKG_CHECK_MODULES(PCIACCESS, [pciaccess >= 0.10])

dnl Use lots of warning flags with GCC

WARN_CFLAGS=""

if test "x$GCC" = "xyes"; then
	WARN_CFLAGS="-Wall -Wpointer-arith -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs -fno-strict-aliasing"
fi

AC_SUBST([WARN_CFLAGS])

SHAVE_INIT([.], [enable])

AC_OUTPUT([
	Makefile
	benchmarks/Makefile
	lib/Makefile
	man/Makefile
	tests/Makefile
	tools/Makefile
])
