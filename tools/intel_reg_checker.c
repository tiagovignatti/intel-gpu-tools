/* Copyright © 2011 Intel Corporation
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
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <stdbool.h>
#include "intel_io.h"
#include "intel_chipset.h"

static uint32_t devid;
static int gen;

static inline uint32_t
read_reg(uint32_t reg)
{
	return *(volatile uint32_t *)((volatile char *)mmio + reg);
}

static uint32_t
read_and_print_reg(const char *name, uint32_t reg)
{
	uint32_t val = read_reg(reg);

	printf("%s (0x%x): 0x%08x\n", name, reg, val);

	return val;
}

static void
check_chicken_unset(const char *name, uint32_t reg)
{
	uint32_t val = read_and_print_reg(name, reg);


	if (val != 0) {
		fprintf(stderr, "           WARN: chicken bits set\n");
	} else {
		printf("           OK:   chicken bits unset\n");
	}
}

static void
check_bit(uint32_t val, int bit, const char *bitname, bool set)
{
	if (!!(val & (1 << bit)) != set) {
		fprintf(stderr, "  (bit %2d) FAIL: %s must be %s\n",
			bit, bitname, set ? "set" : "unset");
	} else {
		printf("  (bit %2d) OK:   %s\n", bit, bitname);
	}
}

static void
check_perf_bit(uint32_t val, int bit, const char *bitname, bool set)
{
	if (!!(val & (1 << bit)) != set) {
		printf("  (bit %2d) PERF: %s should be %s\n",
			bit, bitname, set ? "set" : "unset");
	} else {
		printf("  (bit %2d) OK:   %s\n", bit, bitname);
	}
}

static void
check_mi_mode(void)
{
	/* Described in page 14-16 of the IHD_OS_Vol1_Part3.pdf
	 * specification.
	 */

	uint32_t mi_mode = read_and_print_reg("MI_MODE", 0x209c);

	/* From page 14:
	 *
	 * Async Flip Performance mode
	 * Project: All
	 * Default Value: 0h
	 * Format: U1
	 * [DevSNB] This bit must be set to ‘1’
	 */
	if (gen == 6)
		check_bit(mi_mode, 14, "Async Flip Performance mode", true);
	else
		check_perf_bit(mi_mode, 14, "Async Flip Performance mode",
			       false);

	check_perf_bit(mi_mode, 13, "Flush Performance Mode", false);

	/* Our driver relies on MI_FLUSH, unfortunately. */
	if (gen >= 6)
		check_bit(mi_mode, 12, "MI_FLUSH enable", true);

	/* From page 15:
	 *
	 *     "1h: LRA mode of allocation. Used for validation purposes"
	 */
	if (gen < 7)
		check_bit(mi_mode, 7, "Vertex Shader Cache Mode", false);

	/* From page 16:
	 *
	 *     "To avoid deadlock conditions in hardware this bit
	 *      needs to be set for normal operation.
	 */
	check_bit(mi_mode, 6, "Vertex Shader Timer Dispatch Enable", true);
}

static void
check_gfx_mode(void)
{
	/* Described in page 17-19 of the IHD_OS_Vol1_Part3.pdf
	 * specification.
	 */
	uint32_t gfx_mode;

	if (gen < 6)
		return;

	if (gen == 6)
		gfx_mode = read_and_print_reg("GFX_MODE", 0x2520);
	else
		gfx_mode = read_and_print_reg("GFX_MODE", 0x229c);

	/* Our driver only updates page tables at batchbuffer
	 * boundaries, so we don't need TLB flushes at other times.
	 */
	check_perf_bit(gfx_mode, 13, "Flush TLB Invalidation Mode", true);
}

