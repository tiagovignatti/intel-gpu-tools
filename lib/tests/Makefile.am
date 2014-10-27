include Makefile.sources

EXTRA_PROGRAMS = $(TESTS_testsuite)
EXTRA_DIST = $(TESTS_scripts)

CLEANFILES = $(EXTRA_PROGRAMS)

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) \
	-I$(srcdir)/../.. \
	-I$(srcdir)/.. \
	-include "$(srcdir)/../../lib/check-ndebug.h" \
	-DIGT_DATADIR=\""$(abs_srcdir)"\" \
	$(NULL)

LDADD = ../libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS)

LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)
