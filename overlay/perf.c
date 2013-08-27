#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "perf.h"

uint64_t i915_type_id(void)
{
	char buf[1024];
	int fd, n;

	fd = open("/sys/bus/event_source/devices/i915/type", 0);
	if (fd < 0) {
		n = -1;
	} else {
		n = read(fd, buf, sizeof(buf)-1);
		close(fd);
	}
	if (n < 0)
		return 0;

	buf[n] = '\0';
	return strtoull(buf, 0, 0);
}

