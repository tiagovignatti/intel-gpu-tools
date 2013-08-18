#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "gpu-freq.h"

int gpu_freq_init(struct gpu_freq *gf)
{
	char buf[4096], *s;
	int fd, len = -1;

	memset(gf, 0, sizeof(*gf));

	fd = open("/sys/kernel/debug/dri/0/i915_cur_delayinfo", 0);
	if (fd < 0)
		return errno;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (len < 0)
		return EIO;

	buf[len] = '\0';

	s = strstr(buf, "(RPN)");
	if (s == NULL)
		return EIO;
	sscanf(s, "(RPN) frequency: %dMHz", &gf->rpn);

	s = strstr(s, "(RP1)");
	if (s == NULL)
		return EIO;
	sscanf(s, "(RP1) frequency: %dMHz", &gf->rp1);

	s = strstr(s, "(RP0)");
	if (s == NULL)
		return EIO;
	sscanf(s, "(RP0) frequency: %dMHz", &gf->rp0);

	s = strstr(s, "Max");
	if (s == NULL)
		return EIO;
	sscanf(s, "Max overclocked frequency: %dMHz", &gf->max);
	gf->min = gf->rpn;

	return 0;
}

int gpu_freq_update(struct gpu_freq *gf)
{
	char buf[4096], *s;
	int fd, len = -1;

	fd = open("/sys/kernel/debug/dri/0/i915_cur_delayinfo", 0);
	if (fd < 0)
		return errno;

	len = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (len < 0)
		return EIO;

	buf[len] = '\0';

	s = strstr(buf, "RPNSWREQ:");
	if (s)
		sscanf(s, "RPNSWREQ: %dMHz", &gf->request);

	s = strstr(buf, "CAGF:");
	if (s)
		sscanf(s, "CAGF: %dMHz", &gf->current);

	return 0;
}
