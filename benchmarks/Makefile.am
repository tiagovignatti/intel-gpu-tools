bin_PROGRAMS = \
	intel_upload_blit_large \
	intel_upload_blit_small

BENCHMARK_LIBS = \
	../lib/libintel_tools.la \
	$(DRM_LIBS)

intel_upload_blit_large_LDADD = $(BENCHMARK_LIBS)
intel_upload_blit_small_LDADD = $(BENCHMARK_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
