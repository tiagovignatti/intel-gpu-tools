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
#include <errno.h>

#include "xf86drm.h"
#include <xf86drmMode.h>

#include "intel_bufmgr.h"

int intel_fd = -1, udl_fd = -1;
drm_intel_bufmgr *bufmgr;
uint32_t devid;
struct intel_batchbuffer *intel_batch;

#define BO_SIZE (640*480*2)

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
		if (stat(path, &buf)) {
			/* look for usb dev */
			sprintf(path, "/sys/class/drm/card%d/device/idVendor", i);
			if (stat(path, &buf))
				break;
		}

		fl = fopen(path, "r");
		if (!fl)
			break;

		igt_assert(fgets(vendor_id, 8, fl) != NULL);
		fclose(fl);

		venid = strtoul(vendor_id, NULL, 16);
		sprintf(path, "/dev/dri/card%d", i);
		if (venid == 0x8086) {
			intel_fd = open(path, O_RDWR);
			if (!intel_fd)
				return -1;
		} else if (venid == 0x17e9) {
			udl_fd = open(path, O_RDWR);
			if (!udl_fd)
				return -1;
		}
	}
	return 0;
}

static int dumb_bo_destroy(int fd, uint32_t handle)
{

	struct drm_mode_destroy_dumb arg;
	int ret;
	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
	if (ret)
		return -errno;
	return 0;

}

/*
 * simple share and import
 */
static int test1(void)
{
	drm_intel_bo *test_intel_bo;
	int prime_fd;
	int ret;
	uint32_t udl_handle;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	ret = drmPrimeFDToHandle(udl_fd, prime_fd, &udl_handle);

	dumb_bo_destroy(udl_fd, udl_handle);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

static int test2(void)
{
	drm_intel_bo *test_intel_bo;
	uint32_t fb_id;
	drmModeClip clip;
	int prime_fd;
	uint32_t udl_handle;
	int ret;

	test_intel_bo = drm_intel_bo_alloc(bufmgr, "test bo", BO_SIZE, 4096);

	drm_intel_bo_gem_export_to_prime(test_intel_bo, &prime_fd);

	ret = drmPrimeFDToHandle(udl_fd, prime_fd, &udl_handle);
	if (ret)
		goto out;

	ret = drmModeAddFB(udl_fd, 640, 480, 16, 16, 640, udl_handle, &fb_id);
	if (ret)
		goto out;

	clip.x1 = 0;
	clip.y1 = 0;
	clip.x2 = 10;
	clip.y2 = 10;
	ret = drmModeDirtyFB(udl_fd, fb_id, &clip, 1);
	if (ret) {
		return ret;
	}
out:
	dumb_bo_destroy(udl_fd, udl_handle);
	drm_intel_bo_unreference(test_intel_bo);
	return ret;
}

igt_simple_main
{
	igt_skip_on_simulation();

	igt_assert(find_and_open_devices() >= 0);

	igt_skip_on(udl_fd == -1);
	igt_skip_on(intel_fd == -1);

	/* set up intel bufmgr */
	bufmgr = drm_intel_bufmgr_gem_init(intel_fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);

	/* set up an intel batch buffer */
	devid = intel_get_drm_devid(intel_fd);
	intel_batch = intel_batchbuffer_alloc(bufmgr, devid);

	/* create an object on the i915 */
	igt_assert(test1() == 0);

	igt_assert(test2() == 0);

	intel_batchbuffer_free(intel_batch);

	drm_intel_bufmgr_destroy(bufmgr);

	close(intel_fd);
	close(udl_fd);
}
