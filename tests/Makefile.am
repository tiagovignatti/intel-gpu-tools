noinst_PROGRAMS = \
	gem_stress \
	$(NULL)

gem_stress_SOURCES = \
	gem_stress.c \
	gem_stress.h \
	gem_stress_i915.c \
	gem_stress_i830.c

TESTS = getversion \
	getclient \
	getstats \
	gem_basic \
	gem_exec_nop \
	gem_exec_blt \
	gem_flink \
	gem_readwrite \
	gem_ringfill \
	gem_mmap \
	gem_pwrite \
	gem_pread_after_blit \
	gem_tiled_pread \
	gem_linear_blits \
	gem_tiled_blits \
	gem_tiled_fence_blits \
	gem_largeobject \
	gem_bad_address \
	gem_bad_blit \
	gem_fence_thrash \
	gem_gtt_speed \
	$(NULL)

HANG = \
	gem_bad_batch \
	gem_hang \
	$(NULL)

EXTRA_PROGRAMS = $(TESTS) $(HANG)
CLEANFILES = $(EXTRA_PROGRAMS)

AM_CFLAGS = $(DRM_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib
LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS) 

if HAVE_CAIRO
if HAVE_LIBUDEV
if HAVE_GLIB
TESTS += testdisplay
LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)
endif
endif
endif

gem_fence_thrash_CFLAGS = $(AM_CFLAGS) -pthread
gem_fence_thrash_LDADD = $(LDADD) -lpthread
