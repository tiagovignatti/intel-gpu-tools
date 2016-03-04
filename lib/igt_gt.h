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

#ifndef IGT_GT_H
#define IGT_GT_H

#include "igt_debugfs.h"

void igt_require_hang_ring(int fd, int ring);

typedef struct igt_hang_ring {
	unsigned handle;
	unsigned ctx;
	unsigned ban;
	unsigned flags;
} igt_hang_ring_t;

#define HANG_POISON 0xc5c5c5c5

struct igt_hang_ring igt_hang_ctx(int fd,
				  uint32_t ctx,
				  int ring,
				  unsigned flags,
				  uint64_t *offset);
#define HANG_ALLOW_BAN 1
#define HANG_ALLOW_CAPTURE 2

struct igt_hang_ring igt_hang_ring(int fd, int ring);
void igt_post_hang_ring(int fd, struct igt_hang_ring arg);

void igt_force_gpu_reset(void);

void igt_fork_hang_helper(void);
void igt_stop_hang_helper(void);

int igt_open_forcewake_handle(void);

/**
 * stop_ring_flags:
 * @STOP_RING_NONE: Can be used to clear the pending stop (warning: hang might
 * be declared already). Returned by igt_get_stop_rings() if there is
 * no currently stopped rings.
 * @STOP_RING_RENDER: Render ring
 * @STOP_RING_BSD: Video encoding/decoding ring
 * @STOP_RING_BLT: Blitter ring
 * @STOP_RING_VEBOX: Video enhancement ring
 * @STOP_RING_ALL: All rings
 * @STOP_RING_ALLOW_ERRORS: Driver will not omit expected DRM_ERRORS
 * @STOP_RING_ALLOW_BAN: Driver will use context ban policy
 * @STOP_RING_DEFAULTS: STOP_RING_ALL | STOP_RING_ALLOW_ERRORS
 *
 * Enumeration of all supported flags for igt_set_stop_rings().
 *
 */
enum stop_ring_flags {
	STOP_RING_NONE = 0x00,
	STOP_RING_RENDER = (1 << 0),
	STOP_RING_BSD = (1 << 1),
	STOP_RING_BLT = (1 << 2),
	STOP_RING_VEBOX = (1 << 3),
	STOP_RING_ALL = 0xff,
	STOP_RING_ALLOW_ERRORS = (1 << 30),
	STOP_RING_ALLOW_BAN = (1 << 31),
	STOP_RING_DEFAULTS = STOP_RING_ALL | STOP_RING_ALLOW_ERRORS,
};

enum stop_ring_flags igt_to_stop_ring_flag(int ring);
void igt_set_stop_rings(enum stop_ring_flags flags);
enum stop_ring_flags igt_get_stop_rings(void);

int igt_setup_clflush(void);
void igt_clflush_range(void *addr, int size);

unsigned intel_detect_and_clear_missed_interrupts(int fd);

extern const struct intel_execution_engine {
	const char *name;
	const char *full_name;
	unsigned exec_id;
	unsigned flags;
} intel_execution_engines[];

#define for_if(expr__) if (!(expr__)) {} else

#define for_each_engine(fd__, flags__) \
	for (const struct intel_execution_engine *e__ = intel_execution_engines;\
	     e__->name; \
	     e__++) \
		for_if (gem_has_ring(fd, flags__ = e__->exec_id | e__->flags))


#endif /* IGT_GT_H */
