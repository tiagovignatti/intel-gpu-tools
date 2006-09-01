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

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

AC_OUTPUT([
	Makefile
	doc/Makefile
	src/Makefile
	test/Makefile
])
