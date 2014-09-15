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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *  Arun Siluvery <arun.siluvery@linux.intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <signal.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_aux.h"
#include "intel_chipset.h"
#include "intel_io.h"

enum operation {
	GPU_RESET = 0x01,
	SUSPEND_RESUME = 0x02,
	SIMPLE_READ = 0x03,
};

struct intel_wa_reg {
	uint32_t addr;
	uint32_t value;
	uint32_t mask;
};

static int drm_fd;
static uint32_t devid;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
static int num_wa_regs;

static struct intel_wa_reg *wa_regs;

static void wait_gpu(void)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec;
	uint32_t b[2] = {MI_BATCH_BUFFER_END};

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec.handle = gem_create(drm_fd, 4096);
	gem_write(drm_fd, gem_exec.handle, 0, b, sizeof(b));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_len = sizeof(b);

	drmIoctl(drm_fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

	gem_sync(drm_fd, gem_exec.handle);

	gem_close(drm_fd, gem_exec.handle);
}

static void test_hang_gpu(void)
{
	int retry_count = 30;
	enum stop_ring_flags flags;

	igt_assert(retry_count);
	igt_set_stop_rings(STOP_RING_DEFAULTS);

	wait_gpu();

	while(retry_count--) {
		flags = igt_get_stop_rings();
		if (flags == 0)
			break;
		igt_info("gpu hang not yet cleared, retries left %d\n", retry_count);
		sleep(1);
	}

	flags = igt_get_stop_rings();
	if (flags)
		igt_set_stop_rings(STOP_RING_NONE);
}

static void test_suspend_resume(void)
{
	igt_info("Suspending the device ...\n");
	igt_system_suspend_autoresume();
}

static int workaround_fail_count(void)
{
	int i, fail_count = 0;

	intel_register_access_init(intel_get_pci_device(), 0);

	/* There is a small delay after coming ot of rc6 to the correct
	   render context values will get loaded by hardware (bdw,chv).
	   This here ensures that we have the correct context loaded before
	   we start to read values */
	wait_gpu();

	igt_debug("Address\tval\t\tmask\t\tread\t\tresult\n");

	for (i = 0; i < num_wa_regs; ++i) {
		const uint32_t val = intel_register_read(wa_regs[i].addr);
		const bool ok = (wa_regs[i].value & wa_regs[i].mask) ==
			(val & wa_regs[i].mask);

		igt_debug("0x%05X\t0x%08X\t0x%08X\t0x%08X\t%s\n",
			  wa_regs[i].addr, wa_regs[i].value, wa_regs[i].mask,
			  val, ok ? "OK" : "FAIL");

		if (!ok) {
			igt_warn("0x%05X\t0x%08X\t0x%08X\t0x%08X\t%s\n",
				 wa_regs[i].addr, wa_regs[i].value,
				 wa_regs[i].mask,
				 val, ok ? "OK" : "FAIL");
			fail_count++;
		}
	}

	intel_register_access_fini();

	return fail_count;
}

static void check_workarounds(enum operation op)
{
	igt_assert(workaround_fail_count() == 0);

	switch (op) {
	case GPU_RESET:
		test_hang_gpu();
		break;

	case SUSPEND_RESUME:
		test_suspend_resume();
		break;

	case SIMPLE_READ:
		return;

	default:
		igt_assert(0);
	}

	igt_assert(workaround_fail_count() == 0);
}

igt_main
{
	igt_fixture {
		int i;
		int fd;
		int ret;
		FILE *file;
		char *line = NULL;
		size_t line_size;

		drm_fd = drm_open_any();

		bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
		devid = intel_get_drm_devid(drm_fd);
		batch = intel_batchbuffer_alloc(bufmgr, devid);

		fd = igt_debugfs_open("i915_wa_registers", O_RDONLY);
		igt_assert(fd >= 0);

		file = fdopen(fd, "r");
		igt_assert(file > 0);

		ret = getline(&line, &line_size, file);
		igt_assert(ret > 0);
		sscanf(line, "Workarounds applied: %d", &num_wa_regs);

		if (IS_BROADWELL(devid) ||
		    IS_CHERRYVIEW(devid))
			igt_assert(num_wa_regs > 0);
		else
			igt_assert(num_wa_regs >= 0);

		wa_regs = malloc(num_wa_regs * sizeof(*wa_regs));

		i = 0;
		while(getline(&line, &line_size, file) > 0) {
			sscanf(line, "0x%X: 0x%08X, mask: 0x%08X",
			       &wa_regs[i].addr, &wa_regs[i].value,
			       &wa_regs[i].mask);
			++i;
		}

		free(line);
		fclose(file);
		close(fd);
	}

	igt_subtest("read")
		check_workarounds(SIMPLE_READ);

	igt_subtest("reset")
		check_workarounds(GPU_RESET);

	igt_subtest("suspend-resume")
		check_workarounds(SUSPEND_RESUME);

	igt_fixture {
		free(wa_regs);
		close(drm_fd);
	}
}
