/* basic set of prime tests between intel and nouveau */

/* test list -
   1. share buffer from intel -> nouveau.
   2. share buffer from nouveau -> intel
   3. share intel->nouveau, map on both, write intel, read nouveau
   4. share intel->nouveau, blit intel fill, readback on nouveau
   test 1 + map buffer, read/write, map other size.
   do some hw actions on the buffer
   some illegal operations -
       close prime fd try and map

   TODO add some nouveau rendering tests
*/


#include "igt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "intel_bufmgr.h"
#include "nouveau.h"

int intel_fd = -1, nouveau_fd = -1;
drm_intel_bufmgr *bufmgr;
struct nouveau_device *ndev;
struct nouveau_client *nclient;
uint32_t devid;
struct intel_batchbuffer *intel_batch;

#define BO_SIZE (256*1024)

static int find_and_open_devices(void)
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
			if (!intel_fd)
				return -1;
		} else if (venid == 0x10de) {
			nouveau_fd = open(path, O_RDWR);
			if (!nouveau_fd)
				return -1;
		}
	}
	return 0;
}

/*
 * prime test 1 -
 * allocate buffer on intel,
 * set prime on buffer,
 * retrive buffer from nouveau,
 * close prime_fd,
 *  unref buffers
 */
static void test_i915_nv_sharing(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);
	igt_assert(test_intel_bo);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	close(prime_fd);

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/*
 * prime test 2 -
 * allocate buffer on nouveau
 * set prime on buffer,
 * retrive buffer from intel
 * close prime_fd,
 *  unref buffers
 */
static void test_nv_i915_sharing(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	igt_assert(test_intel_bo);

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/*
 * allocate intel, give to nouveau, map on nouveau
 * write 0xdeadbeef, non-gtt map on intel, read
 */
static void test_nv_write_i915_cpu_mmap_read(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t *ptr;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	close(prime_fd);

	igt_assert(nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient) == 0);
	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	drm_intel_bo_map(test_intel_bo, 1);
	ptr = test_intel_bo->virtual;
	igt_assert(ptr);

	igt_assert(*ptr == 0xdeadbeef);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/*
 * allocate intel, give to nouveau, map on nouveau
 * write 0xdeadbeef, gtt map on intel, read
 */
static void test_nv_write_i915_gtt_mmap_read(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t *ptr;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	close(prime_fd);
	igt_assert(nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient) == 0);
	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	drm_intel_gem_bo_map_gtt(test_intel_bo);
	ptr = test_intel_bo->virtual;
	igt_assert(ptr);

	igt_assert(*ptr == 0xdeadbeef);

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* test drm_intel_bo_map doesn't work properly,
   this tries to map the backing shmem fd, which doesn't exist
   for these objects */
static void test_i915_import_cpu_mmap(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;
	uint32_t *ptr;

	igt_skip("cpu mmap support for imported dma-bufs not yet implemented\n");

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);
	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	igt_assert(test_intel_bo);

	igt_assert(nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient) == 0);

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	igt_assert(drm_intel_bo_map(test_intel_bo, 0) == 0);
	igt_assert(test_intel_bo->virtual);
	ptr = test_intel_bo->virtual;

	igt_assert(*ptr == 0xdeadbeef);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* test drm_intel_bo_map_gtt works properly,
   this tries to map the backing shmem fd, which doesn't exist
   for these objects */
static void test_i915_import_gtt_mmap(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;
	uint32_t *ptr;

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	igt_assert(test_intel_bo);

	igt_assert(nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient) == 0);

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;
	*(ptr + 1) = 0xa55a55;

	igt_assert(drm_intel_gem_bo_map_gtt(test_intel_bo) == 0);
	igt_assert(test_intel_bo->virtual);
	ptr = test_intel_bo->virtual;

	igt_assert(*ptr == 0xdeadbeef);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* test 7 - import from nouveau into intel, test pread/pwrite fail */
static void test_i915_import_pread_pwrite(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;
	uint32_t *ptr;
	uint32_t buf[64];

	igt_assert(nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
				  0, BO_SIZE, NULL, &nvbo) == 0);
	igt_assert(nouveau_bo_set_prime(nvbo, &prime_fd) == 0);

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	igt_assert(test_intel_bo);

	igt_assert(nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient) == 0);

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	gem_read(intel_fd, test_intel_bo->handle, 0, buf, 256);
	igt_assert(buf[0] == 0xdeadbeef);
	buf[0] = 0xabcdef55;

	gem_write(intel_fd, test_intel_bo->handle, 0, buf, 4);

	igt_assert(*ptr == 0xabcdef55);

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

static void
set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
        int size = width * height;
        uint32_t *vaddr;

        drm_intel_gem_bo_start_gtt_access(bo, true);
        vaddr = bo->virtual;
        while (size--)
                *vaddr++ = val;
}

static drm_intel_bo *
create_bo(drm_intel_bufmgr *ibufmgr, uint32_t val, int width, int height)
{
        drm_intel_bo *bo;

        bo = drm_intel_bo_alloc(ibufmgr, "bo", 4*width*height, 0);
        igt_assert(bo);

        /* gtt map doesn't have a write parameter, so just keep the mapping
         * around (to avoid the set_domain with the gtt write domain set) and
         * manually tell the kernel when we start access the gtt. */
        drm_intel_gem_bo_map_gtt(bo);

        set_bo(bo, val, width, height);

        return bo;
}

/* use intel hw to fill the BO with a blit from another BO,
   then readback from the nouveau bo, check value is correct */
static void test_i915_blt_fill_nv_read(void)
{
	drm_intel_bo *test_intel_bo, *src_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t *ptr;

	src_bo = create_bo(bufmgr, 0xaa55aa55, 256, 1);

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	igt_assert(nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo) == 0);
	close(prime_fd);

	intel_copy_bo(intel_batch, test_intel_bo, src_bo, BO_SIZE);

	igt_assert(nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient) == 0);

	drm_intel_bo_map(test_intel_bo, 0);

	ptr = nvbo->map;
	igt_assert(*ptr == 0xaa55aa55);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
}

/* test 8 use nouveau to do blit */

/* test 9 nouveau copy engine?? */

igt_main
{
	igt_fixture {
		igt_assert(find_and_open_devices() == 0);

		igt_require(nouveau_fd != -1);
		igt_require(intel_fd != -1);

		/* set up intel bufmgr */
		bufmgr = drm_intel_bufmgr_gem_init(intel_fd, 4096);
		igt_assert(bufmgr);
		/* Do not enable reuse, we share (almost) all buffers. */
		//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

		/* set up nouveau bufmgr */
		igt_assert(nouveau_device_wrap(nouveau_fd, 0, &ndev) == 0);
		igt_assert(nouveau_client_new(ndev, &nclient) == 0);

		/* set up an intel batch buffer */
		devid = intel_get_drm_devid(intel_fd);
		intel_batch = intel_batchbuffer_alloc(bufmgr, devid);
	}

#define xtest(name) \
	igt_subtest(#name) \
		test_##name();

	xtest(i915_nv_sharing);
	xtest(nv_i915_sharing);
	xtest(nv_write_i915_cpu_mmap_read);
	xtest(nv_write_i915_gtt_mmap_read);
	xtest(i915_import_cpu_mmap);
	xtest(i915_import_gtt_mmap);
	xtest(i915_import_pread_pwrite);
	xtest(i915_blt_fill_nv_read);

	igt_fixture {
		intel_batchbuffer_free(intel_batch);

		nouveau_device_del(&ndev);
		drm_intel_bufmgr_destroy(bufmgr);

		close(intel_fd);
		close(nouveau_fd);
	}
}
