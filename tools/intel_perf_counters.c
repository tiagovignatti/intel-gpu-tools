/*
 * Copyright © 2010, 2013 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Kenneth Graunke <kenneth@whitecape.org>
 *
 * While documentation for performance counters is suspiciously missing from the
 * Sandybridge PRM, they were documented in Volume 1 Part 3 of the Ironlake PRM.
 *
 * A lot of the Ironlake PRM actually unintentionally documents Sandybridge
 * due to mistakes made when updating the documentation for Gen6+.  Many of
 * these mislabeled sections carried forward to the public documentation.
 *
 * The Ironlake PRMs have been publicly available since 2010 and are online at:
 * https://01.org/linuxgraphics/documentation/2010-intel-core-processor-family
 */

#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <sys/ioctl.h>

#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_gpu_tools.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"

#define GEN5_COUNTER_COUNT 29

const char *gen5_counter_names[GEN5_COUNTER_COUNT] = {
	"cycles the CS unit is starved",
	"cycles the CS unit is stalled",
	"cycles the VF unit is starved",
	"cycles the VF unit is stalled",
	"cycles the VS unit is starved",
	"cycles the VS unit is stalled",
	"cycles the GS unit is starved",
	"cycles the GS unit is stalled",
	"cycles the CL unit is starved",
	"cycles the CL unit is stalled",
	"cycles the SF unit is starved",
	"cycles the SF unit is stalled",
	"cycles the WZ unit is starved",
	"cycles the WZ unit is stalled",
	"Z buffer read/write          ",
	"cycles each EU was active    ",
	"cycles each EU was suspended ",
	"cycles threads loaded all EUs",
	"cycles filtering active      ",
	"cycles PS threads executed   ",
	"subspans written to RC       ",
	"bytes read for texture reads ",
	"texels returned from sampler ",
	"polygons not culled          ",
	"clocks MASF has valid message",
	"64b writes/reads from RC     ",
	"reads on dataport            ",
	"clocks MASF has valid msg not consumed by sampler",
	"cycles any EU is stalled for math",
};

#define GEN6_COUNTER_COUNT 29

/**
 * Sandybridge: Counter Select = 001
 * A0   A1   A2   A3   A4   TIMESTAMP RPT_ID
 * A5   A6   A7   A8   A9   A10  A11  A12
 * A13  A14  A15  A16  A17  A18  A19  A20
 * A21  A22  A23  A24  A25  A26  A27  A28
 */
const int gen6_counter_format = 1;

/**
 * Names for aggregating counters A0-A28.
 *
 * While the Ironlake PRM clearly documents that there are 29 counters (A0-A28),
 * it only lists the names for 28 of them; one is missing.  However, careful
 * examination reveals a pattern: there are five GS counters (Active, Stall,
 * Core Stall, # threads loaded, and ready but not running time).  There are
 * also five PS counters, in the same order.  But there are only four VS
 * counters listed - the number of VS threads loaded is missing.  Presumably,
 * it exists and is counter 5, and the rest are shifted over one place.
 */
const char *gen6_counter_names[GEN6_COUNTER_COUNT] = {
	[0]  = "Aggregated Core Array Active",
	[1]  = "Aggregated Core Array Stalled",
	[2]  = "Vertex Shader Active Time",
	[3]  = "Vertex Shader Stall Time",
	[4]  = "Vertex Shader Stall Time - Core Stall",
	[5]  = "# VS threads loaded",
	[6]  = "Vertex Shader Ready but not running time",
	[7]  = "Geometry Shader Active Time",
	[8]  = "Geometry Shader Stall Time",
	[9]  = "Geometry Shader Stall Time - Core Stall",
	[10] = "# GS threads loaded",
	[11] = "Geometry Shader ready but not running Time",
	[12] = "Pixel Shader Active Time",
	[13] = "Pixel Shader Stall Time",
	[14] = "Pixel Shader Stall Time - Core Stall",
	[15] = "# PS threads loaded",
	[16] = "Pixel Shader ready but not running Time",
	[17] = "Early Z Test Pixels Passing",
	[18] = "Early Z Test Pixels Failing",
	[19] = "Early Stencil Test Pixels Passing",
	[20] = "Early Stencil Test Pixels Failing",
	[21] = "Pixel Kill Count",
	[22] = "Alpha Test Pixels Failed",
	[23] = "Post PS Stencil Pixels Failed",
	[24] = "Post PS Z buffer Pixels Failed",
	[25] = "Pixels/samples Written in the frame buffer",
	[26] = "GPU Busy",
	[27] = "CL active and not stalled",
	[28] = "SF active and stalled",
};

int have_totals = 0;
uint32_t *totals;
uint32_t *last_counter;
static drm_intel_bufmgr *bufmgr;
struct intel_batchbuffer *batch;

/* DW0 */
#define GEN5_MI_REPORT_PERF_COUNT ((0x26 << 23) | (3 - 2))
#define MI_COUNTER_SET_0	(0 << 6)
#define MI_COUNTER_SET_1	(1 << 6)
/* DW1 */
#define MI_COUNTER_ADDRESS_GTT	(1 << 0)
/* DW2: report ID */

/**
 * According to the Sandybridge PRM, Volume 1, Part 1, page 48,
 * MI_REPORT_PERF_COUNT is now opcode 0x28.  The Ironlake PRM, Volume 1,
 * Part 3 details how it works.
 */
