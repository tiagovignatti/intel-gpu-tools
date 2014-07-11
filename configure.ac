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
	[1.7],
	[https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
	[intel-gpu-tools])

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_GNU_SOURCE
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_PATH_PYTHON([3],, [:])

AC_PROG_CC
AM_PROG_LEX
AC_PROG_YACC

# check for gtk-doc
m4_ifdef([GTK_DOC_CHECK], [
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])
],[
AM_CONDITIONAL([ENABLE_GTK_DOC], false)
enable_gtk_doc=no
])


# Checks for functions, headers, structures, etc.
AC_HEADER_STDC
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

# warning flags for the assembler. We can't quite use CWARNFLAGS for it yet as
# it generates waaaay too many warnings.
ASSEMBLER_WARN_CFLAGS=""
if test "x$GCC" = "xyes"; then
	ASSEMBLER_WARN_CFLAGS="-Wall -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs -fno-strict-aliasing"
fi
AC_SUBST(ASSEMBLER_WARN_CFLAGS)

PKG_CHECK_MODULES(DRM, [libdrm_intel >= 2.4.52 libdrm])
PKG_CHECK_MODULES(PCIACCESS, [pciaccess >= 0.10])
PKG_CHECK_MODULES(OVERLAY_XVLIB, [xv x11 xext dri2proto >= 2.6], enable_overlay_xvlib=yes, enable_overlay_xvlib=no)
PKG_CHECK_MODULES(OVERLAY_XLIB, [cairo-xlib dri2proto >= 2.6], enable_overlay_xlib=yes, enable_overlay_xlib=no)

AM_CONDITIONAL(BUILD_OVERLAY_XVLIB, [test "x$enable_overlay_xvlib" = xyes])
AM_CONDITIONAL(BUILD_OVERLAY_XLIB, [test "x$enable_overlay_xlib" = xyes])
AM_CONDITIONAL(BUILD_OVERLAY, [test "x$enable_overlay_xlib" = xyes -o "x$enable_overlay_xvlib"])
if test x$enable_overlay_xvlib = xyes; then
	AC_DEFINE(HAVE_OVERLAY_XVLIB, 1, [Enable XV backend])
fi
if test x$enable_overlay_xlib = xyes; then
	AC_DEFINE(HAVE_OVERLAY_XLIB, 1, [Enable X backend])
fi
PKG_CHECK_MODULES(XRANDR, xrandr >= 1.3, AC_DEFINE(HAVE_XRANDR, 1, [Have libXrandr]), [have_xrandr=no])

# for testdisplay
PKG_CHECK_MODULES(CAIRO, [cairo >= 1.12.0])
PKG_CHECK_MODULES(LIBUDEV, [libudev], [udev=yes], [udev=no])
if test x"$udev" = xyes; then
	AC_DEFINE(HAVE_UDEV,1,[Enable udev-based monitor hotplug detection])
fi
PKG_CHECK_MODULES(GLIB, glib-2.0)

# can we build the assembler?
AS_IF([test x"$LEX" != "x:" -a x"$YACC" != xyacc],
      [enable_assembler=yes],
      [enable_assembler=no])
AM_CONDITIONAL(BUILD_ASSEMBLER, [test "x$enable_assembler" = xyes])

# -----------------------------------------------------------------------------
#			Configuration options
# -----------------------------------------------------------------------------
# for dma-buf tests
AC_ARG_ENABLE(nouveau, AS_HELP_STRING([--disable-nouveau],
	      [Enable use of nouveau API for prime tests (default: auto)]),
	      [NOUVEAU=$enableval], [NOUVEAU=auto])
if test "x$NOUVEAU" = xauto; then
	PKG_CHECK_EXISTS([libdrm_nouveau >= 2.4.33], [NOUVEAU=yes], [NOUVEAU=no])
fi
if test "x$NOUVEAU" = xyes; then
	PKG_CHECK_MODULES(DRM_NOUVEAU, [libdrm_nouveau >= 2.4.33])
	AC_DEFINE(HAVE_NOUVEAU, 1, [Have nouveau support])
fi
AM_CONDITIONAL(HAVE_NOUVEAU, [test "x$NOUVEAU" = xyes])

#Configure options for the python quick-dumper
AC_ARG_ENABLE(dumper,
	      AS_HELP_STRING([--disable-dumper],
			     [Disable the python based register dumper (default: enabled)]),
			     [DUMPER=$enableval], [DUMPER=auto])
