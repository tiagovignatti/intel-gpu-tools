include Makefile.sources

AM_TESTS_ENVIRONMENT = \
	top_builddir=$(top_builddir) \
	top_srcdir=$(top_srcdir)

EXTRA_DIST = $(check_SCRIPTS)

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) $(DEBUG_CFLAGS) \
	-I$(srcdir)/../.. \
	-I$(srcdir)/.. \
	-include "$(srcdir)/../../lib/check-ndebug.h" \
	-DIGT_DATADIR=\""$(abs_srcdir)"\" \
	$(NULL)

LDADD = ../libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS) $(LIBUNWIND_LIBS) $(TIMER_LIBS)

LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS) -lm
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)
