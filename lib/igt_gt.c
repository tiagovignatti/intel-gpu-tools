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
#include <errno.h>

#include "igt_core.h"
#include "igt_gt.h"
#include "igt_debugfs.h"
#include "ioctl_wrappers.h"
#include "intel_reg.h"
#include "intel_chipset.h"

void igt_require_hang_ring(int fd, int ring)
{
	gem_context_require_param(fd, LOCAL_CONTEXT_PARAM_BAN_PERIOD);
	igt_require(intel_gen(intel_get_drm_devid(fd)) >= 5);
}

struct igt_hang_ring igt_hang_ring(int fd, int gen, int ring)
{
	struct drm_i915_gem_relocation_entry reloc;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	struct local_i915_gem_context_param param;
	uint32_t b[8];
	unsigned ban;
	unsigned len;

	param.context = 0;
	param.size = 0;
	param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;
	param.value = 0;
	gem_context_get_param(fd, &param);
	ban = param.value;

	param.value = 0;
	igt_require(gem_context_set_param(fd, &param) == 0);

	memset(&reloc, 0, sizeof(reloc));
	memset(&exec, 0, sizeof(exec));
	memset(&execbuf, 0, sizeof(execbuf));

	exec.handle = gem_create(fd, 4096);
	exec.relocation_count = 1;
	exec.relocs_ptr = (uintptr_t)&reloc;

	len = 2;
	if (gen >= 8)
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
	gem_execbuf(fd, &execbuf);

	return (struct igt_hang_ring){ exec.handle, ban };
}

void igt_post_hang_ring(int fd, struct igt_hang_ring arg)
{
	struct local_i915_gem_context_param param;

	if (arg.handle == 0)
		return;

	gem_set_domain(fd, arg.handle,
		       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	gem_close(fd, arg.handle);

	param.context = 0;
	param.size = 0;
	param.param = LOCAL_CONTEXT_PARAM_BAN_PERIOD;
	param.value = arg.ban;
	gem_context_set_param(fd, &param);
}
