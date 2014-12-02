IGT_LIB_PATH := $(builddir)
GPU_TOOLS_PATH := $(top_srcdir)

SUBDIRS = . tests

include Makefile.sources

noinst_LTLIBRARIES = libintel_tools.la
noinst_HEADERS = check-ndebug.h

AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS)  \
	    -DIGT_DATADIR=\""$(abs_top_srcdir)/tests"\" \
	    -DIGT_LOG_DOMAIN=\""$(subst _,-,$*)"\"


LDADD = $(CAIRO_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS)

