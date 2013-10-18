if BUILD_TESTS
noinst_PROGRAMS = \
	gem_stress \
	ddi_compute_wrpll \
	$(TESTS_progs) \
	$(TESTS_progs_M) \
	$(HANG) \
	$(TESTS_testsuite)
	$(NULL)

if HAVE_NOUVEAU
NOUVEAU_TESTS_M = \
	prime_nv_api \
	prime_nv_pcopy \
	prime_nv_test \
	$(NULL)
endif

TESTS_progs_M = \
	debugfs_pipe_crc \
	gem_basic \
	gem_caching \
	gem_concurrent_blit \
	gem_cs_tlb \
	gem_ctx_bad_exec \
	gem_dummy_reloc_loop \
	gem_evict_alignment \
	gem_evict_everything \
	gem_exec_bad_domains \
	gem_exec_nop \
	gem_fenced_exec_thrash \
	gem_fence_thrash \
	gem_flink \
	gem_flink_race \
	gem_linear_blits \
	gem_mmap \
	gem_mmap_gtt \
	gem_partial_pwrite_pread \
	gem_persistent_relocs \
	gem_pread \
	gem_pread_after_blit \
	gem_pwrite \
	gem_pwrite_pread \
	gem_readwrite \
	gem_reloc_overflow \
	gem_reloc_vs_gpu \
	gem_ringfill \
	gem_set_tiling_vs_blt \
	gem_storedw_batches_loop \
	gem_suspend \
	gem_tiled_blits \
	gem_tiled_partial_pwrite_pread \
	gem_write_read_ring_switch \
	kms_addfb \
	kms_cursor_crc \
	kms_flip \
	kms_render \
	kms_setmode \
	$(NOUVEAU_TESTS_M) \
	pc8 \
	prime_self_import \
	$(NULL)

TESTS_progs = \
	drm_get_client_auth \
	drm_vma_limiter \
	drm_vma_limiter_cached \
	drm_vma_limiter_cpu \
	drm_vma_limiter_gtt \
	gem_bad_length \
	gem_cpu_reloc \
	gem_cs_prefetch \
	gem_ctx_bad_destroy \
	gem_ctx_basic \
	gem_ctx_create \
	gem_ctx_exec \
	gem_double_irq_loop \
	gem_exec_big \
	gem_exec_blt \
	gem_exec_faulting_reloc \
	gem_exec_lut_handle \
	gem_gtt_cpu_tlb \
	gem_gtt_speed \
	gem_hangcheck_forcewake \
	gem_largeobject \
	gem_lut_handle \
	gem_mmap_offset_exhaustion \
	gem_pin \
	gem_pipe_control_store_loop \
	gem_reg_read \
	gem_render_linear_blits \
	gem_render_tiled_blits \
	gem_ring_sync_loop \
	gem_seqno_wrap \
	gem_set_tiling_vs_gtt \
	gem_set_tiling_vs_pwrite \
	gem_storedw_loop_blt \
	gem_storedw_loop_bsd \
	gem_storedw_loop_render \
	gem_storedw_loop_vebox \
	gem_threaded_access_tiled \
	gem_tiled_fence_blits \
	gem_tiled_pread \
	gem_tiled_pread_pwrite \
	gem_tiled_swapping \
	gem_tiling_max_stride \
	gem_unfence_active_buffers \
	gem_unref_active_buffers \
	gem_vmap_blits \
	gem_wait_render_timeout \
	gen3_mixed_blits \
	gen3_render_linear_blits \
	gen3_render_mixed_blits \
	gen3_render_tiledx_blits \
	gen3_render_tiledy_blits \
	getclient \
	getstats \
	getversion \
	prime_udl \
	sysfs_rc6_residency \
	sysfs_rps \
	$(NULL)

# IMPORTANT: The ZZ_ tests need to be run last!
# ... and make can't deal with inlined comments ...
TESTS_scripts_M = \
	$(NULL)

TESTS_scripts = \
	debugfs_emon_crash \
	debugfs_reader \
	module_reload \
	sysfs_edid_timing \
	sysfs_l3_parity \
	test_rte_check \
	tools_test \
	ZZ_hangman \
	ZZ_missed_irq \
	$(NULL)

# This target contains testcases which support automagic subtest enumeration
# from the piglit testrunner with --list-subtests and running individual
# subtests with --run-subtest <testname>
multi_kernel_tests = \
	$(TESTS_progs_M) \
	$(TESTS_scripts_M) \
	$(NULL)

single_kernel_tests = \
	$(TESTS_progs) \
	$(TESTS_scripts) \
	$(NULL)

kernel_tests = \
	$(single_kernel_tests) \
	$(multi_kernel_tests) \
	$(NULL)

TESTS_testsuite = \
	igt_fork_helper \
	$(NULL)

TESTS = \
	$(TESTS_testsuite) \
	$(NULL)

list-single-tests:
	@echo TESTLIST
	@echo ${single_kernel_tests}
	@echo END TESTLIST

list-multi-tests:
	@echo TESTLIST
	@echo ${multi_kernel_tests}
	@echo END TESTLIST

HANG = \
	gem_bad_batch \
	gem_hang \
	gem_bad_blit \
	gem_bad_address \
	gem_non_secure_batch \
	$(NULL)

scripts = \
	check_drm_clients \
	ddx_intel_after_fbdev \
	debugfs_wedged \
	drm_lib.sh \
	$(NULL)

IMAGES = pass.png 1080p-left.png 1080-right.png

EXTRA_PROGRAMS = $(TESTS_progs) $(TESTS_progs_M) $(HANG) $(TESTS_testsuite)
EXTRA_DIST = $(TESTS_scripts) $(TESTS_scripts_M) $(scripts) $(IMAGES)

CLEANFILES = $(EXTRA_PROGRAMS)

AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib \
	-DIGT_DATADIR=\""$(abs_srcdir)"\" \
	$(NULL)

LDADD = ../lib/libintel_tools.la $(PCIACCESS_LIBS) $(DRM_LIBS) 

testdisplay_SOURCES = \
	testdisplay.c \
	testdisplay.h \
	testdisplay_hotplug.c \
	$(NULL)

TESTS_progs += testdisplay
LDADD += $(CAIRO_LIBS) $(LIBUDEV_LIBS) $(GLIB_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS) $(LIBUDEV_CFLAGS) $(GLIB_CFLAGS)

gem_fence_thrash_CFLAGS = $(AM_CFLAGS) $(THREAD_CFLAGS)
gem_fence_thrash_LDADD = $(LDADD) -lpthread
gem_flink_race_LDADD = $(LDADD) -lpthread
gem_threaded_access_tiled_LDADD = $(LDADD) -lpthread
prime_self_import_LDADD = $(LDADD) -lpthread

gem_wait_render_timeout_LDADD = $(LDADD) -lrt
kms_flip_LDADD = $(LDADD) -lrt

gem_ctx_basic_LDADD = $(LDADD) -lpthread

prime_nv_test_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_test_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_api_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_api_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
prime_nv_pcopy_CFLAGS = $(AM_CFLAGS) $(DRM_NOUVEAU_CFLAGS)
prime_nv_pcopy_LDADD = $(LDADD) $(DRM_NOUVEAU_LIBS)
endif
