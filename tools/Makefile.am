bin_PROGRAMS = intel_stepping

intel_stepping_LDADD = $(PCIACCESS_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
