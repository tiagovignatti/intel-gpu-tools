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
 */

#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "drmtest.h"
#include "igt_aux.h"
#include "igt_core.h"
#include "igt_gt.h"
#include "igt_debugfs.h"
#include "ioctl_wrappers.h"
#include "intel_reg.h"
#include "intel_chipset.h"

#if NEW_CONTEXT_PARAM_NO_ERROR_CAPTURE_API
#define LOCAL_CONTEXT_PARAM_NO_ERROR_CAPTURE 0x4
#endif

/**
 * SECTION:igt_gt
 * @short_description: GT support library
 * @title: GT
 * @include: igt.h
 *
 * This library provides various auxiliary helper functions to handle general
 * interactions with the GT like forcewake handling, injecting hangs or stopping
 * engines.
 */

static bool has_gpu_reset(int fd)
{
	static int once = -1;
	if (once < 0) {
		struct drm_i915_getparam gp;
		int val = 0;

		memset(&gp, 0, sizeof(gp));
		gp.param = 35; /* HAS_GPU_RESET */
		gp.value = &val;

		if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
			once = intel_gen(intel_get_drm_devid(fd)) >= 5;
		else
			once = val > 0;
	}
	return once;
}

/**
 * igt_require_hang_ring:
 * @fd: open i915 drm file descriptor
 * @ring: execbuf ring flag
 *
 * Convenience helper to check whether advanced hang injection is supported by
 * the kernel. Uses igt_skip to automatically skip the test/subtest if this
 * isn't the case.
 *
 * Note that we can't simply just call this from igt_hang_ring since some
 * tests want to exercise gpu wedging behavior. For which we intentionally
 * disable gpu reset support, but still want to inject a hang, see for example
 * tests/gem_eio.c Instead, we expect that the first invocation of
 * igt_require_hand_ring be from a vanilla context and use the has_gpu_reset()
 * determined then for all later instances. This allows us the convenience
 * of double checking when injecting hangs, whilst pushing the complexity
 * to the tests that are deliberating trying to break the box.
 *
 * This function is also controlled by the environment variables:
 *
 * IGT_HANG (boolean) - if false, skip all tests that try to inject a hang.
 * Default: true
 *
 * IGT_HANG_WITHOUT_RESET (boolean) - if true, allow the hang even if the
 * kernel does not support GPU recovery. The machine will be wedged afterwards
 * (and so require a reboot between testing), but it does allow limited testing
 * to be done under hang injection.
 * Default: false
 */
void igt_require_hang_ring(int fd, int ring)
{
	if (!igt_check_boolean_env_var("IGT_HANG", true))
		igt_skip("hang injection disabled by user");

	gem_require_ring(fd, ring);
	gem_context_require_ban_period(fd);
	if (!igt_check_boolean_env_var("IGT_HANG_WITHOUT_RESET", false))
		igt_require(has_gpu_reset(fd));
}

/**
 * igt_hang_ring_ctx:
 * @fd: open i915 drm file descriptor
 * @ctx: the contxt specifier
 * @ring: execbuf ring flag
 * @flags: set of flags to control execution
 *
 * This helper function injects a hanging batch associated with @ctx into @ring.
 * It returns a #igt_hang_ring_t structure which must be passed to
 * igt_post_hang_ring() for hang post-processing (after the gpu hang
 * interaction has been tested.
 *
 * Returns:
 * Structure with helper internal state for igt_post_hang_ring().
 */
