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
 *    Mika Kuoppala <mika.kuoppala@intel.com>
 *    Oscar Mateo <oscar.mateo@intel.com>
 *
 */

#include "igt.h"
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef I915_PARAM_CMD_PARSER_VERSION
#define I915_PARAM_CMD_PARSER_VERSION       28
#endif

static char read_buffer[1024];

static int _read_sysfs(void *dst, int maxlen,
		      const char* path,
		      const char *fname)
{
	int fd;
	char full[PATH_MAX];
	int r, e;

	igt_assert(snprintf(full, PATH_MAX, "%s/%s", path, fname) < PATH_MAX);

	fd = open(full, O_RDONLY);
	if (fd == -1)
		return -errno;

	r = read(fd, dst, maxlen);
	e = errno;
	close(fd);

	if (r < 0)
		return -e;

	return r;
}

static int read_sysfs(void *dst, int maxlen, const char *fname)
{
	char path[PATH_MAX];

	igt_assert(snprintf(path, PATH_MAX, "/sys/class/drm/card%d",
			    drm_get_card()) < PATH_MAX);

	return _read_sysfs(dst, maxlen, path, fname);
}

static void test_sysfs_error_exists(void)
{
	igt_assert_lt(0, read_sysfs(read_buffer, sizeof(read_buffer), "error"));
}

static void test_debugfs_error_state_exists(void)
{
	int fd;

	igt_assert_lte(0,
		       (fd = igt_debugfs_open("i915_error_state", O_RDONLY)));

	close (fd);
}

static void read_dfs(const char *fname, char *d, int maxlen)
{
	int fd;
	int l;

	igt_assert_lte(0, (fd = igt_debugfs_open(fname, O_RDONLY)));

	igt_assert_lt(0, (l = read(fd, d, maxlen - 1)));
	igt_assert_lt(l, maxlen);
	d[l] = 0;
	close(fd);

	igt_debug("dfs entry %s read '%s'\n", fname, d);
}

static int compare_dfs_entry(const char *fname, const char *s)
{
	const int l = min(strlen(s), sizeof(read_buffer));

	read_dfs(fname, read_buffer, l + 1);
	return strncmp(read_buffer, s, l);
}

static void assert_dfs_entry(const char *fname, const char *s)
{
	igt_fail_on_f(compare_dfs_entry(fname, s) != 0,
		      "contents of %s: '%s' (expected '%s')\n",
		      fname, read_buffer, s);
}

static void assert_dfs_entry_not(const char *fname, const char *s)
{
	igt_fail_on_f(compare_dfs_entry(fname, s) == 0,
		      "contents of %s: '%s' (expected not '%s'\n",
		      fname, read_buffer, s);
}

static void assert_error_state_clear(void)
{
	assert_dfs_entry("i915_error_state", "no error state collected");
}

static void assert_error_state_collected(void)
{
	assert_dfs_entry_not("i915_error_state", "no error state collected");
}

const uint32_t *batch;

static uint64_t submit_hang(int fd, unsigned ring_id)
{
	uint64_t offset;
	igt_hang_ring_t hang;

	hang = igt_hang_ctx(fd, 0, ring_id, HANG_ALLOW_CAPTURE, &offset);

	batch = gem_mmap__cpu(fd, hang.handle, 0, 4096, PROT_READ);
	gem_set_domain(fd, hang.handle, I915_GEM_DOMAIN_CPU, 0);

	igt_post_hang_ring(fd, hang);

	return offset;
}

static void clear_error_state(void)
{
	int fd;
	const char *b = "1";

	igt_assert_lte(0,
		       (fd = igt_debugfs_open("i915_error_state", O_WRONLY)));
	igt_assert(write(fd, b, 1) == 1);
	close(fd);
}

static void test_error_state_basic(void)
{
	int fd;

	fd = drm_open_driver(DRIVER_INTEL);

	clear_error_state();
	assert_error_state_clear();

	submit_hang(fd, I915_EXEC_RENDER);
	close(fd);

	assert_error_state_collected();
	clear_error_state();
	assert_error_state_clear();
}

