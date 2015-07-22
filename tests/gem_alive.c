#include "igt.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <i915_drm.h>


int main(void)
{
	struct drm_i915_gem_sw_finish arg = { 0 };
	int fd;

	signal(SIGALRM, SIG_IGN);

	fd = __drm_open_driver(DRIVER_INTEL);
	if (fd < 0)
		return IGT_EXIT_SKIP;

	alarm(1);
	if (ioctl(fd, DRM_IOCTL_I915_GEM_SW_FINISH, &arg) == 0)
		return IGT_EXIT_SKIP;

	switch (errno) {
	case ENOENT:
		return 0;
	case EIO:
		return 1;
	case EINTR:
		return 2;
	default:
		return 3;
	}
}
