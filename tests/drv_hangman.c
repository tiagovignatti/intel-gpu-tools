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

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "intel_chipset.h"
#include "drmtest.h"
#include "igt_debugfs.h"
#include "ioctl_wrappers.h"

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
	char tmp[1024];

	igt_assert(read_sysfs(tmp, sizeof(tmp), "error") > 0);
}

static void test_debugfs_error_state_exists(void)
{
	int fd;

	igt_assert((fd = igt_debugfs_open("i915_error_state", O_RDONLY)) >= 0);

	close (fd);
}

static void test_debugfs_ring_stop_exists(void)
{
	int fd;

	igt_assert((fd = igt_debugfs_open("i915_ring_stop", O_RDONLY)) >= 0);

	close(fd);
}

static void read_dfs(const char *fname, char *d, int maxlen)
{
	int fd;
	int l;

	igt_assert((fd = igt_debugfs_open(fname, O_RDONLY)) >= 0);

	igt_assert((l = read(fd, d, maxlen-1)) > 0);
	igt_assert(l < maxlen);
	d[l] = 0;
	close(fd);

	igt_debug("dfs entry %s read '%s'\n", fname, d);
}

static void _assert_dfs_entry(const char *fname, const char *s, bool inverse)
{
	char tmp[1024];
	const int l = strlen(s) < sizeof(tmp) ?
		strlen(s) : sizeof(tmp);

	read_dfs(fname, tmp, l + 1);
	if (!inverse) {
		if (strncmp(tmp, s, l) != 0) {
			fprintf(stderr, "contents of %s: '%s' (expected '%s')\n",
			       fname, tmp, s);
			igt_fail(1);
		}
	} else {
		if (strncmp(tmp, s, l) == 0) {
			fprintf(stderr, "contents of %s: '%s' (expected not '%s'\n",
			       fname, tmp, s);
			igt_fail(1);
		}
	}
}

static void assert_dfs_entry(const char *fname, const char *s)
{
	_assert_dfs_entry(fname, s, false);
}

static void assert_dfs_entry_not(const char *fname, const char *s)
{
	_assert_dfs_entry(fname, s, true);
}

static void assert_error_state_clear(void)
{
	assert_dfs_entry("i915_error_state", "no error state collected");
}

static void assert_error_state_collected(void)
{
	assert_dfs_entry_not("i915_error_state", "no error state collected");
}

static int get_line_count(const char *s)
{
	int count = 0;

	while (*s) {
		if (*s == '\n')
			count++;
		s++;
	}

	return count;
}

static void check_other_clients(void)
{
	char tmp[1024];
	char *s;
	int dev, pid, uid, magic;

	read_dfs("clients", tmp, sizeof(tmp));
	if (get_line_count(tmp) <= 2)
		return;

	s = strstr(tmp, "y");
	igt_assert(s != NULL);
	igt_assert(sscanf(s, "y %d %d %d %d",
			  &dev, &pid, &uid, &magic) == 4);

	igt_debug("client %d %d %d %d\n", dev, pid, uid, magic);
	igt_assert(pid == getpid());
	igt_debug("found myself in client list\n");
}

#define MAGIC_NUMBER 0x10001
const uint32_t batch[] = { MI_NOOP,
			   MI_BATCH_BUFFER_END,
			   MAGIC_NUMBER,
			   MAGIC_NUMBER };

static uint64_t submit_batch(int fd, unsigned ring_id, bool stop_ring)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	uint64_t presumed_offset;

	gem_require_ring(fd, ring_id);

	exec.handle = gem_create(fd, 4096);
	gem_write(fd, exec.handle, 0, batch, sizeof(batch));
	exec.relocation_count = 0;
	exec.relocs_ptr = 0;
	exec.alignment = 0;
	exec.offset = 0;
	exec.flags = 0;
	exec.rsvd1 = 0;
	exec.rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)&exec;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = sizeof(batch);
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = ring_id;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	gem_execbuf(fd, &execbuf);
	gem_sync(fd, exec.handle);
	presumed_offset = exec.offset;

	if (stop_ring) {
		igt_set_stop_rings(igt_to_stop_ring_flag(ring_id));

		gem_execbuf(fd, &execbuf);
		gem_sync(fd, exec.handle);

		igt_assert(igt_get_stop_rings() == STOP_RING_NONE);
		igt_assert(presumed_offset == exec.offset);
	}

	gem_close(fd, exec.handle);

	return exec.offset;
}

static void clear_error_state(void)
{
	int fd;
	const char *b = "1";

	igt_assert((fd = igt_debugfs_open("i915_error_state", O_WRONLY)) >= 0);
	igt_assert(write(fd, b, 1) == 1);
	close(fd);
}

static void test_error_state_basic(void)
{
	int fd;

	check_other_clients();
	clear_error_state();
	assert_error_state_clear();

	fd = drm_open_any();
	submit_batch(fd, I915_EXEC_RENDER, true);
	close(fd);

	assert_error_state_collected();
	clear_error_state();
	assert_error_state_clear();
}

