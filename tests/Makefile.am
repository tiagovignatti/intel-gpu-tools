TESTS = getversion \
	getclient \
	getstats \
	gem_basic \
	gem_flink \
	gem_readwrite \
	gem_mmap

EXTRA_PROGRAMS = $(TESTS)
CLEANFILES = $(EXTRA_PROGRAMS)

AM_CFLAGS = $(DRM_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
LDADD = $(DRM_LIBS) ../lib/libintel_tools.la
