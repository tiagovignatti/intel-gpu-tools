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

AC_PREREQ([2.60])
AC_INIT([intel-gpu-tools],
        [1.3],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [intel-gpu-tools])

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_GNU_SOURCE

AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_PATH_PYTHON([3],, [:])
AM_MAINTAINER_MODE

# Checks for functions, headers, structures, etc.
AC_CHECK_HEADERS([termios.h])
AC_CHECK_MEMBERS([struct sysinfo.totalram],[],[],[AC_INCLUDES_DEFAULT
#include <sys/sysinfo.h>
])
AC_CHECK_FUNCS([swapctl])
AC_CHECK_FUNCS([asprintf])

# Initialize libtool
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

# Require X.Org macros 1.16 or later for XORG_TESTSET_CFLAG
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.16 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.16)
XORG_DEFAULT_OPTIONS

PKG_CHECK_MODULES(DRM, [libdrm_intel >= 2.4.38 libdrm])
PKG_CHECK_MODULES(PCIACCESS, [pciaccess >= 0.10])

# for dma-buf tests
AC_ARG_ENABLE(nouveau,
              AS_HELP_STRING([--disable-nouveau],
              [Enable use of nouveau API for prime tests (default: enabled)]),
              [NOUVEAU=$enableval], [NOUVEAU=yes])
if test "x$NOUVEAU" = xyes; then
   PKG_CHECK_MODULES(DRM_NOUVEAU, [libdrm_nouveau >= 2.4.33])
   AC_DEFINE(HAVE_NOUVEAU, 1, [Have nouveau support])
fi
AM_CONDITIONAL(HAVE_NOUVEAU, [test "x$NOUVEAU" = xyes])


# for testdisplay
PKG_CHECK_MODULES(CAIRO, cairo)
PKG_CHECK_MODULES(LIBUDEV, [libudev], [udev=yes], [udev=no])
if test x"$udev" = xyes; then
	AC_DEFINE(HAVE_UDEV,1,[Enable udev-based monitor hotplug detection])
fi
PKG_CHECK_MODULES(GLIB, glib-2.0)

# -----------------------------------------------------------------------------
#			Configuration options
# -----------------------------------------------------------------------------
# Define a configure option for the shadder debugger
AC_ARG_ENABLE(shader-debugger, AS_HELP_STRING([--enable-shader-debugger],
    [Enable shader debugging support [autodetected]]),
    [BUILD_SHADER_DEBUGGER="$enableval"], [BUILD_SHADER_DEBUGGER=auto])

# Shadder debugger depends on python3, intel-genasm and objcopy
if test "x$BUILD_SHADER_DEBUGGER" != xno; then
    # Check Python 3 is installed
    if test "$PYTHON" = ":" ; then
	if test "x$BUILD_SHADER_DEBUGGER" = xyes; then
	    AC_MSG_ERROR([Shader debugger requested, python version 3 not found.])
	else
	    BUILD_SHADER_DEBUGGER=no
	fi
    fi
    # Check for the Intel Chipset assembler compiler
    AC_PATH_PROGS([GEN4ASM], intel-gen4asm)
    if test -z "$GEN4ASM" ; then
	if test "x$BUILD_SHADER_DEBUGGER" = xyes; then
	    AC_MSG_ERROR([Shader debugger requested, but intel-gen4asm not found.])
	else
	    BUILD_SHADER_DEBUGGER=no
	fi
    fi
    # Check for the objcopy GNU binary utiliy command
    AC_PATH_PROGS([OBJCOPY], objcopy)
    if test -z "$OBJCOPY" ; then
	if test "x$BUILD_SHADER_DEBUGGER" = xyes; then
	    AC_MSG_ERROR([Shader debugger requested, but objcopy command not found.])
	else
	    BUILD_SHADER_DEBUGGER=no
	fi
    fi
fi

AM_CONDITIONAL(BUILD_SHADER_DEBUGGER, [test "x$BUILD_SHADER_DEBUGGER" != xno])
# -----------------------------------------------------------------------------

# To build multithread code, gcc uses -pthread, Solaris Studio cc uses -mt
XORG_TESTSET_CFLAG([THREAD_CFLAGS], [-pthread], [-mt])
AC_SUBST([THREAD_CFLAGS])

AC_CONFIG_FILES([
	Makefile
	benchmarks/Makefile
	demos/Makefile
	lib/Makefile
	man/Makefile
	scripts/Makefile
	tests/Makefile
	tools/Makefile
	debugger/Makefile
	debugger/system_routine/Makefile
])
AC_OUTPUT
