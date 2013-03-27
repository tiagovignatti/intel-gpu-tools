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
 */

#include <unistd.h>
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

#define STATS_CHECK_FREQUENCY	100
#define STATS_REPORT_FREQUENCY	2

int
main(int argc, char **argv)
{
	uint32_t devid;
	int counter_count;
	const char **counter_name;
	void (*get_counters)(void);
	int i;
	char clear_screen[] = {0x1b, '[', 'H',
			       0x1b, '[', 'J',
			       0x0};
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
	} else {
		printf("This tool is not yet supported on your platform.\n");
		abort();
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

	free(totals);
	free(last_counter);

	return 0;
}
