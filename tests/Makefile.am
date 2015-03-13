include Makefile.sources

if HAVE_NOUVEAU
    TESTS_progs_M += $(NOUVEAU_TESTS_M)
endif

if BUILD_TESTS
all-local: test-list.txt

test-list.txt: Makefile.sources
	@echo TESTLIST > $@
	@echo ${single_kernel_tests} ${multi_kernel_tests} >> $@
	@echo END TESTLIST >> $@

noinst_PROGRAMS = \
	$(HANG) \
	$(TESTS_testsuite) \
	$(NULL)

pkglibexec_PROGRAMS = \
	gem_alive \
	gem_stress \
	$(TESTS_progs) \
	$(TESTS_progs_M) \
	$(NULL)

dist_pkglibexec_SCRIPTS = \
	$(TESTS_scripts) \
	$(TESTS_scripts_M) \
	$(scripts) \
	$(NULL)

dist_pkgdata_DATA = \
	$(IMAGES) \
	$(NULL)

EXTRA_PROGRAMS = $(HANG)
EXTRA_DIST = $(common_files)

CLEANFILES = $(EXTRA_PROGRAMS) test-list.txt

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) $(DEBUG_CFLAGS)\
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib \
	-include "$(srcdir)/../lib/check-ndebug.h" \
	-DIGT_SRCDIR=\""$(abs_srcdir)"\" \
	-DIGT_DATADIR=\""$(pkgdatadir)"\" \
	$(LIBUNWIND_CFLAGS) \
	$(NULL)

LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS) $(LIBUNWIND_LIBS)

LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)

drm_import_export_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
drm_import_export_LDADD = $(LDADD) -lpthread
gem_close_race_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_close_race_LDADD = $(LDADD) -lpthread
gem_ctx_basic_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_ctx_basic_LDADD = $(LDADD) -lpthread
gem_ctx_thrash_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_ctx_thrash_LDADD = $(LDADD) -lpthread
gem_fence_thrash_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_fence_thrash_LDADD = $(LDADD) -lpthread
gem_fence_upload_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_fence_upload_LDADD = $(LDADD) -lpthread
gem_flink_race_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_flink_race_LDADD = $(LDADD) -lpthread
gem_mmap_gtt_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_mmap_gtt_LDADD = $(LDADD) -lpthread
gem_mmap_wc_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_mmap_wc_LDADD = $(LDADD) -lpthread
gem_threaded_access_tiled_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_threaded_access_tiled_LDADD = $(LDADD) -lpthread
gem_tiled_swapping_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_tiled_swapping_LDADD = $(LDADD) -lpthread
prime_self_import_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
prime_self_import_LDADD = $(LDADD) -lpthread
gen7_forcewake_mt_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gen7_forcewake_mt_LDADD = $(LDADD) -lpthread
gem_userptr_blits_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_userptr_blits_LDADD = $(LDADD) -lpthread

gem_wait_LDADD = $(LDADD) -lrt
kms_flip_LDADD = $(LDADD) -lrt -lpthread

prime_nv_test_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_test_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_api_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_api_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_pcopy_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_pcopy_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
endif