static void check_error_state(const int gen,
			      const bool uses_cmd_parser,
			      const char *expected_ring_name,
			      uint64_t expected_offset)
{
	FILE *file;
	char *line = NULL;
	size_t line_size = 0;

	file = igt_debugfs_fopen("i915_error_state", "r");
	igt_require(file);

	while (getline(&line, &line_size, file) > 0) {
		char *dashes;
		uint32_t gtt_offset_upper, gtt_offset_lower;
		int matched;

		dashes = strstr(line, "---");
		if (!dashes)
			continue;

		matched = sscanf(dashes, "--- gtt_offset = 0x%08x %08x\n",
				    &gtt_offset_upper, &gtt_offset_lower);
		if (matched) {
			char expected_line[64];
			uint64_t gtt_offset;
			int i;

			strncpy(expected_line, line, dashes - line);
			expected_line[dashes - line - 1] = '\0';
			igt_assert(strstr(expected_line, expected_ring_name));

			gtt_offset = gtt_offset_upper;
			if (matched == 2) {
				gtt_offset <<= 32;
				gtt_offset |= gtt_offset_lower;
			}
			if (!uses_cmd_parser)
				igt_assert_eq_u64(gtt_offset, expected_offset);

			for (i = 0; i < 1024; i++) {
				igt_assert(getline(&line, &line_size, file) > 0);
				snprintf(expected_line, sizeof(expected_line),
					 "%08x :  %08x",
					 4*i, batch[i]);
				igt_assert(strstr(line, expected_line));
			}
			break;
		}
	}

	free(line);
	fclose(file);
}

static bool uses_cmd_parser(int fd, int gen)
{
	int parser_version = 0;
	drm_i915_getparam_t gp;
	int rc;

	gp.param = I915_PARAM_CMD_PARSER_VERSION;
	gp.value = &parser_version;
	rc = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	if (rc || parser_version == 0)
		return false;

	if (!gem_uses_ppgtt(fd))
		return false;

	if (gen != 7)
		return false;

	return true;
}

static void test_error_state_capture(unsigned ring_id,
				     const char *ring_name)
{
	int fd, gen;
	uint64_t offset;
	bool cmd_parser;

	fd = drm_open_driver(DRIVER_INTEL);

	clear_error_state();

	gen = intel_gen(intel_get_drm_devid(fd));
	cmd_parser = uses_cmd_parser(fd, gen);

	offset = submit_hang(fd, ring_id);
	close(fd);

	check_error_state(gen, cmd_parser, ring_name, offset);
}


/* This test covers the case where we end up in an uninitialised area of the
 * ppgtt and keep executing through it. This is particularly relevant if 48b
 * ppgtt is enabled because the ppgtt is massively bigger compared to the 32b
 * case and it takes a lot more time to wrap, so the acthd can potentially keep
 * increasing for a long time
 */
#define NSEC_PER_SEC	1000000000LL
static void hangcheck_unterminated(void)
{
	int fd;
	/* timeout needs to be greater than ~5*hangcheck */
	int64_t timeout_ns = 100 * NSEC_PER_SEC; /* 100 seconds */
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec;
	uint32_t handle;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(gem_uses_full_ppgtt(fd));
	igt_require_hang_ring(fd, 0);

	handle = gem_create(fd, 4096);

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&gem_exec;
	execbuf.buffer_count = 1;

	gem_execbuf(fd, &execbuf);
	if (gem_wait(fd, handle, &timeout_ns) != 0) {
		/* need to manually trigger an hang to clean before failing */
		igt_force_gpu_reset();
		igt_assert_f(0, "unterminated batch did not trigger an hang!");
	}

	close(fd);
}

igt_main
{
	const struct intel_execution_engine *e;

	igt_skip_on_simulation();

	igt_subtest("error-state-debugfs-entry")
		test_debugfs_error_state_exists();

	igt_subtest("error-state-sysfs-entry")
		test_sysfs_error_exists();

	igt_subtest("error-state-basic")
		test_error_state_basic();

	for (e = intel_execution_engines; e->name; e++) {
		if (e->exec_id == 0)
			continue;

		igt_subtest_f("error-state-capture-%s", e->name)
			test_error_state_capture(e->exec_id | e->flags,
						 e->full_name);
	}

	igt_subtest("hangcheck-unterminated")
		hangcheck_unterminated();
}