static void
check_gt_mode(void)
{
	/* Described in page 20-22 of the IHD_OS_Vol1_Part3.pdf
	 * specification.
	 */
	uint32_t gt_mode;

	if (gen < 6)
		return;

	if (gen == 6)
		gt_mode = read_and_print_reg("GT_MODE", 0x20d0);
	else
		gt_mode = read_and_print_reg("GT_MODE", 0x7008);

	if (gen == 6)
		check_perf_bit(gt_mode, 8, "Full Rate Sampler Disable", false);

	/* For DevSmallGT, this bit must be set, which means disable
	 * hashing.
	 */
	if (devid == PCI_CHIP_SANDYBRIDGE_GT1 ||
	    devid == PCI_CHIP_SANDYBRIDGE_M_GT1)
		check_bit(gt_mode, 6, "WIZ Hashing disable", true);
	else if (gen == 6)
		check_perf_bit(gt_mode, 6, "WIZ Hashing disable", false);

	if (gen == 6) {
		check_perf_bit(gt_mode, 5, "TD Four Row Dispatch Disable",
			       false);
		check_perf_bit(gt_mode, 4, "Full Size URB Disable", false);
		check_perf_bit(gt_mode, 3, "Full Size SF FIFO Disable", false);
		check_perf_bit(gt_mode, 1, "VS Quad Thread Dispatch Disable",
			       false);
	}
}

static void
check_cache_mode_0(void)
{
	/* Described in page 23-25 of the IHD_OS_Vol1_Part3.pdf
	 * specification.
	 */
	uint32_t cache_mode_0;

	if (gen >= 7)
		cache_mode_0 = read_and_print_reg("CACHE_MODE_0", 0x7000);
	else
		cache_mode_0 = read_and_print_reg("CACHE_MODE_0", 0x2120);

	check_perf_bit(cache_mode_0, 15, "Sampler L2 Disable", false);
	check_perf_bit(cache_mode_0, 9, "Sampler L2 TLB Prefetch Enable", true);
	check_perf_bit(cache_mode_0, 8,
		       "Depth Related Cache Pipelined Flush Disable", false);

	/* From page 24:
	 *
	 *     "If this bit is set, RCCunit will have LRA as
	 *      replacement policy. The default value i.e. ( when this
	 *      bit is reset ) indicates that non-LRA eviction
	 *      policy. This bit must be reset. LRA replacement policy
	 *      is not supported."
	 *
	 * And the same for STC Eviction Policy.
	 */
	check_bit(cache_mode_0, 5, "STC LRA Eviction Policy", false);
	if (gen >= 6)
		check_bit(cache_mode_0, 4, "RCC LRA Eviction Policy", false);

	check_perf_bit(cache_mode_0, 3, "Hierarchical Z Disable", false);

	if (gen == 6) {
		check_perf_bit(cache_mode_0, 2,
			       "Hierarchical Z RAW Stall Optimization "
			       "Disable", false);
	}

	/* From page 25:
	 *
	 *     "This bit must be 0. Operational Flushes [DevSNB] are
	 *      not supported in [DevSNB].  SW must flush the render
	 *      target after front buffer rendering."
	 */
	check_bit(cache_mode_0, 0, "Render Cache Operational Flush", false);
}


static void
check_cache_mode_1(void)
{
	/* Described in page 23-25 of the IHD_OS_Vol1_Part3.pdf
	 * specification.
	 */
	uint32_t cache_mode_1;

	if (gen >= 7)
		cache_mode_1 = read_and_print_reg("CACHE_MODE_1", 0x7004);
	else
		cache_mode_1 = read_and_print_reg("CACHE_MODE_1", 0x2124);

	if (gen >= 7) {
		check_perf_bit(cache_mode_1, 13,
			       "STC Address Lookup Optimization Disable",
			       false);
	}

	/* From page 24:
	 *
	 *     "If this bit is set, Hizunit will have LRA as
	 *      replacement policy. The default value i.e.  (when this
	 *      bit is reset) indicates the non-LRA eviction
	 *      policy. For performance reasons, this bit must be
	 *      reset."
	 */
	check_bit(cache_mode_1, 12, "HIZ LRA Eviction Policy", false);

	/* Page 26 describes these bits as reserved (debug only). */
	check_bit(cache_mode_1, 11,
		  "DAP Instruction and State Cache Invalidate", false);
	check_bit(cache_mode_1, 10,
		  "Instruction L1 Cache and In-Flight Queue Disable",
		  false);
	check_bit(cache_mode_1, 9, "Instruction L2 Cache Fill Buffers Disable",
		  false);


	if (gen >= 7) {
		check_perf_bit(cache_mode_1, 6,
			       "Pixel Backend sub-span collection "
			       "Optimization Disable",
			       false);
		check_perf_bit(cache_mode_1, 5, "MCS Cache Disable", false);
	}
	check_perf_bit(cache_mode_1, 4, "Data Disable", false);

	if (gen == 6) {
		/* In a later update of the documentation, it says:
		 *
		 *     "[DevSNB:A0{WKA1}] [DevSNB]: This bit must be
		 *      set for depth buffer format
		 *      D24_UNORM_S8_UINT."
		 *
		 * XXX: Does that mean A0 only, or all DevSNB?
		 */
		check_perf_bit(cache_mode_1, 3,
			       "Depth Read Hit Write-Only Optimization "
			       "Disable", false);

		check_perf_bit(cache_mode_1, 2,
			       "Depth Cache LRA Hunt Feature Disable",
			       false);
	}

	check_bit(cache_mode_1, 1, "Instruction and State L2 Cache Disable",
		  false);
	check_bit(cache_mode_1, 0, "Instruction and State L1 Cache Disable",
		  false);
}


