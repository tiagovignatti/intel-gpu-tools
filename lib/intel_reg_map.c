/*
 * Copyright © 2011 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>

#include "intel_io.h"
#include "intel_chipset.h"
#include "igt_core.h"

static struct intel_register_range gen_bwcl_register_map[] = {
	{0x00000000, 0x00000fff, INTEL_RANGE_RW},
	{0x00001000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x00002000, 0x00000fff, INTEL_RANGE_RW},
	{0x00003000, 0x000001ff, INTEL_RANGE_RW},
	{0x00003200, 0x00000dff, INTEL_RANGE_RW},
	{0x00004000, 0x000003ff, INTEL_RANGE_RSVD},
	{0x00004400, 0x00000bff, INTEL_RANGE_RSVD},
	{0x00005000, 0x00000fff, INTEL_RANGE_RW},
	{0x00006000, 0x00000fff, INTEL_RANGE_RW},
	{0x00007000, 0x000003ff, INTEL_RANGE_RW},
	{0x00007400, 0x000014ff, INTEL_RANGE_RW},
	{0x00008900, 0x000006ff, INTEL_RANGE_RSVD},
	{0x00009000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x0000a000, 0x00000fff, INTEL_RANGE_RW},
	{0x0000b000, 0x00004fff, INTEL_RANGE_RSVD},
	{0x00010000, 0x00003fff, INTEL_RANGE_RW},
	{0x00014000, 0x0001bfff, INTEL_RANGE_RSVD},
	{0x00030000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00040000, 0x0001ffff, INTEL_RANGE_RSVD},
	{0x00060000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00070000, 0x00002fff, INTEL_RANGE_RW},
	{0x00073000, 0x00000fff, INTEL_RANGE_RW},
	{0x00074000, 0x0000bfff, INTEL_RANGE_RSVD},
	{0x00000000, 0x00000000, INTEL_RANGE_END}
};

static struct intel_register_range gen4_register_map[] = {
	{0x00000000, 0x00000fff, INTEL_RANGE_RW},
	{0x00001000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x00002000, 0x00000fff, INTEL_RANGE_RW},
	{0x00003000, 0x000001ff, INTEL_RANGE_RW},
	{0x00003200, 0x00000dff, INTEL_RANGE_RW},
	{0x00004000, 0x000003ff, INTEL_RANGE_RW},
	{0x00004400, 0x00000bff, INTEL_RANGE_RW},
	{0x00005000, 0x00000fff, INTEL_RANGE_RW},
	{0x00006000, 0x00000fff, INTEL_RANGE_RW},
	{0x00007000, 0x000003ff, INTEL_RANGE_RW},
	{0x00007400, 0x000014ff, INTEL_RANGE_RW},
	{0x00008900, 0x000006ff, INTEL_RANGE_RSVD},
	{0x00009000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x0000a000, 0x00000fff, INTEL_RANGE_RW},
	{0x0000b000, 0x00004fff, INTEL_RANGE_RSVD},
	{0x00010000, 0x00003fff, INTEL_RANGE_RW},
	{0x00014000, 0x0001bfff, INTEL_RANGE_RSVD},
	{0x00030000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00040000, 0x0001ffff, INTEL_RANGE_RSVD},
	{0x00060000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00070000, 0x00002fff, INTEL_RANGE_RW},
	{0x00073000, 0x00000fff, INTEL_RANGE_RW},
	{0x00074000, 0x0000bfff, INTEL_RANGE_RSVD},
	{0x00000000, 0x00000000, INTEL_RANGE_END}
};

/* The documentation is a little sketchy on these register ranges. */
static struct intel_register_range gen6_gt_register_map[] = {
	{0x00000000, 0x00000fff, INTEL_RANGE_RW},
	{0x00001000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x00002000, 0x00000fff, INTEL_RANGE_RW},
	{0x00003000, 0x000001ff, INTEL_RANGE_RW},
	{0x00003200, 0x00000dff, INTEL_RANGE_RW},
	{0x00004000, 0x00000fff, INTEL_RANGE_RW},
	{0x00005000, 0x0000017f, INTEL_RANGE_RW},
	{0x00005180, 0x00000e7f, INTEL_RANGE_RW},
	{0x00006000, 0x00001fff, INTEL_RANGE_RW},
	{0x00008000, 0x000007ff, INTEL_RANGE_RW},
	{0x00008800, 0x000000ff, INTEL_RANGE_RSVD},
	{0x00008900, 0x000006ff, INTEL_RANGE_RW},
	{0x00009000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x0000a000, 0x00000fff, INTEL_RANGE_RW},
	{0x0000b000, 0x00004fff, INTEL_RANGE_RSVD},
	{0x00010000, 0x00001fff, INTEL_RANGE_RW},
	{0x00012000, 0x000003ff, INTEL_RANGE_RW},
	{0x00012400, 0x00000bff, INTEL_RANGE_RW},
	{0x00013000, 0x00000fff, INTEL_RANGE_RW},
	{0x00014000, 0x00000fff, INTEL_RANGE_RW},
	{0x00015000, 0x0000cfff, INTEL_RANGE_RW},
	{0x00022000, 0x00000fff, INTEL_RANGE_RW},
	{0x00023000, 0x00000fff, INTEL_RANGE_RSVD},
	{0x00024000, 0x00000fff, INTEL_RANGE_RW},
	{0x00025000, 0x0000afff, INTEL_RANGE_RSVD},
	{0x00030000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00040000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00050000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00060000, 0x0000ffff, INTEL_RANGE_RW},
	{0x00070000, 0x00003fff, INTEL_RANGE_RW},
	{0x00074000, 0x0008bfff, INTEL_RANGE_RSVD},
	{0x00100000, 0x00007fff, INTEL_RANGE_RW},
	{0x00108000, 0x00037fff, INTEL_RANGE_RSVD},
	{0x00140000, 0x0003ffff, INTEL_RANGE_RW},
	{0x00000000, 0x00000000, INTEL_RANGE_END}
};

struct intel_register_map
intel_get_register_map(uint32_t devid)
{
	struct intel_register_map map;
	const int gen = intel_gen(devid);

	if (gen >= 6) {
		map.map = gen6_gt_register_map;
		map.top = 0x180000;
	} else if (IS_BROADWATER(devid) || IS_CRESTLINE(devid)) {
		map.map = gen_bwcl_register_map;
		map.top = 0x80000;
	} else if (gen >= 4) {
		map.map = gen4_register_map;
		map.top = 0x80000;
	} else {
		igt_fail_on("Gen2/3 Ranges are not supported. Please use ""unsafe access.");
	}

	map.alignment_mask = 0x3;

	return map;
}

struct intel_register_range *
intel_get_register_range(struct intel_register_map map, uint32_t offset, uint32_t mode)
{
	struct intel_register_range *range = map.map;
	uint32_t align = map.alignment_mask;

	if (offset & map.alignment_mask)
		return NULL;

	if (offset >= map.top)
		return NULL;

	while (!(range->flags & INTEL_RANGE_END)) {
		/*  list is assumed to be in order */
		if (offset < range->base)
			break;

		if ( (offset >= range->base) &&
		     (offset + align) <= (range->base + range->size)) {
			if ((mode & range->flags) == mode)
				return range;
		}
		range++;
	}

	return NULL;
}
