include Makefile.sources

SUBDIRS = null_state_gen registers

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DEBUG_CFLAGS) $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS) $(LIBUNWIND_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(LIBUNWIND_LIBS) -lm
AM_LDFLAGS = -Wl,--as-needed


# aubdumper

module_LTLIBRARIES = intel_aubdump.la
moduledir = $(libdir)
intel_aubdump_la_LDFLAGS = -module -avoid-version -no-undefined
intel_aubdump_la_SOURCES = aubdump.c intel_aub.h
intel_aubdump_la_LIBADD = $(top_builddir)/lib/libintel_tools.la

bin_SCRIPTS = intel_aubdump
CLEANFILES = $(bin_SCRIPTS)

