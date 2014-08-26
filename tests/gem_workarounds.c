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
};

struct intel_wa_reg {
	uint32_t addr;
	uint32_t value;
	uint32_t mask;
};

int drm_fd;
uint32_t devid;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;
int num_wa_regs;
struct intel_wa_reg *wa_regs;


static void test_hang_gpu(void)
{
	int retry_count = 30;
	enum stop_ring_flags flags;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec;
	uint32_t b[2] = {MI_BATCH_BUFFER_END};

	igt_assert(retry_count);
	igt_set_stop_rings(STOP_RING_DEFAULTS);

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec.handle = gem_create(drm_fd, 4096);
	gem_write(drm_fd, gem_exec.handle, 0, b, sizeof(b));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&gem_exec;
	execbuf.buffer_count = 1;
	execbuf.batch_len = sizeof(b);

	drmIoctl(drm_fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

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

static void get_current_wa_data(struct intel_wa_reg **curr, int num)
{
	int i;
	struct intel_wa_reg *ptr = NULL;

	ptr = *curr;

	intel_register_access_init(intel_get_pci_device(), 0);

	for (i = 0; i < num; ++i) {
		ptr[i].addr = wa_regs[i].addr;
		ptr[i].value = intel_register_read(wa_regs[i].addr);
		ptr[i].mask = wa_regs[i].mask;
	}

	intel_register_access_fini();
}

static void check_workarounds(enum operation op, int num)
{
	int i;
	int fail_count = 0;
	int status = 0;
	struct intel_wa_reg *current_wa = NULL;

	switch (op) {
	case GPU_RESET:
		test_hang_gpu();
		break;

	case SUSPEND_RESUME:
		test_suspend_resume();
		break;

	default:
		fail_count = 1;
		goto out;
	}

	current_wa = malloc(num * sizeof(*current_wa));
	igt_assert(current_wa);
	get_current_wa_data(&current_wa, num);

	igt_info("Address\tbefore\t\tafter\t\tw/a mask\tresult\n");
	for (i = 0; i < num; ++i) {
		status = (current_wa[i].value & current_wa[i].mask) !=
			(wa_regs[i].value & wa_regs[i].mask);
		if (status)
			++fail_count;

		igt_info("0x%X\t0x%08X\t0x%08X\t0x%08X\t%s\n",
		       current_wa[i].addr, wa_regs[i].value,
		       current_wa[i].value, current_wa[i].mask,
		       status ? "fail" : "success");
	}

out:
	free(current_wa);
	igt_assert(fail_count == 0);
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

		fd = igt_debugfs_open("intel_wa_registers", O_RDONLY);
		igt_assert(fd >= 0);

		file = fdopen(fd, "r");
		igt_assert(file > 0);

		ret = getline(&line, &line_size, file);
		igt_assert(ret > 0);
		sscanf(line, "Workarounds applied: %d", &num_wa_regs);
		igt_assert(num_wa_regs > 0);

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

	igt_subtest("check-workaround-data-after-reset") {
		if (IS_BROADWELL(devid))
			check_workarounds(GPU_RESET, num_wa_regs);
		else
			igt_skip_on("No Workaround table available!!\n");
	}

	igt_subtest("check-workaround-data-after-suspend-resume") {
		if (IS_BROADWELL(devid))
			check_workarounds(SUSPEND_RESUME, num_wa_regs);
		else
			igt_skip_on("No Workaround table available!!\n");
	}

	igt_fixture {
		free(wa_regs);
		close(drm_fd);
	}
}
