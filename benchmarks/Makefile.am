NULL=#

bin_PROGRAMS = $(NULL)

bin_PROGRAMS += \
		intel_upload_blit_large \
		intel_upload_blit_large_gtt \
		intel_upload_blit_large_map \
		intel_upload_blit_small \
		$(NULL)

BENCHMARK_LIBS = \
	../lib/libintel_tools.la \
	$(DRM_LIBS)

intel_upload_blit_large_LDADD = $(BENCHMARK_LIBS)
intel_upload_blit_large_gtt_LDADD = $(BENCHMARK_LIBS)
intel_upload_blit_large_map_LDADD = $(BENCHMARK_LIBS)
intel_upload_blit_small_LDADD = $(BENCHMARK_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