igt_hang_ring_t igt_hang_ctx(int fd,
			     uint32_t ctx,
			     int ring,
			     unsigned flags,
			     uint64_t *offset)
{
	struct drm_i915_gem_relocation_entry reloc;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	struct local_i915_gem_context_param param;
	uint32_t b[8];
	unsigned ban;
	unsigned len;

	igt_require_hang_ring(fd, ring);

	/* One day the kernel ABI will be fixed! */
	igt_require(ctx == 0 || ring == I915_EXEC_RENDER);

	param.context = ctx;
	param.size = 0;

	if ((flags & HANG_ALLOW_CAPTURE) == 0) {
#if NEW_CONTEXT_PARAM_NO_ERROR_CAPTURE_API
		param.param = LOCAL_CONTEXT_PARAM_NO_ERROR_CAPTURE;
		param.value = 1;
		/* Older kernels may not have NO_ERROR_CAPTURE, in which case
		 * we just eat the error state in post-hang (and hope we eat
		 * the right one).
		 */
		__gem_context_set_param(fd, &param);
#endif
	}

	param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;
	param.value = 0;
	gem_context_get_param(fd, &param);
	ban = param.value;

	if ((flags & HANG_ALLOW_BAN) == 0) {
		param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;
		param.value = 0;
		gem_context_set_param(fd, &param);
	}

	memset(&reloc, 0, sizeof(reloc));
	memset(&exec, 0, sizeof(exec));
	memset(&execbuf, 0, sizeof(execbuf));

	exec.handle = gem_create(fd, 4096);
	exec.relocation_count = 1;
	exec.relocs_ptr = (uintptr_t)&reloc;

	memset(b, 0xc5, sizeof(b));
	len = 2;
	if (intel_gen(intel_get_drm_devid(fd)) >= 8)
		len++;
	b[0] = MI_BATCH_BUFFER_START | (len - 2);
	b[len] = MI_BATCH_BUFFER_END;
	b[len+1] = MI_NOOP;
	gem_write(fd, exec.handle, 0, b, sizeof(b));

	reloc.offset = 4;
	reloc.target_handle = exec.handle;
	reloc.read_domains = I915_GEM_DOMAIN_COMMAND;

	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.batch_len = sizeof(b);
	execbuf.flags = ring;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	gem_execbuf(fd, &execbuf);

	if (offset)
		*offset = exec.offset;

	return (struct igt_hang_ring){ exec.handle, ctx, ban, flags };
}

/**
 * igt_hang_ring:
 * @fd: open i915 drm file descriptor
 * @ring: execbuf ring flag
 *
 * This helper function injects a hanging batch into @ring. It returns a
 * #igt_hang_ring_t structure which must be passed to igt_post_hang_ring() for
 * hang post-processing (after the gpu hang interaction has been tested.
 *
 * Returns:
 * Structure with helper internal state for igt_post_hang_ring().
 */
igt_hang_ring_t igt_hang_ring(int fd, int ring)
{
	return igt_hang_ctx(fd, 0, ring, 0, NULL);
}

static void eat_error_state(void)
{
	int fd, ret;

	fd = igt_debugfs_open("i915_error_state", O_WRONLY);
	do {
		ret = write(fd, "", 1);
		if (ret < 0)
			ret = -errno;
	} while (ret == -EINTR || ret == -EAGAIN);
	close(fd);
}

/**
 * igt_post_hang_ring:
 * @fd: open i915 drm file descriptor
 * @arg: hang state from igt_hang_ring()
 *
 * This function does the necessary post-processing after a gpu hang injected
 * with igt_hang_ring().
 */
