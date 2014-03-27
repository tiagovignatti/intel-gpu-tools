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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <drm.h>

#include "drmtest.h"
#include "ioctl_wrappers.h"
#include "intel_chipset.h"

#ifndef I915_PARAM_CMD_PARSER_VERSION
#define I915_PARAM_CMD_PARSER_VERSION       28
#endif

static int exec_batch_patched(int fd, uint32_t cmd_bo, uint32_t *cmds,
			      int size, int patch_offset, uint64_t expected_value)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 objs[2];
	struct drm_i915_gem_relocation_entry reloc[1];

	uint32_t target_bo = gem_create(fd, 4096);
	uint64_t actual_value = 0;

	gem_write(fd, cmd_bo, 0, cmds, size);

	reloc[0].offset = patch_offset;
	reloc[0].delta = 0;
	reloc[0].target_handle = target_bo;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	reloc[0].presumed_offset = 0;

	objs[0].handle = target_bo;
	objs[0].relocation_count = 0;
	objs[0].relocs_ptr = 0;
	objs[0].alignment = 0;
	objs[0].offset = 0;
	objs[0].flags = 0;
	objs[0].rsvd1 = 0;
	objs[0].rsvd2 = 0;

	objs[1].handle = cmd_bo;
	objs[1].relocation_count = 1;
	objs[1].relocs_ptr = (uintptr_t)reloc;
	objs[1].alignment = 0;
	objs[1].offset = 0;
	objs[1].flags = 0;
	objs[1].rsvd1 = 0;
	objs[1].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)objs;
	execbuf.buffer_count = 2;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = size;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = I915_EXEC_RENDER;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	gem_execbuf(fd, &execbuf);
	gem_sync(fd, cmd_bo);

	gem_read(fd,target_bo, 0, &actual_value, sizeof(actual_value));
	igt_assert(expected_value == actual_value);

	gem_close(fd, target_bo);

	return 1;
}

static int exec_batch(int fd, uint32_t cmd_bo, uint32_t *cmds,
		      int size, int ring, int expected_ret)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 objs[1];
	int ret;

	gem_write(fd, cmd_bo, 0, cmds, size);

	objs[0].handle = cmd_bo;
	objs[0].relocation_count = 0;
	objs[0].relocs_ptr = 0;
	objs[0].alignment = 0;
	objs[0].offset = 0;
	objs[0].flags = 0;
	objs[0].rsvd1 = 0;
	objs[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)objs;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = size;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = ring;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	if (ret == 0)
		igt_assert(expected_ret == 0);
	else
		igt_assert(-errno == expected_ret);

	gem_sync(fd, cmd_bo);

	return 1;
}

static int exec_split_batch(int fd, uint32_t *cmds,
			    int size, int ring, int expected_ret)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 objs[1];
	uint32_t cmd_bo;
	uint32_t noop[1024] = { 0 };
	int ret;

	// Allocate and fill a 2-page batch with noops
	cmd_bo = gem_create(fd, 4096 * 2);
	gem_write(fd, cmd_bo, 0, noop, sizeof(noop));
	gem_write(fd, cmd_bo, 4096, noop, sizeof(noop));

	// Write the provided commands such that the first dword
	// of the command buffer is the last dword of the first
	// page (i.e. the command is split across the two pages).
	gem_write(fd, cmd_bo, 4096-sizeof(uint32_t), cmds, size);

	objs[0].handle = cmd_bo;
	objs[0].relocation_count = 0;
	objs[0].relocs_ptr = 0;
	objs[0].alignment = 0;
	objs[0].offset = 0;
	objs[0].flags = 0;
	objs[0].rsvd1 = 0;
	objs[0].rsvd2 = 0;

	execbuf.buffers_ptr = (uintptr_t)objs;
	execbuf.buffer_count = 1;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = size;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = ring;
	i915_execbuffer2_set_context_id(execbuf, 0);
	execbuf.rsvd2 = 0;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);
	if (ret == 0)
		igt_assert(expected_ret == 0);
	else
		igt_assert(-errno == expected_ret);

	gem_sync(fd, cmd_bo);
	gem_close(fd, cmd_bo);

	return 1;
}

uint32_t handle;
int fd;

#define MI_ARB_ON_OFF (0x8 << 23)
#define MI_DISPLAY_FLIP ((0x14 << 23) | 1)
#define MI_LOAD_REGISTER_IMM ((0x22 << 23) | 1)

#define GFX_OP_PIPE_CONTROL	((0x3<<29)|(0x3<<27)|(0x2<<24)|2)
#define   PIPE_CONTROL_QW_WRITE	(1<<14)
#define   PIPE_CONTROL_LRI_POST_OP (1<<23)

#define OACONTROL 0x2360

