SUBDIRS=system_routine

bin_PROGRAMS = #

if HAVE_SHADER_DEBUGGER
##needed by distcheck
DIST_SUBDIRS=system_routine
EXTRA_DIST=system_routine/

bin_PROGRAMS += \
	eudb \
	debug_rdata \
	$(NULL)
endif
LDADD = ../lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
