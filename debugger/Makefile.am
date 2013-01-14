
SUBDIRS = system_routine

bin_PROGRAMS = eudb
noinst_PROGRAMS = debug_rdata

AM_CPPFLAGS = 			\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/lib

AM_CFLAGS = 			\
	$(DRM_CFLAGS) 		\
	$(PCIACCESS_CFLAGS) 	\
	$(CAIRO_CFLAGS)		\
	$(CWARNFLAGS)

LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS)