/* DW0 */
#define GEN6_MI_REPORT_PERF_COUNT (0x28 << 23)
/* DW1 and 2 are the same as above */

/* OACONTROL exists on Gen6+ but is documented in the Ironlake PRM */
#define OACONTROL                       0x2360
# define OACONTROL_COUNTER_SELECT_SHIFT 2
# define PERFORMANCE_COUNTER_ENABLE     (1 << 0)

static void
gen5_get_counters(void)
{
	int i;
	drm_intel_bo *stats_bo;
	uint32_t *stats_result;

	stats_bo = drm_intel_bo_alloc(bufmgr, "stats", 4096, 4096);

	BEGIN_BATCH(6);
	OUT_BATCH(GEN5_MI_REPORT_PERF_COUNT | MI_COUNTER_SET_0);
	OUT_RELOC(stats_bo,
		  I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
		  0);
	OUT_BATCH(0);

	OUT_BATCH(GEN5_MI_REPORT_PERF_COUNT | MI_COUNTER_SET_1);
	OUT_RELOC(stats_bo,
		  I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
		  64);
	OUT_BATCH(0);

	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);

	drm_intel_bo_map(stats_bo, 0);
	stats_result = stats_bo->virtual;
	/* skip REPORT_ID, TIMESTAMP */
	stats_result += 3;
	for (i = 0 ; i < GEN5_COUNTER_COUNT; i++) {
		totals[i] += stats_result[i] - last_counter[i];
		last_counter[i] = stats_result[i];
	}

	drm_intel_bo_unmap(stats_bo);
	drm_intel_bo_unreference(stats_bo);
}

static void
gen6_get_counters(void)
{
	int i;
	drm_intel_bo *stats_bo;
	uint32_t *stats_result;

	/* Map from counter names to their index in the buffer object */
	static const int buffer_index[GEN6_COUNTER_COUNT] =
	{
		7,   6,  5,  4,  3,
		15, 14, 13, 12, 11, 10,  9,  8,
		23, 22, 21, 20, 19, 18, 17, 16,
		31, 30, 29, 28, 27, 26, 25, 24,
	};

	stats_bo = drm_intel_bo_alloc(bufmgr, "stats", 4096, 4096);

	BEGIN_BATCH(3);
	OUT_BATCH(GEN6_MI_REPORT_PERF_COUNT | (3 - 2));
	OUT_RELOC(stats_bo,
		  I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
		  MI_COUNTER_ADDRESS_GTT);
	OUT_BATCH(0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush_on_ring(batch, I915_EXEC_RENDER);

	drm_intel_bo_map(stats_bo, 0);
	stats_result = stats_bo->virtual;
	for (i = 0; i < GEN6_COUNTER_COUNT; i++) {
		totals[i] += stats_result[buffer_index[i]] - last_counter[i];
		last_counter[i] = stats_result[buffer_index[i]];
	}

	drm_intel_bo_unmap(stats_bo);
	drm_intel_bo_unreference(stats_bo);
}

#define STATS_CHECK_FREQUENCY	100
#define STATS_REPORT_FREQUENCY	2

int
main(int argc, char **argv)
{
	uint32_t devid;
	int counter_format;
	int counter_count;
	const char **counter_name;
	void (*get_counters)(void);
	int i;
	char clear_screen[] = {0x1b, '[', 'H',
			       0x1b, '[', 'J',
			       0x0};
	bool oacontrol = true;
	int fd;
	int l;

	fd = drm_open_any();
	devid = intel_get_drm_devid(fd);

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch = intel_batchbuffer_alloc(bufmgr, devid);

	if (IS_GEN5(devid)) {
		counter_name = gen5_counter_names;
		counter_count = GEN5_COUNTER_COUNT;
		get_counters = gen5_get_counters;
		oacontrol = false;
	} else if (IS_GEN6(devid)) {
		counter_name = gen6_counter_names;
		counter_count = GEN6_COUNTER_COUNT;
		counter_format = gen6_counter_format;
		get_counters = gen6_get_counters;
	} else {
		printf("This tool is not yet supported on your platform.\n");
		abort();
	}

	if (oacontrol) {
		/* Forcewake */
		intel_register_access_init(intel_get_pci_device(), 0);

		/* Enable performance counters */
		intel_register_write(OACONTROL,
			counter_format << OACONTROL_COUNTER_SELECT_SHIFT |
			PERFORMANCE_COUNTER_ENABLE);
	}

	totals = calloc(counter_count, sizeof(uint32_t));
	last_counter = calloc(counter_count, sizeof(uint32_t));

	for (;;) {
		for (l = 0; l < STATS_CHECK_FREQUENCY; l++) {
			printf("%s", clear_screen);

			if (l % (STATS_CHECK_FREQUENCY / STATS_REPORT_FREQUENCY) == 0) {
				if (have_totals) {
					for (i = 0; i < counter_count; i++) {
						printf("%s: %u\n", counter_name[i],
						       totals[i]);
						totals[i] = 0;
					}
				}
			}

			get_counters();
			have_totals = 1;

			usleep(1000000 / STATS_CHECK_FREQUENCY);
		}
	}

	if (oacontrol) {
		/* Disable performance counters */
		intel_register_write(OACONTROL, 0);

		/* Forcewake */
		intel_register_access_fini();
	}

	free(totals);
	free(last_counter);

	return 0;
}
