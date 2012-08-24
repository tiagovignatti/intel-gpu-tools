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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "i915_drm.h"
#include "intel_bufmgr.h"
#include "nouveau.h"
#include "intel_gpu_tools.h"
#include "intel_batchbuffer.h"

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
		sprintf(path, "/sys/class/drm/card%d/device/vendor", i);
		if (stat(path, &buf))
			break;

		fl = fopen(path, "r");
		if (!fl)
			break;

		fgets(vendor_id, 8, fl);
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
static int test1(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	close(prime_fd);
	if (ret < 0)
		return ret;

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return 0;
}

/*
 * prime test 2 -
 * allocate buffer on nouveau
 * set prime on buffer,
 * retrive buffer from intel
 * close prime_fd,
 *  unref buffers
 */
static int test2(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;

	ret = nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			     0, BO_SIZE, NULL, &nvbo);
	if (ret < 0)
		return ret;
	ret = nouveau_bo_set_prime(nvbo, &prime_fd);
	if (ret < 0)
		return ret;

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	if (!test_intel_bo)
		return -1;

	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return 0;
}

/*
 * allocate intel, give to nouveau, map on nouveau
 * write 0xdeadbeef, non-gtt map on intel, read
 */
static int test3(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t *ptr;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	if (ret < 0) {
		fprintf(stderr,"failed to ref prime buffer %d\n", ret);
		close(prime_fd);
		goto free_intel;
	}
	close(prime_fd);
		goto free_intel;

	ret = nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to map nouveau bo\n");
		goto out;
	}

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	drm_intel_bo_map(test_intel_bo, 1);

	ptr = test_intel_bo->virtual;

	if (*ptr != 0xdeadbeef) {
		fprintf(stderr,"mapped value doesn't match\n");
		ret = -1;
	}
out:
	nouveau_bo_ref(NULL, &nvbo);
free_intel:
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/*
 * allocate intel, give to nouveau, map on nouveau
 * write 0xdeadbeef, gtt map on intel, read
 */
static int test4(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t *ptr;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	close(prime_fd);
	if (ret < 0) {
		fprintf(stderr,"failed to ref prime buffer\n");
		return ret;
	}

	ret = nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to map nouveau bo\n");
		goto out;
	}


	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	drm_intel_gem_bo_map_gtt(test_intel_bo);
	ptr = test_intel_bo->virtual;

	if (*ptr != 0xdeadbeef) {
		fprintf(stderr,"mapped value doesn't match\n");
		ret = -1;
	}
out:
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* test drm_intel_bo_map doesn't work properly,
   this tries to map the backing shmem fd, which doesn't exist
   for these objects */
static int test5(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;
	uint32_t *ptr;

	ret = nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			     0, BO_SIZE, NULL, &nvbo);
	if (ret < 0)
		return ret;
	ret = nouveau_bo_set_prime(nvbo, &prime_fd);
	if (ret < 0)
		return ret;

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	if (!test_intel_bo)
		return -1;

	ret = nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to map nouveau bo\n");
		goto out;
	}

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	ret = drm_intel_bo_map(test_intel_bo, 0);
	if (ret != 0) {
		/* failed to map the bo is expected */
		ret = 0;
		goto out;
	}
	if (!test_intel_bo->virtual) {
		ret = 0;
		goto out;
	}
	ptr = test_intel_bo->virtual;

	if (*ptr != 0xdeadbeef) {
		fprintf(stderr,"mapped value doesn't match %08x\n", *ptr);
		ret = -1;
	}
 out:
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* test drm_intel_bo_map_gtt works properly,
   this tries to map the backing shmem fd, which doesn't exist
   for these objects */
static int test6(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;
	uint32_t *ptr;

	ret = nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			     0, BO_SIZE, NULL, &nvbo);
	if (ret < 0)
		return ret;
	ret = nouveau_bo_set_prime(nvbo, &prime_fd);
	if (ret < 0)
		return ret;

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	if (!test_intel_bo)
		return -1;

	ret = nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to map nouveau bo\n");
		goto out;
	}

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;
	*(ptr + 1) = 0xa55a55;

	ret = drm_intel_gem_bo_map_gtt(test_intel_bo);
	if (ret != 0) {
		fprintf(stderr,"failed to map bo\n");
		goto out;
	}
	if (!test_intel_bo->virtual) {
		ret = -1;
		fprintf(stderr,"failed to map bo\n");
		goto out;
	}
	ptr = test_intel_bo->virtual;

	if (*ptr != 0xdeadbeef) {
		fprintf(stderr,"mapped value doesn't match %08x %08x\n", *ptr, *(ptr + 1));
		ret = -1;
	}
 out:
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

static int do_read(int fd, int handle, void *buf, int offset, int size)
{
        struct drm_i915_gem_pread intel_pread;

        /* Ensure that we don't have any convenient data in buf in case
         * we fail.
         */
        memset(buf, 0xd0, size);

        memset(&intel_pread, 0, sizeof(intel_pread));
        intel_pread.handle = handle;
        intel_pread.data_ptr = (uintptr_t)buf;
        intel_pread.size = size;
        intel_pread.offset = offset;

        return ioctl(fd, DRM_IOCTL_I915_GEM_PREAD, &intel_pread);
}