static void
check_3d_chicken4(void)
{
	/* Described in page 23-25 of the IHD_OS_Vol1_Part3.pdf
	 * specification.
	 */
	uint32_t _3d_chicken4 = read_and_print_reg("3D_CHICKEN4", 0x20d4);

	check_perf_bit(_3d_chicken4, 6, "3D Scoreboard Hashing Enable", true);

	if (_3d_chicken4 & 0x0fbf) {
		fprintf(stderr,
			"         WARN:   other non-thread deps bits set\n");
	} else {
		printf("           OK:   other non-thread deps bits unset\n");
	}
}

static void
check_dpfc_control_sa(void)
{
	uint32_t dpfc_control_sa;

	if (gen != 6)
		return;

	dpfc_control_sa = read_and_print_reg("DPFC_CONTROL_SA", 0x100100);

	/* This is needed for framebuffer compression for us to be
	 * able to access the framebuffer by the CPU through the GTT.
	 */
	check_bit(dpfc_control_sa, 29, "CPU Fence Enable", true);
}

int main(int argc, char** argv)
{
	struct pci_device *dev;

	dev = intel_get_pci_device();
	devid = dev->device_id;
	intel_mmio_use_pci_bar(dev);

	if (IS_GEN7(devid))
		gen = 7;
	else if (IS_GEN6(devid))
		gen = 6;
	else if (IS_GEN5(devid))
		gen = 5;
	else
		gen = 4;

	check_mi_mode();
	check_gfx_mode();
	check_gt_mode();
	check_cache_mode_0();
	check_cache_mode_1();

	if (gen < 7) {
 		check_chicken_unset("3D_CHICKEN", 0x2084);
 		check_chicken_unset("3D_CHICKEN2", 0x208c);
	} else {
		check_chicken_unset("FF_SLICE_CHICKEN", 0x2088);
	}
	if (gen >= 6)
		check_chicken_unset("3D_CHICKEN3", 0x2090);
	if (gen == 6)
		check_3d_chicken4();

	if (gen >= 7) {
		check_chicken_unset("FF_SLICE_CS_CHICKEN1", 0x20e0);
		check_chicken_unset("FF_SLICE_CS_CHICKEN2", 0x20e4);
		check_chicken_unset("FF_SLICE_CS_CHICKEN3", 0x20e8);
		check_chicken_unset("COMMON_SLICE_CHICKEN1", 0x7010);
		check_chicken_unset("COMMON_SLICE_CHICKEN2", 0x7014);
		check_chicken_unset("WM_CHICKEN", 0x5580);
		check_chicken_unset("HALF_SLICE_CHICKEN", 0xe100);
		check_chicken_unset("HALF_SLICE_CHICKEN2", 0xe180);
		check_chicken_unset("ROW_CHICKEN", 0xe4f0);
		check_chicken_unset("ROW_CHICKEN2", 0xe4f4);
	}

	check_chicken_unset("ECOSKPD", 0x21d0);

	check_dpfc_control_sa();

	return 0;
}

