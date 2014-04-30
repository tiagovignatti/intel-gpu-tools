include Makefile.sources

if HAVE_NOUVEAU
    TESTS_progs_M += $(NOUVEAU_TESTS_M)
endif

if BUILD_TESTS
all-local: single-tests.txt multi-tests.txt

list-single-tests:
	@echo TESTLIST
	@echo ${single_kernel_tests}
	@echo END TESTLIST

list-multi-tests:
	@echo TESTLIST
	@echo ${multi_kernel_tests}
	@echo END TESTLIST

single-tests.txt: Makefile.sources
	@echo TESTLIST > $@
	@echo ${single_kernel_tests} >> $@
	@echo END TESTLIST >> $@

multi-tests.txt: Makefile.sources
	@echo TESTLIST > $@
	@echo ${multi_kernel_tests} >> $@
	@echo END TESTLIST >> $@

EXTRA_PROGRAMS = $(TESTS_progs) $(TESTS_progs_M) $(HANG) $(TESTS_testsuite)
EXTRA_DIST = $(TESTS_scripts) $(TESTS_scripts_M) $(scripts) $(IMAGES) $(common_files)

CLEANFILES = $(EXTRA_PROGRAMS) single-tests.txt multi-tests.txt

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib \
	-include "$(srcdir)/../lib/check-ndebug.h" \
	-include "$(top_builddir)/version.h" \
	-DIGT_DATADIR=\""$(abs_srcdir)"\" \
	$(NULL)

LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS) 

LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)

gem_close_race_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_close_race_LDADD = $(LDADD) -lpthread
gem_ctx_basic_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_ctx_basic_LDADD = $(LDADD) -lpthread
gem_fence_thrash_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_fence_thrash_LDADD = $(LDADD) -lpthread
gem_flink_race_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_flink_race_LDADD = $(LDADD) -lpthread
gem_threaded_access_tiled_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_threaded_access_tiled_LDADD = $(LDADD) -lpthread
gem_tiled_swapping_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_tiled_swapping_LDADD = $(LDADD) -lpthread
prime_self_import_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
prime_self_import_LDADD = $(LDADD) -lpthread
gen7_forcewake_mt_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gen7_forcewake_mt_LDADD = $(LDADD) -lpthread

gem_wait_render_timeout_LDADD = $(LDADD) -lrt
kms_flip_LDADD = $(LDADD) -lrt -lpthread

prime_nv_test_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_test_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_api_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_api_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_pcopy_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_pcopy_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
endif

