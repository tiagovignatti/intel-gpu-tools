/* wierd use of API tests */

/* test1- export buffer from intel, import same fd twice into nouveau,
   check handles match
   test2 - export buffer from intel, import fd once, close fd, try import again
   fail if it succeeds
   test3 - export buffer from intel, import twice on nouveau, check handle is the same
   test4 - export handle twice from intel, import into nouveau twice, check handle is the same
*/

#include "igt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "intel_bufmgr.h"
#include "nouveau.h"

#define BO_SIZE (256*1024)

int intel_fd = -1, intel_fd2 = -1, nouveau_fd = -1, nouveau_fd2 = -1;
drm_intel_bufmgr *bufmgr;
drm_intel_bufmgr *bufmgr2;
struct nouveau_device *ndev, *ndev2;
struct nouveau_client *nclient, *nclient2;
uint32_t devid;
struct intel_batchbuffer *intel_batch;

static void find_and_open_devices(void)
{
	int i;
	char path[80];
	struct stat buf;
	FILE *fl;
	char vendor_id[8];
	int venid;
	for (i = 0; i < 9; i++) {
		char *ret;

		sprintf(path, "/sys/class/drm/card%d/device/vendor", i);
		if (stat(path, &buf))
			break;

		fl = fopen(path, "r");
		if (!fl)
			break;

		ret = fgets(vendor_id, 8, fl);
		igt_assert(ret);
		fclose(fl);

		venid = strtoul(vendor_id, NULL, 16);
		sprintf(path, "/dev/dri/card%d", i);
		if (venid == 0x8086) {
			intel_fd = open(path, O_RDWR);
			igt_assert(intel_fd);
			intel_fd2 = open(path, O_RDWR);
			igt_assert(intel_fd2);
		} else if (venid == 0x10de) {
			nouveau_fd = open(path, O_RDWR);
			igt_assert(nouveau_fd);
			nouveau_fd2 = open(path, O_RDWR);
			igt_assert(nouveau_fd2);
		}
	}
}

static void test_i915_nv_import_twice(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	igt_assert(drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	igt_assert(nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2) == 0);
	close(prime_fd);

	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

static void test_i915_nv_import_twice_check_flink_name(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;
	uint32_t flink_name1, flink_name2;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	igt_assert(drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	igt_assert(nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2) == 0);
	close(prime_fd);

	igt_assert(nouveau_bo_name_get(nvbo, &flink_name1) == 0);
	igt_assert(nouveau_bo_name_get(nvbo2, &flink_name2) == 0);

	igt_assert_eq_u32(flink_name1, flink_name2);

	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

static void test_i915_nv_reimport_twice_check_flink_name(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;
	uint32_t flink_name1, flink_name2;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	igt_assert(drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);

	/* create a new dma-buf */
	close(prime_fd);
	igt_assert(drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2) == 0);
	close(prime_fd);

	igt_assert(nouveau_bo_name_get(nvbo, &flink_name1) == 0);
	igt_assert(nouveau_bo_name_get(nvbo2, &flink_name2) == 0);

	igt_assert_eq_u32(flink_name1, flink_name2);

	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

static void test_nv_i915_import_twice_check_flink_name(void)
{
	drm_intel_bo *intel_bo = NULL, *intel_bo2 = NULL;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t flink_name1, flink_name2;

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);

	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	igt_assert(intel_bo);

	intel_bo2 = drm_intel_bo_gem_create_from_prime(bufmgr2, prime_fd, BO_SIZE);
	igt_assert(intel_bo2);
	close(prime_fd);

	igt_assert(drm_intel_bo_flink(intel_bo, &flink_name1) == 0);
	igt_assert(drm_intel_bo_flink(intel_bo2, &flink_name2) == 0);

	igt_assert_eq_u32(flink_name1, flink_name2);

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(intel_bo);
	drm_intel_bo_unreference(intel_bo2);
}

static void test_nv_i915_reimport_twice_check_flink_name(void)
{
	drm_intel_bo *intel_bo = NULL, *intel_bo2 = NULL;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t flink_name1, flink_name2;

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);

	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	igt_assert(intel_bo);
	close(prime_fd);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	intel_bo2 = drm_intel_bo_gem_create_from_prime(bufmgr2, prime_fd, BO_SIZE);
	igt_assert(intel_bo2);
	close(prime_fd);

	igt_assert(drm_intel_bo_flink(intel_bo, &flink_name1) == 0);
	igt_assert(drm_intel_bo_flink(intel_bo2, &flink_name2) == 0);

	igt_assert_eq_u32(flink_name1, flink_name2);

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(intel_bo);
	drm_intel_bo_unreference(intel_bo2);
}

