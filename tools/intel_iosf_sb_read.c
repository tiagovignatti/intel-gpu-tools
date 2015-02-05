/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include "intel_io.h"
#include "intel_chipset.h"
#include "drmtest.h"

/* keep sorted by name for bsearch() */
static const struct iosf_sb_port {
	const char *name;
	uint8_t port;
	uint8_t reg_stride;
} iosf_sb_ports[] = {
	{ "bunit",   0x03, 1, },
	{ "cck",     0x14, 1, },
	{ "ccu",     0xa9, 4, },
	{ "dpio",    0x12, 4, },
	{ "dpio2",   0x1a, 4, },
	{ "flisdsi", 0x1b, 1, },
	{ "gpio_nc", 0x13, 4, },
	{ "nc",      0x11, 4, },
	{ "punit",   0x04, 1, },
};

static int iosf_sb_port_compare(const void *a, const void *b)
{
	const char *name = a;
	const struct iosf_sb_port *p = b;

	return strcasecmp(name, p->name);
}

static int iosf_sb_port_parse(const char *name, int *reg_stride)
{
	const struct iosf_sb_port *p;

	p = bsearch(name, iosf_sb_ports, ARRAY_SIZE(iosf_sb_ports),
		    sizeof(iosf_sb_ports[0]),
		    iosf_sb_port_compare);
	if (p) {
		*reg_stride = p->reg_stride;
		return p->port;
	}

	*reg_stride = 4;
	return strtoul(name, NULL, 16);
}

static void usage(const char *name)
{
	int i;

	printf("Warning : This program will work only on Valleyview/Cherryview\n"
	       "Usage: %s [-h] [-c <count>] [--] <port> <reg> [<reg> ...]\n"
	       "\t -h : Show this help text\n"
	       "\t -c <count> : how many consecutive registers to read\n"
	       "\t <port> : ", name);
	for (i = 0; i < ARRAY_SIZE(iosf_sb_ports); i++)
		printf("%s,", iosf_sb_ports[i].name);
	printf(" or in hex\n"
	       "\t <reg> : in hex\n");
}

int main(int argc, char *argv[])
{
	uint32_t port, reg, val;
	struct pci_device *dev = intel_get_pci_device();
	int i, nregs, count = 1, reg_stride;
	const char *name;

	if (!IS_VALLEYVIEW(dev->device_id) &&
	    !IS_CHERRYVIEW(dev->device_id)) {
		usage(argv[0]);
		return 1;
	}

	for (;;) {
		int c = getopt(argc, argv, "hc:");

		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 'c':
			count = strtol(optarg, NULL, 0);
			if (count < 1) {
				usage(argv[0]);
				return 3;
			}
			break;
		}
	}

	nregs = argc - optind;
	if (nregs < 1) {
		usage(argv[0]);
		return 2;
	}

	i = optind;
	name = argv[i++];
	port = iosf_sb_port_parse(name, &reg_stride);

	intel_register_access_init(dev, 0);

	for (; i < argc; i++) {
		int j;

		reg = strtoul(argv[i], NULL, 16);

		for (j = 0; j < count; j++) {
			val = intel_iosf_sb_read(port, reg);
			printf("0x%02x(%s)/0x%04x : 0x%08x\n", port, name, reg, val);
			reg += reg_stride;
		}
	}

	intel_register_access_fini();

	return 0;
}
