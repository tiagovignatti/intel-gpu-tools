include Makefile.sources

noinst_LTLIBRARIES = libintel_tools.la

AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS)

LDADD = $(CAIRO_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS)

