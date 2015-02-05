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
} iosf_sb_ports[] = {
	{ "bunit",   0x03, },
	{ "cck",     0x14, },
	{ "ccu",     0xa9, },
	{ "dpio",    0x12, },
	{ "dpio2",   0x1a, },
	{ "flisdsi", 0x1b, },
	{ "gpio_nc", 0x13, },
	{ "nc",      0x11, },
	{ "punit",   0x04, },
};

static int iosf_sb_port_compare(const void *a, const void *b)
{
	const char *name = a;
	const struct iosf_sb_port *p = b;

	return strcasecmp(name, p->name);
}

static int iosf_sb_port_parse(const char *name)
{
	const struct iosf_sb_port *p;

	p = bsearch(name, iosf_sb_ports, ARRAY_SIZE(iosf_sb_ports),
		    sizeof(iosf_sb_ports[0]),
		    iosf_sb_port_compare);
	if (p)
		return p->port;

	return strtoul(name, NULL, 16);
}

static void usage(const char *name)
{
	int i;

	printf("Warning : This program will work only on Valleyview/Cherryview\n"
	       "Usage: %s [-h] [--] <port> <reg> <val>\n"
	       "\t -h : Show this help text\n"
	       "\t <port> : ", name);
	for (i = 0; i < ARRAY_SIZE(iosf_sb_ports); i++)
		printf("%s,", iosf_sb_ports[i].name);
	printf(" or in hex\n"
	       "\t <reg> : in hex\n"
	       "\t <val> : in hex\n");
}

int main(int argc, char** argv)
{
	uint32_t port, reg, val, tmp;
	struct pci_device *dev = intel_get_pci_device();
	int i, nregs;
	const char *name;

	if (!IS_VALLEYVIEW(dev->device_id) &&
	    !IS_CHERRYVIEW(dev->device_id)) {
		usage(argv[0]);
		return 1;
	}

	for (;;) {
		int c = getopt(argc, argv, "h");

		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage(argv[0]);
			return 0;
		}
	}

	nregs = argc - optind;
	if (nregs < 2) {
		usage(argv[0]);
		return 2;
	}

	i = optind;
	name = argv[i++];
	port = iosf_sb_port_parse(name);

	reg = strtoul(argv[i], NULL, 16);
	val = strtoul(argv[i+1], NULL, 16);

	intel_register_access_init(dev, 0);

	tmp = intel_iosf_sb_read(port, reg);
	printf("0x%02x(%s)/0x%04x before : 0x%08x\n", port, name, reg, tmp);

	intel_iosf_sb_write(port, reg, val);

	tmp = intel_iosf_sb_read(port, reg);
	printf("0x%02x(%s)/0x%04x after  : 0x%08x\n", port, name, reg, tmp);

	intel_register_access_fini();

	return 0;
}
