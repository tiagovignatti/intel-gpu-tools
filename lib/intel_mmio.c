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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "intel_gpu_tools.h"

void *mmio;

static struct _mmio_data {
	int inited;
	bool safe;
	char debugfs_path[FILENAME_MAX];
	char debugfs_forcewake_path[FILENAME_MAX];
	uint32_t i915_devid;
	struct intel_register_map map;
	int key;
} mmio_data;

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
	uint32_t devid, gen;
	int mmio_bar, mmio_size;
	int error;

	devid = pci_dev->device_id;
	if (IS_GEN2(devid))
		mmio_bar = 1;
	else
		mmio_bar = 0;

	gen = intel_gen(devid);
	if (gen < 3)
		mmio_size = 64*1024;
	else if (gen < 5)
		mmio_size = 512*1024;
	else
		mmio_size = 2*1024*1024;

	error = pci_device_map_range (pci_dev,
				      pci_dev->regions[mmio_bar].base_addr,
				      mmio_size,
				      PCI_DEV_MAP_FLAG_WRITABLE,
				      &mmio);

	if (error != 0) {
		fprintf(stderr, "Couldn't map MMIO region: %s\n",
			strerror(error));
		exit(1);
	}
}

/*
 * If successful, i915_debugfs_path and i915_debugfs_forcewake_path are both
 * updated with the correct path.
 */
static int
find_debugfs_path(const char *dri_base)
{
	char buf[FILENAME_MAX];
	struct stat sb;
	int i, ret;

	for (i = 0; i < 16; i++) {
		snprintf(buf, FILENAME_MAX, "%s/%i/name", dri_base, i);

		snprintf(mmio_data.debugfs_path, FILENAME_MAX,
			 "%s/%i/", dri_base, i);
		snprintf(mmio_data.debugfs_forcewake_path, FILENAME_MAX,
			 "%s/%i/i915_forcewake_user", dri_base, i);

		ret = stat(mmio_data.debugfs_forcewake_path, &sb);
		if (ret) {
			mmio_data.debugfs_path[0] = 0;
			mmio_data.debugfs_forcewake_path[0] = 0;
		} else
			return 0;
	}

	return -1;
}

static int
get_forcewake_lock(void)
{
	return open(mmio_data.debugfs_forcewake_path, 0);
}

static void
release_forcewake_lock(int fd)
{
	close(fd);
}

/*
 * Initialize register access library.
 *
 * @pci_dev: pci device we're mucking with
 * @safe: use safe register access tables
 */
int
intel_register_access_init(struct pci_device *pci_dev, int safe)
{
	int ret;

	/* after old API is deprecated, remove this */
	if (mmio == NULL)
		intel_get_mmio(pci_dev);

	assert(mmio != NULL);

	if (mmio_data.inited)
		return -1;

	mmio_data.safe = safe != 0 ? true : false;
	mmio_data.i915_devid = pci_dev->device_id;
	if (mmio_data.safe)
		mmio_data.map = intel_get_register_map(mmio_data.i915_devid);

	if (!(IS_GEN6(pci_dev->device_id) ||
	      IS_GEN7(pci_dev->device_id)))
		goto done;

	/* Find where the forcewake lock is */
	ret = find_debugfs_path("/sys/kernel/debug/dri");
	if (ret) {
		ret = find_debugfs_path("/debug/dri");
		if (ret) {
			fprintf(stderr, "Couldn't find path to dri/debugfs entry\n");
			return ret;
		}
	}
	mmio_data.key = get_forcewake_lock();

done:
	mmio_data.inited++;
	return 0;
}

void
intel_register_access_fini(void)
{
	if (mmio_data.key)
		release_forcewake_lock(mmio_data.key);
	mmio_data.inited--;
}

uint32_t
intel_register_read(uint32_t reg)
{
	struct intel_register_range *range;
	uint32_t ret;

	assert(mmio_data.inited);

	if (intel_gen(mmio_data.i915_devid) >= 6)
		assert(mmio_data.key != -1);

	if (!mmio_data.safe)
		goto read_out;

	range = intel_get_register_range(mmio_data.map,
					 reg,
					 INTEL_RANGE_READ);

	if(!range) {
		fprintf(stderr, "Register read blocked for safety "
			"(*0x%08x)\n", reg);
		ret = 0xffffffff;
		goto out;
	}

read_out:
	ret = *(volatile uint32_t *)((volatile char *)mmio + reg);
out:
	return ret;
}

void
intel_register_write(uint32_t reg, uint32_t val)
{
	struct intel_register_range *range;

	assert(mmio_data.inited);

	if (intel_gen(mmio_data.i915_devid) >= 6)
		assert(mmio_data.key != -1);

	if (!mmio_data.safe)
		goto write_out;

	range = intel_get_register_range(mmio_data.map,
					 reg,
					 INTEL_RANGE_WRITE);

	if (!range) {
		fprintf(stderr, "Register write blocked for safety "
			"(*0x%08x = 0x%x)\n", reg, val);
	}

write_out:
	*(volatile uint32_t *)((volatile char *)mmio + reg) = val;
}
