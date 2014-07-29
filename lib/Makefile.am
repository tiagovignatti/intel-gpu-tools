IGT_LIB_PATH := $(builddir)
GPU_TOOLS_PATH := $(top_srcdir)

include Makefile.sources

noinst_LTLIBRARIES = libintel_tools.la
noinst_HEADERS = check-ndebug.h

AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS)

LDADD = $(CAIRO_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS)

