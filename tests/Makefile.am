TESTS = getversion \
	getclient \
	getstats \
	gem_basic \
	gem_flink \
	gem_readwrite \
	gem_ringfill \
	gem_mmap \
	gem_pread_after_blit \
	gem_tiled_pread \
	gem_tiled_blits

EXTRA_PROGRAMS = $(TESTS)
CLEANFILES = $(EXTRA_PROGRAMS)

AM_CFLAGS = $(DRM_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS) 
