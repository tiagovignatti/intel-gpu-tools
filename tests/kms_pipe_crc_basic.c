/*
 * Copyright © 2013 Intel Corporation
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
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_aux.h"
#include "ioctl_wrappers.h"

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb;
} data_t;

static uint64_t submit_batch(int fd, unsigned ring_id)
{
	const uint32_t batch[] = { MI_NOOP,
				   MI_BATCH_BUFFER_END };
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

	igt_set_stop_rings(igt_to_stop_ring_flag(ring_id));

	gem_execbuf(fd, &execbuf);
	gem_sync(fd, exec.handle);

	igt_assert(igt_get_stop_rings() == STOP_RING_NONE);
	igt_assert(presumed_offset == exec.offset);

	gem_close(fd, exec.handle);

	return exec.offset;
}

static void test_bad_command(data_t *data, const char *cmd)
{
	FILE *ctl;
	size_t written;

	ctl = igt_debugfs_fopen("i915_display_crc_ctl", "r+");
	written = fwrite(cmd, 1, strlen(cmd), ctl);
	fflush(ctl);
	igt_assert_cmpint(written, ==, (strlen(cmd)));
	igt_assert(ferror(ctl));
	igt_assert_cmpint(errno, ==, EINVAL);

	fclose(ctl);
}

#define TEST_SEQUENCE (1<<0)

static void test_read_crc(data_t *data, int pipe, unsigned flags)
{
	igt_display_t *display = &data->display;
	igt_pipe_crc_t *pipe_crc;
	igt_crc_t *crcs = NULL;
	int valid_connectors = 0;
	igt_output_t *output;

	igt_skip_on(pipe >= data->display.n_pipes);

	for_each_connected_output(display, output) {
		igt_plane_t *primary;
		drmModeModeInfo *mode;

		igt_output_set_pipe(output, pipe);

		igt_info("%s: Testing connector %s using pipe %c\n",
			 igt_subtest_name(), igt_output_name(output),
			 pipe_name(pipe));

		mode = igt_output_get_mode(output);
		igt_create_color_fb(data->drm_fd,
					mode->hdisplay, mode->vdisplay,
					DRM_FORMAT_XRGB8888,
					false, /* tiled */
					0.0, 1.0, 0.0,
					&data->fb);

		primary = igt_output_get_plane(output, 0);
		igt_plane_set_fb(primary, &data->fb);

		igt_display_commit(display);

		pipe_crc = igt_pipe_crc_new(pipe, INTEL_PIPE_CRC_SOURCE_AUTO);

		if (!pipe_crc)
			continue;
		valid_connectors++;

		igt_pipe_crc_start(pipe_crc);

		/* wait for 3 vblanks and the corresponding 3 CRCs */
		igt_pipe_crc_get_crcs(pipe_crc, 3, &crcs);

		igt_pipe_crc_stop(pipe_crc);

		/* ensure the CRCs are not all 0s */
		igt_assert(!igt_crc_is_null(&crcs[0]));
		igt_assert(!igt_crc_is_null(&crcs[1]));
		igt_assert(!igt_crc_is_null(&crcs[2]));

		/* and ensure that they'are all equal, we haven't changed the fb */
		igt_assert(igt_crc_equal(&crcs[0], &crcs[1]));
		igt_assert(igt_crc_equal(&crcs[1], &crcs[2]));

		if (flags & TEST_SEQUENCE) {
			igt_assert(crcs[0].frame + 1 == crcs[1].frame);
			igt_assert(crcs[1].frame + 1 == crcs[2].frame);
		}

		free(crcs);
		igt_pipe_crc_free(pipe_crc);
		igt_remove_fb(data->drm_fd, &data->fb);
		igt_plane_set_fb(primary, NULL);

		igt_output_set_pipe(output, PIPE_ANY);
	}

	igt_require_f(valid_connectors, "No connector found for pipe %i\n", pipe);

}

data_t data = {0, };

igt_main
{
	igt_skip_on_simulation();

	igt_fixture {
		data.drm_fd = drm_open_any();

		igt_set_vt_graphics_mode();

		igt_require_pipe_crc();

		igt_display_init(&data.display, data.drm_fd);
	}

	igt_subtest("bad-pipe")
		test_bad_command(&data, "pipe D none");

	igt_subtest("bad-source")
		test_bad_command(&data, "pipe A foo");

	igt_subtest("bad-nb-words-1")
		test_bad_command(&data, "pipe foo");

	igt_subtest("bad-nb-words-3")
		test_bad_command(&data, "pipe A none option");

	for (int i = 0; i < 3; i++) {
		igt_subtest_f("read-crc-pipe-%c", 'A'+i)
			test_read_crc(&data, i, 0);

		igt_subtest_f("read-crc-pipe-%c-frame-sequence", 'A'+i)
			test_read_crc(&data, i, TEST_SEQUENCE);

		igt_subtest_f("suspend-read-crc-pipe-%c", 'A'+i) {
			igt_system_suspend_autoresume();

			test_read_crc(&data, i, 0);
		}

		igt_subtest_f("hang-read-crc-pipe-%c", 'A'+i) {
			submit_batch(data.drm_fd, I915_EXEC_RENDER);

			test_read_crc(&data, i, 0);
		}
	}

	igt_fixture {
		igt_display_fini(&data.display);
	}
}