static int do_write(int fd, int handle, void *buf, int offset, int size)
{
        struct drm_i915_gem_pwrite intel_pwrite;

        memset(&intel_pwrite, 0, sizeof(intel_pwrite));
        intel_pwrite.handle = handle;
        intel_pwrite.data_ptr = (uintptr_t)buf;
        intel_pwrite.size = size;
        intel_pwrite.offset = offset;

        return ioctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &intel_pwrite);
}

/* test 7 - import from nouveau into intel, test pread/pwrite fail */
static int test7(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo;
	uint32_t *ptr;
	uint32_t buf[64];

	ret = nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			     0, BO_SIZE, NULL, &nvbo);
	if (ret < 0)
		return ret;
	ret = nouveau_bo_set_prime(nvbo, &prime_fd);
	if (ret < 0)
		return ret;

	test_intel_bo = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	if (!test_intel_bo)
		return -1;

	ret = nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to map nouveau bo\n");
		goto out;
	}

	ptr = nvbo->map;
	*ptr = 0xdeadbeef;

	ret = do_read(intel_fd, test_intel_bo->handle, buf, 0, 256);
	if (ret != -1) {
		fprintf(stderr,"pread succeedded %d\n", ret);
		goto out;
	}
	buf[0] = 0xabcdef55;

	ret = do_write(intel_fd, test_intel_bo->handle, buf, 0, 4);
	if (ret != -1) {
		fprintf(stderr,"pwrite succeedded\n");
		goto out;
	}
	ret = 0;
 out:
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
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
        assert(bo);

        /* gtt map doesn't have a write parameter, so just keep the mapping
         * around (to avoid the set_domain with the gtt write domain set) and
         * manually tell the kernel when we start access the gtt. */
        drm_intel_gem_bo_map_gtt(bo);

        set_bo(bo, val, width, height);

        return bo;
}

/* use intel hw to fill the BO with a blit from another BO,
   then readback from the nouveau bo, check value is correct */
static int test8(void)
{
	int ret;
	drm_intel_bo *test_intel_bo, *src_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL;
	uint32_t *ptr;

	src_bo = create_bo(bufmgr, 0xaa55aa55, 256, 1);

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	close(prime_fd);
	if (ret < 0) {
		fprintf(stderr,"failed to ref prime buffer\n");
		return ret;
	}

	intel_copy_bo(intel_batch, test_intel_bo, src_bo, 256, 1);

	ret = nouveau_bo_map(nvbo, NOUVEAU_BO_RDWR, nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to map nouveau bo\n");
		goto out;
	}

	drm_intel_bo_map(test_intel_bo, 0);

	ptr = nvbo->map;
	if (*ptr != 0xaa55aa55) {
		fprintf(stderr,"mapped value doesn't match\n");
		ret = -1;
	}
out:
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* test 8 use nouveau to do blit */

/* test 9 nouveau copy engine?? */

int main(int argc, char **argv)
{
	int ret;

	ret = find_and_open_devices();
	if (ret < 0)
		return ret;

	if (nouveau_fd == -1 || intel_fd == -1) {
		fprintf(stderr,"failed to find intel and nouveau GPU\n");
		return 77;
	}

	/* set up intel bufmgr */
	bufmgr = drm_intel_bufmgr_gem_init(intel_fd, 4096);
	if (!bufmgr)
		return -1;
	/* Do not enable reuse, we share (almost) all buffers. */
	//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	/* set up nouveau bufmgr */
	ret = nouveau_device_wrap(nouveau_fd, 0, &ndev);
	if (ret < 0) {
		fprintf(stderr,"failed to wrap nouveau device\n");
		return 77;
	}

	ret = nouveau_client_new(ndev, &nclient);
	if (ret < 0) {
		fprintf(stderr,"failed to setup nouveau client\n");
		return -1;
	}

	/* set up an intel batch buffer */
	devid = intel_get_drm_devid(intel_fd);
	intel_batch = intel_batchbuffer_alloc(bufmgr, devid);

	/* create an object on the i915 */
	ret = test1();
	if (ret)
		fprintf(stderr,"prime_test: failed test 1\n");

	ret = test2();
	if (ret)
		fprintf(stderr,"prime_test: failed test 2\n");

	ret = test3();
	if (ret)
		fprintf(stderr,"prime_test: failed test 3\n");

	ret = test4();
	if (ret)
		fprintf(stderr,"prime_test: failed test 4\n");

	ret = test5();
	if (ret)
		fprintf(stderr,"prime_test: failed test 5\n");

	ret = test6();
	if (ret)
		fprintf(stderr,"prime_test: failed test 6\n");

	ret = test7();
	if (ret)
		fprintf(stderr,"prime_test: failed test 7\n");

	ret = test8();
	if (ret)
		fprintf(stderr,"prime_test: failed test 8\n");

	intel_batchbuffer_free(intel_batch);

	nouveau_device_del(&ndev);
	drm_intel_bufmgr_destroy(bufmgr);

	close(intel_fd);
	close(nouveau_fd);

	return ret;
}