static void test_i915_nv_import_vs_close(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);
	igt_assert(test_intel_bo);

	igt_assert(drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	close(prime_fd);
	igt_assert(nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2) < 0);

	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* import handle twice on one driver */
static void test_i915_nv_double_import(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);
	igt_assert(test_intel_bo);

	igt_assert(drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo2) == 0);
	close(prime_fd);

	igt_assert(nvbo->handle == nvbo2->handle);

	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* export handle twice from one driver - import twice
   see if we get same object */
static void test_i915_nv_double_export(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd, prime_fd2;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);
	igt_assert(test_intel_bo);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd2);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	close(prime_fd);
	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd2, &nvbo2) == 0);
	close(prime_fd2);

	igt_assert(nvbo->handle == nvbo2->handle);

	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* export handle from intel driver - reimport to intel driver
   see if you get same object */
static void test_i915_self_import(void)
{
	drm_intel_bo *test_intel_bo, *test_intel_bo2;
	int prime_fd;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	test_intel_bo2 = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	igt_assert(test_intel_bo2);

	igt_assert(test_intel_bo->handle == test_intel_bo2->handle);

	drm_intel_bo_unreference(test_intel_bo);
}

/* nouveau export reimport test */
static void test_nv_self_import(void)
{
	int prime_fd;
	struct nouveau_bo *nvbo, *nvbo2;

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo2) == 0);
	close(prime_fd);

	igt_assert(nvbo->handle == nvbo2->handle);
	nouveau_bo_ref(NULL, &nvbo);
	nouveau_bo_ref(NULL, &nvbo2);
}

/* export handle from intel driver - reimport to another intel driver bufmgr
   see if you get same object */
static void test_i915_self_import_to_different_fd(void)
{
	drm_intel_bo *test_intel_bo, *test_intel_bo2;
	int prime_fd;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	test_intel_bo2 = drm_intel_bo_gem_create_from_prime(bufmgr2, prime_fd, BO_SIZE);
	close(prime_fd);
	igt_assert(test_intel_bo2);

	drm_intel_bo_unreference(test_intel_bo2);
	drm_intel_bo_unreference(test_intel_bo);
}

/* nouveau export reimport to other driver test */
static void test_nv_self_import_to_different_fd(void)
{
	int prime_fd;
	struct nouveau_bo *nvbo, *nvbo2;

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	igt_assert(nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2) == 0);
	close(prime_fd);

	/* not sure what to test for, just make sure we don't explode */
	nouveau_bo_ref(NULL, &nvbo);
	nouveau_bo_ref(NULL, &nvbo2);
}

igt_main
{
	igt_fixture {
		find_and_open_devices();

		igt_require(nouveau_fd != -1);
		igt_require(nouveau_fd2 != -1);
		igt_require(intel_fd != -1);
		igt_require(intel_fd2 != -1);

		/* set up intel bufmgr */
		bufmgr = drm_intel_bufmgr_gem_init(intel_fd, 4096);
		igt_assert(bufmgr);
		/* Do not enable reuse, we share (almost) all buffers. */
		//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

		bufmgr2 = drm_intel_bufmgr_gem_init(intel_fd2, 4096);
		igt_assert(bufmgr2);
		drm_intel_bufmgr_gem_enable_reuse(bufmgr2);

		/* set up nouveau bufmgr */
		igt_assert(nouveau_device_wrap(nouveau_fd, 0, &ndev) >= 0);
		igt_assert(nouveau_client_new(ndev, &nclient) >= 0);

		/* set up nouveau bufmgr */
		igt_assert(nouveau_device_wrap(nouveau_fd2, 0, &ndev2) >= 0);

		igt_assert(nouveau_client_new(ndev2, &nclient2) >= 0);;

		/* set up an intel batch buffer */
		devid = intel_get_drm_devid(intel_fd);
		intel_batch = intel_batchbuffer_alloc(bufmgr, devid);
		igt_assert(intel_batch);
	}

#define xtest(name) \
	igt_subtest(#name) \
		test_##name();

	xtest(i915_nv_import_twice);
	xtest(i915_nv_import_twice_check_flink_name);
	xtest(i915_nv_reimport_twice_check_flink_name);
	xtest(nv_i915_import_twice_check_flink_name);
	xtest(nv_i915_reimport_twice_check_flink_name);
	xtest(i915_nv_import_vs_close);
	xtest(i915_nv_double_import);
	xtest(i915_nv_double_export);
	xtest(i915_self_import);
	xtest(nv_self_import);
	xtest(i915_self_import_to_different_fd);
	xtest(nv_self_import_to_different_fd);
	
	igt_fixture {
		intel_batchbuffer_free(intel_batch);

		nouveau_device_del(&ndev);
		drm_intel_bufmgr_destroy(bufmgr);

		close(intel_fd);
		close(nouveau_fd);
	}
}
