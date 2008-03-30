#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT([intel-gen4asm],
        0.1,
        [eric@anholt.net],
        intel-gen4asm)

AC_CONFIG_SRCDIR([Makefile.am])
AM_INIT_AUTOMAKE([dist-bzip2 foreign])

AM_MAINTAINER_MODE

# Checks for programs.
AC_PROG_CC
AM_PROG_LEX
AC_PROG_YACC

WARN_CFLAGS=""
if test "x$GCC" = "xyes"; then
	WARN_CFLAGS="-Wall -Wpointer-arith -Wstrict-prototypes \
	-Wmissing-prototypes -Wmissing-declarations \
	-Wnested-externs -fno-strict-aliasing"
	AC_DEFINE_UNQUOTED(HAVE_WARNING_CPP_DIRECTIVE,1,
	[Can use #warning in C files])
fi
AC_SUBST(WARN_CFLAGS)

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

AC_OUTPUT([
	Makefile
	doc/Makefile
	src/Makefile
	test/Makefile
])
