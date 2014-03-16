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

/**
 * igt_debugfs_t:
 *
 * debugfs access structure. Needs to be initialized with igt_debugfs_init().
 */
typedef struct {
	char root[128];
	char dri_path[128];
} igt_debugfs_t;

void igt_debugfs_init(igt_debugfs_t *debugfs);
int igt_debugfs_open(const char *filename, int mode);
FILE *igt_debugfs_fopen(const char *filename,
			const char *mode);

/*
 * Pipe CRC
 */

/**
 * igt_pipe_crc_t:
 *
 * Pipe CRC support structure. Needs to be allocated and set up with
 * igt_pipe_crc_new() for a specific pipe and pipe CRC source value.
 */
typedef struct _igt_pipe_crc igt_pipe_crc_t;

/**
 * igt_crc_t:
 * @frame: frame number of the capture CRC
 * @n_words: internal field, don't access
 * @crc: internal field, don't access
 *
 * Pipe CRC value. All other members than @frame are private and should not be
 * inspected by testcases.
 */
typedef struct {
	uint32_t frame;
	int n_words;
	uint32_t crc[5];
} igt_crc_t;

/**
 * intel_pipe_crc_source:
 *
 * Enumeration of all supported pipe CRC sources. Not all platforms and all
 * outputs support all of them. Generic tests should just use
 * INTEL_PIPE_CRC_SOURCE_AUTO. It should always map to an end-of-pipe CRC
 * suitable for checking planes, cursor, color correction and any other
 * output-agnostic features.
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

bool igt_crc_is_null(igt_crc_t *crc);
bool igt_crc_equal(igt_crc_t *a, igt_crc_t *b);
char *igt_crc_to_string(igt_crc_t *crc);

void igt_pipe_crc_check(igt_debugfs_t *debugfs);
igt_pipe_crc_t *
igt_pipe_crc_new(igt_debugfs_t *debugfs, enum pipe pipe,
		 enum intel_pipe_crc_source source);
void igt_pipe_crc_free(igt_pipe_crc_t *pipe_crc);
void igt_pipe_crc_start(igt_pipe_crc_t *pipe_crc);
void igt_pipe_crc_stop(igt_pipe_crc_t *pipe_crc);
void igt_pipe_crc_get_crcs(igt_pipe_crc_t *pipe_crc, int n_crcs,
			   igt_crc_t **out_crcs);
void igt_pipe_crc_collect_crc(igt_pipe_crc_t *pipe_crc, igt_crc_t *out_crc);

/*
 * Drop caches
 */

/**
 * DROP_UNBOUND:
 *
 * Drop all currently unbound gem buffer objects from the cache.
 */
#define DROP_UNBOUND 0x1
/**
 * DROP_BOUND:
 *
 * Drop all inactive objects which are bound into some gpu address space.
 */
#define DROP_BOUND 0x2
/**
 * DROP_RETIRE:
 *
 * Wait for all outstanding gpu commands to complete, but do not take any
 * further actions.
 */
#define DROP_RETIRE 0x4
/**
 * DROP_ACTIVE:
 *
 * Also drop active objects once retired.
 */
#define DROP_ACTIVE 0x8
#define DROP_ALL (DROP_UNBOUND | \
		  DROP_BOUND | \
		  DROP_RETIRE | \
		  DROP_ACTIVE)

void igt_drop_caches_set(uint64_t val);

/*
 * Prefault control
 */

void igt_disable_prefault(void);
void igt_enable_prefault(void);

int igt_open_forcewake_handle(void);

#endif /* __IGT_DEBUGFS_H__ */