igt_main
{
	igt_fixture {
		int parser_version = 0;
                drm_i915_getparam_t gp;
		int rc;

		fd = drm_open_any();

		gp.param = I915_PARAM_CMD_PARSER_VERSION;
		gp.value = &parser_version;
		rc = drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
		igt_require(!rc && parser_version > 0);

		handle = gem_create(fd, 4096);

		/* ATM cmd parser only exists on gen7. */
		igt_require(intel_gen(intel_get_drm_devid(fd)) == 7);
	}

	igt_subtest("basic-allowed") {
		uint32_t pc[] = {
			GFX_OP_PIPE_CONTROL,
			PIPE_CONTROL_QW_WRITE,
			0, // To be patched
			0x12000000,
			0,
			MI_BATCH_BUFFER_END,
		};
		igt_assert(
			exec_batch_patched(fd, handle,
					   pc, sizeof(pc),
					   8, // patch offset,
					   0x12000000));
	}

	igt_subtest("basic-rejected") {
		uint32_t arb_on_off[] = {
			MI_ARB_ON_OFF,
			MI_BATCH_BUFFER_END,
		};
		uint32_t display_flip[] = {
			MI_DISPLAY_FLIP,
			0, 0, 0,
			MI_BATCH_BUFFER_END,
			0
		};
		igt_assert(
			   exec_batch(fd, handle,
				      arb_on_off, sizeof(arb_on_off),
				      I915_EXEC_RENDER,
				      -EINVAL));
		igt_assert(
			   exec_batch(fd, handle,
				      arb_on_off, sizeof(arb_on_off),
				      I915_EXEC_BSD,
				      -EINVAL));
		if (gem_has_vebox(fd)) {
			igt_assert(
				   exec_batch(fd, handle,
					      arb_on_off, sizeof(arb_on_off),
					      I915_EXEC_VEBOX,
					      -EINVAL));
		}
		igt_assert(
			   exec_batch(fd, handle,
				      display_flip, sizeof(display_flip),
				      I915_EXEC_BLT,
				      -EINVAL));
	}

	igt_subtest("registers") {
		uint32_t lri_bad[] = {
			MI_LOAD_REGISTER_IMM,
			0, // disallowed register address
			0x12000000,
			MI_BATCH_BUFFER_END,
		};
		uint32_t lri_ok[] = {
			MI_LOAD_REGISTER_IMM,
			0x5280, // allowed register address (SO_WRITE_OFFSET[0])
			0x1,
			MI_BATCH_BUFFER_END,
		};
		igt_assert(
			   exec_batch(fd, handle,
				      lri_bad, sizeof(lri_bad),
				      I915_EXEC_RENDER,
				      -EINVAL));
		igt_assert(
			   exec_batch(fd, handle,
				      lri_ok, sizeof(lri_ok),
				      I915_EXEC_RENDER,
				      0));
	}

	igt_subtest("bitmasks") {
		uint32_t pc[] = {
			GFX_OP_PIPE_CONTROL,
			(PIPE_CONTROL_QW_WRITE |
			 PIPE_CONTROL_LRI_POST_OP),
			0, // To be patched
			0x12000000,
			0,
			MI_BATCH_BUFFER_END,
		};
		igt_assert(
			   exec_batch(fd, handle,
				      pc, sizeof(pc),
				      I915_EXEC_RENDER,
				      -EINVAL));
	}

	igt_subtest("batch-without-end") {
		uint32_t noop[1024] = { 0 };
		igt_assert(
			   exec_batch(fd, handle,
				      noop, sizeof(noop),
				      I915_EXEC_RENDER,
				      -EINVAL));
	}

	igt_subtest("cmd-crossing-page") {
		uint32_t lri_ok[] = {
			MI_LOAD_REGISTER_IMM,
			0x5280, // allowed register address (SO_WRITE_OFFSET[0])
			0x1,
			MI_BATCH_BUFFER_END,
		};
		igt_assert(
			   exec_split_batch(fd,
					    lri_ok, sizeof(lri_ok),
					    I915_EXEC_RENDER,
					    0));
	}

	igt_subtest("oacontrol-tracking") {
		uint32_t lri_ok[] = {
			MI_LOAD_REGISTER_IMM,
			OACONTROL,
			0x31337000,
			MI_LOAD_REGISTER_IMM,
			OACONTROL,
			0x0,
			MI_BATCH_BUFFER_END,
			0
		};
		uint32_t lri_bad[] = {
			MI_LOAD_REGISTER_IMM,
			OACONTROL,
			0x31337000,
			MI_BATCH_BUFFER_END,
		};
		uint32_t lri_extra_bad[] = {
			MI_LOAD_REGISTER_IMM,
			OACONTROL,
			0x31337000,
			MI_LOAD_REGISTER_IMM,
			OACONTROL,
			0x0,
			MI_LOAD_REGISTER_IMM,
			OACONTROL,
			0x31337000,
			MI_BATCH_BUFFER_END,
		};
		igt_assert(
			exec_batch(fd, handle,
				      lri_ok, sizeof(lri_ok),
				      I915_EXEC_RENDER,
				      0));
		igt_assert(
			exec_batch(fd, handle,
				      lri_bad, sizeof(lri_bad),
				      I915_EXEC_RENDER,
				      -EINVAL));
		igt_assert(
			exec_batch(fd, handle,
				      lri_extra_bad, sizeof(lri_extra_bad),
				      I915_EXEC_RENDER,
				      -EINVAL));
	}

	igt_fixture {
		gem_close(fd, handle);

		close(fd);
	}
}
