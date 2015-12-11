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
#include "igt.h"

enum operation {
	GPU_RESET,
	SUSPEND_RESUME,
	SIMPLE_READ,
};

struct intel_wa_reg {
	uint32_t addr;
	uint32_t value;
	uint32_t mask;
};

static struct intel_wa_reg *wa_regs;
static int num_wa_regs;

static void wait_gpu(void)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	gem_quiescent_gpu(fd);
	close(fd);
}

static void test_hang_gpu(void)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	igt_post_hang_ring(fd, igt_hang_ring(fd, I915_EXEC_DEFAULT));
	close(fd);
}

static void test_suspend_resume(void)
{
	igt_info("Suspending the device ...\n");
	igt_system_suspend_autoresume();
}

static int workaround_fail_count(void)
{
	int i, fail_count = 0;

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

	return fail_count;
}

static void check_workarounds(enum operation op)
{
	igt_assert_eq(workaround_fail_count(), 0);

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

	igt_assert_eq(workaround_fail_count(), 0);
}

igt_main
{
	igt_fixture {
		struct pci_device *pci_dev;
		FILE *file;
		char *line = NULL;
		size_t line_size;
		int i;

		pci_dev = intel_get_pci_device();
		igt_require(pci_dev);

		intel_register_access_init(pci_dev, 0);

		file = igt_debugfs_fopen("i915_wa_registers", "r");
		igt_assert(getline(&line, &line_size, file) > 0);
		igt_debug("i915_wa_registers: %s", line);
		sscanf(line, "Workarounds applied: %d", &num_wa_regs);

		if (IS_BROADWELL(pci_dev->device_id) ||
		    IS_CHERRYVIEW(pci_dev->device_id))
			igt_assert(num_wa_regs > 0);
		else
			igt_assert(num_wa_regs >= 0);

		wa_regs = malloc(num_wa_regs * sizeof(*wa_regs));
		igt_assert(wa_regs);

		i = 0;
		while (getline(&line, &line_size, file) > 0) {
			igt_debug("%s", line);
			igt_assert(i < num_wa_regs);
			if (sscanf(line, "0x%X: 0x%08X, mask: 0x%08X",
				   &wa_regs[i].addr,
				   &wa_regs[i].value,
				   &wa_regs[i].mask) == 3)
				i++;
		}

		free(line);
		fclose(file);
	}

	igt_subtest("read")
		check_workarounds(SIMPLE_READ);

	igt_subtest("reset")
		check_workarounds(GPU_RESET);

	igt_subtest("suspend-resume")
		check_workarounds(SUSPEND_RESUME);

	igt_fixture {
		free(wa_regs);
		intel_register_access_fini();
	}

}
