#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>

#include "debugfs.h"

int debugfs_init(void)
{
	struct stat st;

	if (stat("/sys/kernel/debug/dri", &st) == 0)
		return 0;

	if (stat("/sys/kernel/debug", &st))
		return errno;

	if (mount("debug", "/sys/kernel/debug", "debugfs", 0, 0))
		return errno;

	return 0;
}