static void check_error_state(const int gen,
			      const char *expected_ring_name,
			      uint64_t expected_offset)
{
	FILE *file;
	int debug_fd;
	char *line = NULL;
	size_t line_size = 0;
	char *ring_name = NULL;
	bool bb_ok = false, req_ok = false, ringbuf_ok = false;

	debug_fd = igt_debugfs_open("i915_error_state", O_RDONLY);
	igt_assert(debug_fd >= 0);
	file = fdopen(debug_fd, "r");

	while (getline(&line, &line_size, file) > 0) {
		char *dashes = NULL;
		int bb_matched = 0;
		uint32_t gtt_offset;
		int req_matched = 0;
		int requests;
		uint32_t tail;
		int ringbuf_matched = 0;
		int i, items;

		dashes = strstr(line, "---");
		if (!dashes)
			continue;

		ring_name = realloc(ring_name, dashes - line);
		strncpy(ring_name, line, dashes - line);
		ring_name[dashes - line - 1] = '\0';

		bb_matched = sscanf(dashes, "--- gtt_offset = 0x%08x\n",
				    &gtt_offset);
		if (bb_matched == 1) {
			char expected_line[32];

			igt_assert(strstr(ring_name, expected_ring_name));
			igt_assert(gtt_offset == expected_offset);

			for (i = 0; i < sizeof(batch) / 4; i++) {
				igt_assert(getline(&line, &line_size, file) > 0);
				snprintf(expected_line, sizeof(expected_line), "%08x :  %08x",
					 4*i, batch[i]);
				igt_assert(strstr(line, expected_line));
			}
			bb_ok = true;
			continue;
		}

		req_matched = sscanf(dashes, "--- %d requests\n", &requests);
		if (req_matched == 1) {
			igt_assert(strstr(ring_name, expected_ring_name));
			igt_assert(requests > 0);

			for (i = 0; i < requests; i++) {
				uint32_t seqno;
				long jiffies;

				igt_assert(getline(&line, &line_size, file) > 0);
				items = sscanf(line, "  seqno 0x%08x, emitted %ld, tail 0x%08x\n",
					       &seqno, &jiffies, &tail);
				igt_assert(items == 3);
			}
			req_ok = true;
			continue;
		}

		ringbuf_matched = sscanf(dashes, "--- ringbuffer = 0x%08x\n",
					 &gtt_offset);
		if (ringbuf_matched == 1) {
			unsigned int offset, command, expected_addr = 0;

			if (!strstr(ring_name, expected_ring_name))
				continue;
			igt_assert(req_ok);

			for (i = 0; i < tail / 4; i++) {
				igt_assert(getline(&line, &line_size, file) > 0);
				items = sscanf(line, "%08x :  %08x\n",
					       &offset, &command);
				igt_assert(items == 2);
				if ((command & 0x1F800000) == MI_BATCH_BUFFER_START) {
					igt_assert(getline(&line, &line_size, file) > 0);
					items = sscanf(line, "%08x :  %08x\n",
						       &offset, &expected_addr);
					igt_assert(items == 2);
					i++;
				}
			}
			if (gen >= 4)
				igt_assert(expected_addr == expected_offset);
			else
				igt_assert((expected_addr & ~0x1) == expected_offset);
			ringbuf_ok = true;
			continue;
		}

		if (bb_ok && req_ok && ringbuf_ok)
			break;
	}
	igt_assert(bb_ok && req_ok && ringbuf_ok);

	free(line);
	free(ring_name);
	close(debug_fd);
}

static void test_error_state_capture(unsigned ring_id,
				     const char *ring_name)
{
	int fd, gen;
	uint64_t offset;

	check_other_clients();
	clear_error_state();

	fd = drm_open_any();
	gen = intel_gen(intel_get_drm_devid(fd));

	offset = submit_batch(fd, ring_id, true);
	close(fd);

	check_error_state(gen, ring_name, offset);
}

static const struct target_ring {
	const int id;
	const char *short_name;
	const char *full_name;
} rings[] = {
	{ I915_EXEC_RENDER, "render", "render ring" },
	{ I915_EXEC_BSD, "bsd", "bsd ring" },
	{ I915_EXEC_BLT, "blt", "blitter ring" },
	{ I915_EXEC_VEBOX, "vebox", "video enhancement ring" },
};

igt_main
{
	igt_skip_on_simulation();

	igt_subtest("error-state-debugfs-entry")
		test_debugfs_error_state_exists();

	igt_subtest("error-state-sysfs-entry")
		test_sysfs_error_exists();

	igt_subtest("ring-stop-sysfs-entry")
		test_debugfs_ring_stop_exists();

	igt_subtest("error-state-basic")
		test_error_state_basic();

	for (int i = 0; i < sizeof(rings)/sizeof(rings[0]); i++) {
		igt_subtest_f("error-state-capture-%s", rings[i].short_name)
			test_error_state_capture(rings[i].id, rings[i].full_name);
	}
}