if test "x$DUMPER" = xauto; then
	# AX_PYTHON_DEVEL/AX_SWIG_PYTHON are not super friendly and don't
	# easily allow us to introspect the result of their checks. So if we
	# find SWING on the system, that's enough to try compiling the dumper.
	AX_PKG_SWIG(2.0.0, [DUMPER=yes], [DUMPER=no])
fi
if test "x$DUMPER" == xyes; then
	AC_DEFINE(HAVE_DUMPER, 1, [Have dumper support])
	# SWIG configuration
	AX_PKG_SWIG(2.0.0, [], [ AC_MSG_ERROR([SWIG 2.0.0 or higher is required to build..]) ])
	AX_PYTHON_DEVEL([>= '3.0'])
	AX_SWIG_PYTHON
fi
AM_CONDITIONAL(HAVE_DUMPER, [test "x$DUMPER" = xyes])

# Define a configure option for the shader debugger
AC_ARG_ENABLE(shader-debugger, AS_HELP_STRING([--enable-shader-debugger],
	      [Enable shader debugging support [autodetected]]),
	      [BUILD_SHADER_DEBUGGER="$enableval"], [BUILD_SHADER_DEBUGGER=auto])

# Shader debugger depends on python3, intel-genasm and objcopy
if test "x$BUILD_SHADER_DEBUGGER" != xno; then
	# Check that the assembler is built
	if test "x$enable_assembler" = xno; then
		BUILD_SHADER_DEBUGGER=no
		if test "x$BUILD_SHADER_DEBUGGER" = xyes; then
			AC_MSG_ERROR([Shader debugger requested, but assembler not enabled.])
		fi
	fi

	# Check Python 3 is installed
	if test "$PYTHON" = ":" ; then
		if test "x$BUILD_SHADER_DEBUGGER" = xyes; then
			AC_MSG_ERROR([Shader debugger requested, python version 3 not found.])
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
AS_IF([test "x$BUILD_SHADER_DEBUGGER" != xno],
      [enable_debugger=yes], [enable_debugger=no])

# -----------------------------------------------------------------------------

# To build multithread code, gcc uses -pthread, Solaris Studio cc uses -mt
XORG_TESTSET_CFLAG([THREAD_CFLAGS], [-pthread], [-mt])
AC_SUBST([THREAD_CFLAGS])

AC_ARG_ENABLE(tests,
	      AS_HELP_STRING([--disable-tests],
	      [Disable tests build (default: enabled)]),
	      [BUILD_TESTS=$enableval], [BUILD_TESTS="yes"])
if test "x$BUILD_TESTS" = xyes; then
	AC_DEFINE(BUILD_TESTS, 1, [Build tests])
fi
AM_CONDITIONAL(BUILD_TESTS, [test "x$BUILD_TESTS" = xyes])
AC_DEFINE_UNQUOTED(TARGET_CPU_PLATFORM, ["$host_cpu"], [Target platform])

files="broadwell cherryview haswell ivybridge sandybridge valleyview"
for file in $files; do
	QUICK_DUMP_EXTRA_DIST+="$file `tr '\n' ' ' < tools/quick_dump/$file`"
done
AC_SUBST(QUICK_DUMP_EXTRA_DIST)

AC_CONFIG_FILES([
		 Makefile
		 benchmarks/Makefile
		 demos/Makefile
		 docs/Makefile
		 docs/reference/Makefile
		 docs/reference/intel-gpu-tools/Makefile
		 docs/reference/intel-gpu-tools/version.xml
		 lib/Makefile
		 man/Makefile
		 scripts/Makefile
		 tests/Makefile
		 tools/Makefile
		 tools/quick_dump/Makefile
		 tools/null_state_gen/Makefile
		 debugger/Makefile
		 debugger/system_routine/Makefile
		 assembler/Makefile
		 assembler/doc/Makefile
		 assembler/test/Makefile
		 assembler/intel-gen4asm.pc
		 overlay/Makefile
		 ])
AC_OUTPUT

# Print a summary of the compilation
echo ""
echo "Intel GPU tools"

echo ""
echo " • Tests:"
echo "       Build tests        : ${BUILD_TESTS}"
echo "       Compile prime tests: ${NOUVEAU}"
echo ""
echo " • Tools:"
echo "       Assembler          : ${enable_assembler}"
echo "       Debugger           : ${enable_debugger}"
echo "       Python dumper      : ${DUMPER}"
echo "       Overlay            : X: ${enable_overlay_xlib}, Xv: ${enable_overlay_xvlib}"
echo ""
echo " • API-Documentation      : ${enable_gtk_doc}"
echo ""

# vim: set ft=config ts=8 sw=8 tw=0 noet :
