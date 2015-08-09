
include Makefile.sources

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS) $(LIBUNWIND_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS) $(LIBUNWIND_LIBS) -lm

noinst_LTLIBRARIES = generate_trace.la
generate_trace_la_LDFLAGS = -module -avoid-version -no-undefined
generate_trace_la_LIBADD = -ldl
