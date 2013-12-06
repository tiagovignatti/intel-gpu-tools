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

#ifndef __IGT_DEBUGFS_H__
#define __IGT_DEBUGFS_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "igt_display.h"

typedef struct {
	char root[128];
	char dri_path[128];
} igt_debugfs_t;

int igt_debugfs_init(igt_debugfs_t *debugfs);
int igt_debugfs_open(igt_debugfs_t *debugfs, const char *filename, int mode);
FILE *igt_debugfs_fopen(igt_debugfs_t *debugfs, const char *filename,
			const char *mode);

/*
 * Pipe CRC
 */

enum intel_pipe_crc_source {
        INTEL_PIPE_CRC_SOURCE_NONE,
        INTEL_PIPE_CRC_SOURCE_PLANE1,
        INTEL_PIPE_CRC_SOURCE_PLANE2,
        INTEL_PIPE_CRC_SOURCE_PF,
        INTEL_PIPE_CRC_SOURCE_PIPE,
        INTEL_PIPE_CRC_SOURCE_TV,
        INTEL_PIPE_CRC_SOURCE_DP_B,
        INTEL_PIPE_CRC_SOURCE_DP_C,
        INTEL_PIPE_CRC_SOURCE_DP_D,
        INTEL_PIPE_CRC_SOURCE_AUTO,
        INTEL_PIPE_CRC_SOURCE_MAX,
};

typedef struct _igt_pipe_crc igt_pipe_crc_t;
typedef struct {
	uint32_t frame;
	int n_words;
	uint32_t crc[5];
} igt_crc_t;

bool igt_crc_is_null(igt_crc_t *crc);
bool igt_crc_equal(igt_crc_t *a, igt_crc_t *b);
char *igt_crc_to_string(igt_crc_t *crc);

void igt_pipe_crc_check(igt_debugfs_t *debugfs);
igt_pipe_crc_t *
igt_pipe_crc_new(igt_debugfs_t *debugfs, int drm_fd, enum pipe pipe,
		 enum intel_pipe_crc_source source);
void igt_pipe_crc_free(igt_pipe_crc_t *pipe_crc);
bool igt_pipe_crc_start(igt_pipe_crc_t *pipe_crc);
void igt_pipe_crc_stop(igt_pipe_crc_t *pipe_crc);
void igt_pipe_crc_get_crcs(igt_pipe_crc_t *pipe_crc, int n_crcs,
			   igt_crc_t **out_crcs);

/*
 * Drop caches
 */

#define DROP_UNBOUND 0x1
#define DROP_BOUND 0x2
#define DROP_RETIRE 0x4
#define DROP_ACTIVE 0x8
#define DROP_ALL (DROP_UNBOUND | \
		  DROP_BOUND | \
		  DROP_RETIRE | \
		  DROP_ACTIVE)

void igt_drop_caches_set(uint64_t val);

#endif /* __IGT_DEBUGFS_H__ */
