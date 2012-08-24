/* wierd use of API tests */

/* test1- export buffer from intel, import same fd twice into nouveau,
   check handles match
   test2 - export buffer from intel, import fd once, close fd, try import again
   fail if it succeeds
   test3 - export buffer from intel, import twice on nouveau, check handle is the same
   test4 - export handle twice from intel, import into nouveau twice, check handle is the same
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "intel_bufmgr.h"
#include "nouveau.h"
#include "intel_gpu_tools.h"
#include "intel_batchbuffer.h"

#define BO_SIZE (256*1024)

int intel_fd = -1, intel_fd2 = -1, nouveau_fd = -1, nouveau_fd2 = -1;
drm_intel_bufmgr *bufmgr;
drm_intel_bufmgr *bufmgr2;
struct nouveau_device *ndev, *ndev2;
struct nouveau_client *nclient, *nclient2;
uint32_t devid;
struct intel_batchbuffer *intel_batch;

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
			intel_fd2 = open(path, O_RDWR);
			if (!intel_fd2)
				return -1;
		} else if (venid == 0x10de) {
			nouveau_fd = open(path, O_RDWR);
			if (!nouveau_fd)
				return -1;
			nouveau_fd2 = open(path, O_RDWR);
			if (!nouveau_fd2)
				return -1;
		}
	}
	return 0;
}

static int test1(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	ret = drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);
	if (ret)
		goto out;

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	if (ret < 0) {
		close(prime_fd);
		goto out;
	}
	ret = nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2);
	close(prime_fd);
	if (ret < 0)
		goto out;

	if (nvbo->handle != nvbo2->handle)
		ret = -1;
out:
	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}


static int test2(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	ret = drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);
	if (ret < 0)
		goto out;

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	close(prime_fd);
	if (ret < 0)
		goto out;
	ret = nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2);
	if (ret == 0)
		ret = -1;
	else
		ret = 0;

out:
	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}


/* import handle twice on one driver */
static int test3(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	ret = drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);
	if (ret < 0)
		goto out;

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	if (ret < 0) {
		close(prime_fd);
		goto out;
	}
	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo2);
	close(prime_fd);
	if (ret < 0)
		goto out;

	if (nvbo->handle != nvbo2->handle)
		ret = -1;

out:
	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* export handle twice from one driver - import twice
   see if we get same object */
static int test4(void)
{
	int ret;
	drm_intel_bo *test_intel_bo;
	int prime_fd, prime_fd2;
	struct nouveau_bo *nvbo = NULL, *nvbo2 = NULL;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd2);

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo);
	close(prime_fd);
	if (ret >= 0)
		ret = nouveau_bo_prime_handle_ref(ndev, prime_fd2, &nvbo2);
	close(prime_fd2);
	if (ret < 0)
		goto out;

	if (nvbo->handle != nvbo2->handle)
		ret = -1;

out:
	nouveau_bo_ref(NULL, &nvbo2);
	nouveau_bo_ref(NULL, &nvbo);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* export handle from intel driver - reimport to intel driver
   see if you get same object */
static int test5(void)
{
	int ret;
	drm_intel_bo *test_intel_bo, *test_intel_bo2;
	int prime_fd;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	test_intel_bo2 = drm_intel_bo_gem_create_from_prime(bufmgr, prime_fd, BO_SIZE);
	close(prime_fd);
	if (!test_intel_bo2) {
		ret = -1;
		goto out;
	}

	ret = 0;
	if (test_intel_bo->handle != test_intel_bo2->handle)
		ret = -1;

out:
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* nouveau export reimport test */
static int test6(void)
{
	int ret;
	int prime_fd;
	struct nouveau_bo *nvbo, *nvbo2;

	ret = nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			     0, BO_SIZE, NULL, &nvbo);
	if (ret < 0)
		return ret;
	ret = nouveau_bo_set_prime(nvbo, &prime_fd);
	if (ret < 0)
		return ret;

	ret = nouveau_bo_prime_handle_ref(ndev, prime_fd, &nvbo2);
	close(prime_fd);
	if (ret < 0)
		return ret;

	if (nvbo->handle != nvbo2->handle)
		fprintf(stderr,"mismatch handles %d %d\n", nvbo->handle, nvbo2->handle);
	nouveau_bo_ref(NULL, &nvbo);
	nouveau_bo_ref(NULL, &nvbo2);
	return 0;
}

/* export handle from intel driver - reimport to another intel driver bufmgr
   see if you get same object */
static int test7(void)
{
	int ret;
	drm_intel_bo *test_intel_bo, *test_intel_bo2;
	int prime_fd;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	test_intel_bo2 = drm_intel_bo_gem_create_from_prime(bufmgr2, prime_fd, BO_SIZE);
	close(prime_fd);
	if (!test_intel_bo2) {
		ret = -1;
		goto out;
	}

	ret = 0;
	/* not sure what to test for, just that we don't explode */
out:
	drm_intel_bo_unreference(test_intel_bo2);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

/* nouveau export reimport to other driver test */
static int test8(void)
{
	int ret;
	int prime_fd;
	struct nouveau_bo *nvbo, *nvbo2;

	ret = nouveau_bo_new(ndev, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
			     0, BO_SIZE, NULL, &nvbo);
	if (ret < 0)
		return ret;
	ret = nouveau_bo_set_prime(nvbo, &prime_fd);
	if (ret < 0)
		return ret;

	ret = nouveau_bo_prime_handle_ref(ndev2, prime_fd, &nvbo2);
	close(prime_fd);
	if (ret < 0)
		return ret;

	/* not sure what to test for, just make sure we don't explode */
	nouveau_bo_ref(NULL, &nvbo);
	nouveau_bo_ref(NULL, &nvbo2);
	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	ret = find_and_open_devices();
	if (ret < 0)
		return ret;

	if (nouveau_fd == -1 || intel_fd == -1 || nouveau_fd2 == -1 || intel_fd2 == -1) {
		fprintf(stderr,"failed to find intel and nouveau GPU\n");
		return 77;
	}

	/* set up intel bufmgr */
	bufmgr = drm_intel_bufmgr_gem_init(intel_fd, 4096);
	if (!bufmgr)
		return -1;
	/* Do not enable reuse, we share (almost) all buffers. */
	//drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	bufmgr2 = drm_intel_bufmgr_gem_init(intel_fd2, 4096);
	if (!bufmgr2)
		return -1;
	drm_intel_bufmgr_gem_enable_reuse(bufmgr2);

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

	/* set up nouveau bufmgr */
	ret = nouveau_device_wrap(nouveau_fd2, 0, &ndev2);
	if (ret < 0) {
		fprintf(stderr,"failed to wrap nouveau device\n");
		return 77;
	}

	ret = nouveau_client_new(ndev2, &nclient2);
	if (ret < 0) {
		fprintf(stderr,"failed to setup nouveau client\n");
		return -1;
	}

	/* set up an intel batch buffer */
	devid = intel_get_drm_devid(intel_fd);
	intel_batch = intel_batchbuffer_alloc(bufmgr, devid);

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
