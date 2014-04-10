include Makefile.sources

SUBDIRS = null_state_gen

if HAVE_DUMPER
SUBDIRS += quick_dump
endif

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS) $(LIBUDEV_LIBS)

