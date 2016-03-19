IGT_LIB_PATH := $(builddir)
GPU_TOOLS_PATH := $(top_srcdir)

SUBDIRS = . tests

include Makefile.sources

noinst_LTLIBRARIES = libintel_tools.la
noinst_HEADERS = check-ndebug.h

if HAVE_VC4
    libintel_tools_la_SOURCES += 	\
        igt_vc4.c			\
        igt_vc4.h
endif

AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) $(LIBUNWIND_CFLAGS) $(DEBUG_CFLAGS) \
	    -DIGT_SRCDIR=\""$(abs_top_srcdir)/tests"\" \
	    -DIGT_DATADIR=\""$(pkgdatadir)"\" \
	    -DIGT_LOG_DOMAIN=\""$(subst _,-,$*)"\" \
	    -pthread

LDADD = $(CAIRO_LIBS) $(LIBUNWIND_LIBS) $(TIMER_LIBS) -lm
AM_CFLAGS += $(CAIRO_CFLAGS)

