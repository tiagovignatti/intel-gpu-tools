/*
 * Copyright © 2012 Intel Corporation
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
 * Authors:
 *		Vijay Purushothaman <vijay.a.purushothaman@intel.com>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include "intel_io.h"
#include "intel_chipset.h"

static void usage(char *cmdname)
{
	printf("Warning : This program will work only on Valleyview\n");
	printf("Usage: %s [addr]\n", cmdname);
	printf("\t addr : in 0xXXXX format\n");
}

int main(int argc, char** argv)
{
	int ret = 0;
	uint32_t reg, val;
	char *cmdname = strdup(argv[0]);
	struct pci_device *dev = intel_get_pci_device();

	if (argc != 2 || !IS_VALLEYVIEW(dev->device_id)) {
		usage(cmdname);
		ret = 1;
		goto out;
	}

	sscanf(argv[1], "0x%x", &reg);

	intel_register_access_init(dev, 0);

	ret = intel_punit_read(reg, &val);
	if (ret)
		fprintf(stderr, "iosf read failed: %d\n", ret);

	printf("Read IOSF register: 0x%x - Value : 0x%x\n", reg, val);

	intel_register_access_fini();

out:
	free(cmdname);
	return ret;
}