void igt_post_hang_ring(int fd, struct igt_hang_ring arg)
{
	struct local_i915_gem_context_param param;

	if (arg.handle == 0)
		return;

	gem_set_domain(fd, arg.handle,
		       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	gem_close(fd, arg.handle);

	param.context = arg.ctx;
	param.size = 0;
	param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;
	param.value = arg.ban;
	gem_context_set_param(fd, &param);

	if ((arg.flags & HANG_ALLOW_CAPTURE) == 0) {
#if NEW_CONTEXT_PARAM_NO_ERROR_CAPTURE_API
		param.param = LOCAL_CONTEXT_PARAM_NO_ERROR_CAPTURE;
		param.value = 0;
		if (__gem_context_set_param(fd, &param))
			eat_error_state();
#else
		eat_error_state();
#endif
	}
}

/**
 * igt_force_gpu_reset:
 *
 * forces a gpu reset using the i915_wedged debugfs interface. To be used to
 * recover from situations where the hangcheck didn't trigger and/or the gpu is
 * stuck, either because the test manually disabled gpu resets or because the
 * test hit an hangcheck bug
 */
void igt_force_gpu_reset(void)
{
	int fd, ret;

	igt_debug("Triggering GPU reset\n");

	fd = igt_debugfs_open("i915_wedged", O_RDWR);
	igt_require(fd >= 0);

	ret = write(fd, "-1\n", 3);
	close(fd);

	igt_assert_eq(ret, 3);
}

/* GPU abusers */
static struct igt_helper_process hang_helper;
static void __attribute__((noreturn))
hang_helper_process(pid_t pid, int fd)
{
	while (1) {
		if (kill(pid, 0)) /* Parent has died, so must we. */
			exit(0);

		igt_post_hang_ring(fd,
				   igt_hang_ring(fd, I915_EXEC_DEFAULT));

		sleep(1);
	}
}

/**
 * igt_fork_hang_helper:
 *
 * Fork a child process using #igt_fork_helper to hang the default engine
 * of the GPU at regular intervals.
 *
 * This is useful to exercise slow running code (such as aperture placement)
 * which needs to be robust against a GPU reset.
 *
 * This function automatically skips when test requirements aren't met using
 * igt_skip().
 */
void igt_fork_hang_helper(void)
{
	int fd, gen;

	fd = drm_open_driver(DRIVER_INTEL);

	gen = intel_gen(intel_get_drm_devid(fd));
	igt_skip_on(gen < 5);

	igt_fork_helper(&hang_helper)
		hang_helper_process(getppid(), fd);

	close(fd);
}

/**
 * igt_stop_hang_helper:
 *
 * Stops the child process spawned with igt_fork_hang_helper().
 *
 * In tests with subtests this function can be called outside of failure
 * catching code blocks like #igt_fixture or #igt_subtest.
 */
void igt_stop_hang_helper(void)
{
	if (hang_helper.running)
		igt_stop_helper(&hang_helper);
}

/**
 * igt_open_forcewake_handle:
 *
 * This functions opens the debugfs forcewake file and so prevents the GT from
 * suspending. The reference is automatically dropped when the is closed.
 *
 * Returns:
 * The file descriptor of the forcewake handle or -1 if that didn't work out.
 */
int igt_open_forcewake_handle(void)
{
	if (getenv("IGT_NO_FORCEWAKE"))
		return -1;
	return igt_debugfs_open("i915_forcewake_user", O_WRONLY);
}
static unsigned int clflush_size;

#if defined(__x86_64__) || defined(__i386__)
int igt_setup_clflush(void)
{
	FILE *file;
	char *line = NULL;
	size_t size = 0;
	int first_stanza = 1;
	int has_clflush = 0;

	if (clflush_size)
		return 1;

	file = fopen("/proc/cpuinfo", "r");
	if (file == NULL)
		return 0;

	while (getline(&line, &size, file) != -1) {
		if (strncmp(line, "processor", 9) == 0) {
			if (!first_stanza)
				break;
			first_stanza = 0;
		}

		if (strncmp(line, "flags", 5) == 0) {
			if (strstr(line, "clflush"))
				has_clflush = 1;
		}

		if (strncmp(line, "clflush size", 12) == 0) {
			char *colon = strchr(line, ':');
			if (colon)
				clflush_size = atoi(colon + 1);
		}
	}
	free(line);
	fclose(file);

	return has_clflush && clflush_size;
}

__attribute__((target("sse2")))
void igt_clflush_range(void *addr, int size)
{
	char *p, *end;

	end = (char *)addr + size;
	p = (char *)((uintptr_t)addr & ~((uintptr_t)clflush_size - 1));

	__builtin_ia32_mfence();
	for (; p < end; p += clflush_size)
		__builtin_ia32_clflush(p);
	__builtin_ia32_clflush(end - 1); /* magic serialisation for byt+ */
	__builtin_ia32_mfence();
}
#else
int igt_setup_clflush(void)
{
	/* requires mfence + clflush, both SSE2 instructions */
	return 0;
}

void igt_clflush_range(void *addr, int size)
{
	fprintf(stderr, "igt_clflush_range() unsupported\n");
}
#endif

/**
 * intel_detect_and_clear_missed_irq:
 * @fd: open i915 drm file descriptor, used to quiesce the gpu
 *
 * This functions idles the GPU and then queries whether there has
 * been a missed interrupt reported by the driver. Afterwards it
 * clears the missed interrupt flag, in order to disable the timer
 * fallback for the next test.
 */
unsigned intel_detect_and_clear_missed_interrupts(int fd)
{
	unsigned missed = 0;
	FILE *file;

	gem_quiescent_gpu(fd);

	file = igt_debugfs_fopen("i915_ring_missed_irq", "r");
	if (file) {
		igt_assert(fscanf(file, "%x", &missed) == 1);
		fclose(file);
	}
	if (missed) {
		file = igt_debugfs_fopen("i915_ring_missed_irq", "w");
		if (file) {
			fwrite("0\n", 1, 2, file);
			fclose(file);
		}
	}

	return missed;
}

const struct intel_execution_engine intel_execution_engines[] = {
	{ "default", NULL, 0, 0 },
	{ "render", "render ring", I915_EXEC_RENDER, 0 },
	{ "bsd", "bsd ring", I915_EXEC_BSD, 0 },
	{ "bsd1", "bsd ring", I915_EXEC_BSD, 1<<13 /*I915_EXEC_BSD_RING1*/ },
	{ "bsd2", "bsd2 ring", I915_EXEC_BSD, 2<<13 /*I915_EXEC_BSD_RING2*/ },
	{ "blt", "blitter ring", I915_EXEC_BLT, 0 },
	{ "vebox", "video enhancement ring", I915_EXEC_VEBOX, 0 },
	{ NULL, 0, 0 }
};
