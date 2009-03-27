bin_PROGRAMS = \
	intel_gpu_top \
	intel_stepping

intel_gpu_top_LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS)
intel_stepping_LDADD = $(PCIACCESS_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
