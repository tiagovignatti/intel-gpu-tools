/*
 * Copyright © 2008 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "intel_gpu_tools.h"

void *mmio;

void
intel_map_file(char *file)
{
	int fd;
	struct stat st;

	fd = open(file, O_RDWR);
	if (fd == -1) {
		    fprintf(stderr, "Couldn't open %s: %s\n", file,
			    strerror(errno));
		    exit(1);
	}
	fstat(fd, &st);
	mmio = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (mmio == MAP_FAILED) {
		    fprintf(stderr, "Couldn't mmap %s: %s\n", file,
			    strerror(errno));
		    exit(1);
	}
	close(fd);
}

void
intel_get_mmio(struct pci_device *pci_dev)
{
	uint32_t devid;
	int mmio_bar;
	int err;

	devid = pci_dev->device_id;
	if (IS_9XX(devid))
		mmio_bar = 0;
	else
		mmio_bar = 1;

	err = pci_device_map_range (pci_dev,
				    pci_dev->regions[mmio_bar].base_addr,
				    pci_dev->regions[mmio_bar].size,
				    PCI_DEV_MAP_FLAG_WRITABLE,
				    &mmio);

	if (err != 0) {
		fprintf(stderr, "Couldn't map MMIO region: %s\n",
			strerror(err));
		exit(1);
	}
}

