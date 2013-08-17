#include <unistd.h>
#include <fcntl.h>

#include "gem-objects.h"

int gem_objects_update(char *buf, int buflen)
{
	int fd, len = -1;

	fd = open("/sys/kernel/debug/dri/0/i915_gem_objects", 0);
	if (fd >= 0) {
		len = read(fd, buf, buflen-1);
		if (len >= 0)
			buf[len] = '\0';
		close(fd);
	}

	return len;
}
