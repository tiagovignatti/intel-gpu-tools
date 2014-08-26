/*
 * Copyright © 2008 Intel Corporation
 * Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#ifdef HAVE_STRUCT_SYSINFO_TOTALRAM
#include <sys/sysinfo.h>
#elif defined(HAVE_SWAPCTL) /* Solaris */
#include <sys/swap.h>
#endif

#include "intel_io.h"
#include "drmtest.h"
#include "igt_aux.h"

/**
 * intel_get_total_ram_mb:
 *
 * Returns:
 * The total amount of system RAM available in MB.
 */
uint64_t
intel_get_total_ram_mb(void)
{
	uint64_t retval;

#ifdef HAVE_STRUCT_SYSINFO_TOTALRAM /* Linux */
	struct sysinfo sysinf;
	int ret;

	ret = sysinfo(&sysinf);
	igt_assert(ret == 0);

	retval = sysinf.totalram;
	retval *= sysinf.mem_unit;
#elif defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES) /* Solaris */
	long pagesize, npages;

	pagesize = sysconf(_SC_PAGESIZE);
        npages = sysconf(_SC_PHYS_PAGES);

	retval = (uint64_t) pagesize * npages;
#else
#error "Unknown how to get RAM size for this OS"
#endif

	return retval / (1024*1024);
}

/**
 * intel_get_avail_ram_mb:
 *
 * Returns:
 * The amount of unused system RAM available in MB.
 */
uint64_t
intel_get_avail_ram_mb(void)
{
	uint64_t retval;

#ifdef HAVE_STRUCT_SYSINFO_TOTALRAM /* Linux */
	struct sysinfo sysinf;
	int fd, ret;

	fd = open("/proc/sys/vm/drop_caches", O_RDWR);
	if (fd != -1) {
		ret = write(fd, "3\n", 2);
		close(fd);
		(void)ret;
	}

	ret = sysinfo(&sysinf);
	igt_assert(ret == 0);

	retval = sysinf.freeram;
	retval *= sysinf.mem_unit;
#elif defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES) /* Solaris */
	long pagesize, npages;

	pagesize = sysconf(_SC_PAGESIZE);
        npages = sysconf(_SC_AVPHYS_PAGES);

	retval = (uint64_t) pagesize * npages;
#else
#error "Unknown how to get available RAM for this OS"
#endif

	return retval / (1024*1024);
}

/**
 * intel_get_total_swap_mb:
 *
 * Returns:
 * The total amount of swap space available in MB.
 */
uint64_t
intel_get_total_swap_mb(void)
{
	uint64_t retval;

#ifdef HAVE_STRUCT_SYSINFO_TOTALRAM /* Linux */
	struct sysinfo sysinf;
	int ret;

	ret = sysinfo(&sysinf);
	igt_assert(ret == 0);

	retval = sysinf.freeswap;
	retval *= sysinf.mem_unit;
#elif defined(HAVE_SWAPCTL) /* Solaris */
	long pagesize = sysconf(_SC_PAGESIZE);
	uint64_t totalpages = 0;
	swaptbl_t *swt;
	char *buf;
	int n, i;

	if ((n = swapctl(SC_GETNSWP, NULL)) == -1) {
	    igt_warn("swapctl: GETNSWP");
	    return 0;
	}
	if (n == 0) {
	    /* no error, but no swap devices either */
	    return 0;
	}

	swt = malloc(sizeof(struct swaptable) + (n * sizeof(swapent_t)));
	buf = malloc(n * MAXPATHLEN);
	if (!swt || !buf) {
	    igt_warn("malloc");
	} else {
	    swt->swt_n = n;
	    for (i = 0 ; i < n; i++) {
		swt->swt_ent[i].ste_path = buf + (i * MAXPATHLEN);
	    }

	    if ((n = swapctl(SC_LIST, swt)) == -1) {
		igt_warn("swapctl: LIST");
	    } else {
		for (i = 0; i < swt->swt_n; i++) {
		    totalpages += swt->swt_ent[i].ste_pages;
		}
	    }
	}
	free(swt);
	free(buf);

	retval = (uint64_t) pagesize * totalpages;
#else
#warning "Unknown how to get swap size for this OS"
	return 0;
#endif

	return retval / (1024*1024);
}

/**
 * intel_check_memory:
 * @count: number of surfaces that will be created
 * @size: the size in bytes of each surface
 * @mode: a bitfield declaring whether the test will be run in RAM or in SWAP
 *
 * Computes the total amount of memory required to allocate @count surfaces,
 * each of @size bytes, and includes an estimate for kernel overhead. It then
 * queries the kernel for the avilable amount of memory on the system (either
 * RAM and/or SWAP depending upon @mode) and determines whether there is
 * sufficient to run the test.
 *
 * Most tests should check that there is enough RAM to hold their working set.
 * The rare swap thrashing tests should check that there is enough RAM + SWAP
 * for their tests. oom-killer tests should only run if this reports that
 * there is not enough RAM + SWAP!
 *
 * Returns:
 * Whether the estimated amount of memory required for the objects
 * fits in the available memory on the system.
 */
bool intel_check_memory(uint32_t count, uint32_t size, unsigned mode)
{
/* rough estimate of how many bytes the kernel requires to track each object */
#define KERNEL_BO_OVERHEAD 512
	uint64_t required, total;

	required = count;
	required *= size + KERNEL_BO_OVERHEAD;
	required = ALIGN(required, 4096);

	igt_debug("Checking %u surfaces of size %u bytes (total %llu) against %s%s\n",
		  count, size, (long long)required,
		  mode & (CHECK_RAM | CHECK_SWAP) ? "RAM" : "",
		  mode & CHECK_SWAP ? " + swap": "");

	total = 0;
	if (mode & (CHECK_RAM | CHECK_SWAP))
		total += intel_get_avail_ram_mb();
	if (mode & CHECK_SWAP)
		total += intel_get_total_swap_mb();
	total *= 1024 * 1024;

	if (total <= required) {
		igt_log(IGT_LOG_INFO,
			"Estimated that we need %llu bytes for the test, but only have %llu bytes available (%s%s)\n",
			(long long)required, (long long)total,
			mode & (CHECK_RAM | CHECK_SWAP) ? "RAM" : "",
			mode & CHECK_SWAP ? " + swap": "");
		return 0;
	}

	return 1;
}


void
intel_purge_vm_caches(void)
{
	int fd;

	fd = open("/proc/sys/vm/drop_caches", O_RDWR);
	if (fd < 0)
		return;

	write(fd, "3\n", 2);
	close(fd);
}


/*
 * When testing a port to a new platform, create a standalone test binary
 * by running:
 * cc -o porttest intel_drm.c -I.. -DSTANDALONE_TEST `pkg-config --cflags libdrm`
 * and then running the resulting porttest program.
 */
#ifdef STANDALONE_TEST
void *mmio;

int main(int argc, char **argv)
{
    igt_info("Total RAM:  %"PRIu64" Mb\n", intel_get_total_ram_mb());
    igt_info("Total Swap: %"PRIu64" Mb\n", intel_get_total_swap_mb());

    return 0;
}
#endif /* STANDALONE_TEST */
