
include Makefile.sources

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS) $(LIBUNWIND_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS) $(LIBUNWIND_LIBS) -lm

benchmarks_LTLIBRARIES = gem_exec_tracer.la
gem_exec_tracer_la_LDFLAGS = -module -avoid-version -no-undefined
gem_exec_tracer_la_LIBADD = -ldl

gem_wait_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_wait_LDADD = $(LDADD) -lpthread

gem_latency_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_latency_LDADD = $(LDADD) -lpthread

EXTRA_DIST=README
