/*
 * Copyright © 2015 Intel Corporation
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
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <err.h>
#include <string.h>
#include "intel_io.h"
#include "intel_chipset.h"

static uint32_t display_base;
static uint32_t devid;

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

static uint32_t read_reg(uint32_t addr)
{
	return INREG(display_base + addr);
}

struct gmch_wm {
	int wm, wm1, dl, fifo, fbc, burst;
	bool dl_prec, valid;
};

enum plane {
	PRI_HPLL_SR,
	CUR_HPLL_SR,
	PRI_SR,
	CUR_SR,
	PRI_A,
	CUR_A,
	SPR_A,
	SPR_B,
	PRI_B,
	CUR_B,
	SPR_C,
	SPR_D,
	PRI_C,
	CUR_C,
	SPR_E,
	SPR_F,
	MAX_PLANE,
};

#define NAME(x) [x] = #x

static const char * const plane_name[] = {
	NAME(PRI_HPLL_SR),
	NAME(CUR_HPLL_SR),
	NAME(PRI_SR),
	NAME(CUR_SR),
	NAME(PRI_A),
	NAME(CUR_A),
	NAME(SPR_A),
	NAME(SPR_B),
	NAME(PRI_B),
	NAME(CUR_B),
	NAME(SPR_C),
	NAME(SPR_D),
	NAME(PRI_C),
	NAME(CUR_C),
	NAME(SPR_E),
	NAME(SPR_F),
};

struct ilk_wm_level {
	int primary, sprite, cursor, latency, fbc;
	bool enabled, sprite_enabled;
	bool primary_trickle_feed_dis, sprite_trickle_feed_dis;
};

struct ilk_wm {
	struct ilk_wm_level pipe[3];
	struct {
		int linetime, ips;
	} linetime[3];
	struct ilk_wm_level lp[3];
};

#define MASK(size) ((1 << (size)) - 1)

#define REG_DECODE1(x, shift, size) \
	(((x) >> (shift)) & MASK(size))

#define REG_DECODE2(lo, shift_lo, size_lo, hi, shift_hi, size_hi) \
	((((lo) >> (shift_lo)) & MASK(size_lo)) | \
	 ((((hi) >> (shift_hi)) & MASK(size_hi)) << (size_lo)))

static const char pipe_name(int pipe)
{
	return 'A' + pipe;
}

static const char *endis(bool enabled)
{
	return enabled ? "enabled" : "disabled";
}

static int is_gen7_plus(uint32_t d)
{
	return !(IS_GEN5(d) || IS_GEN6(d));
}

static int is_hsw_plus(uint32_t d)
{
	return !(IS_GEN5(d) || IS_GEN6(d) || IS_IVYBRIDGE(d));
}

static void ilk_wm_dump(void)
{
	int i;
	uint32_t dspcntr[3];
	uint32_t spcntr[3];
	uint32_t wm_pipe[3];
	uint32_t wm_linetime[3];
	uint32_t wm_lp[3];
	uint32_t wm_lp_spr[3];
	uint32_t arb_ctl, arb_ctl2, wm_misc = 0;
	int num_pipes = is_gen7_plus(devid) ? 3 : 2;
	struct ilk_wm wm = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	for (i = 0; i < num_pipes; i++) {
		dspcntr[i] = read_reg(0x70180 + i * 0x1000);
		if (is_gen7_plus(devid))
			spcntr[i] = read_reg(0x70280 + i * 0x1000);
		else
			spcntr[i] = read_reg(0x72180 + i * 0x1000);
	}

	wm_pipe[0] = read_reg(0x45100);
	wm_pipe[1] = read_reg(0x45104);
	if (num_pipes == 3)
		wm_pipe[2] = read_reg(0x45200);

	if (is_hsw_plus(devid)) {
		wm_linetime[0] = read_reg(0x45270);
		wm_linetime[1] = read_reg(0x45274);
		wm_linetime[2] = read_reg(0x45278);
	}

	wm_lp[0] = read_reg(0x45108);
	wm_lp[1] = read_reg(0x4510c);
	wm_lp[2] = read_reg(0x45110);

	wm_lp_spr[0] = read_reg(0x45120);
	if (is_gen7_plus(devid)) {
		wm_lp_spr[1] = read_reg(0x45124);
		wm_lp_spr[2] = read_reg(0x45128);
	}

	arb_ctl = read_reg(0x45000);
	arb_ctl2 = read_reg(0x45004);
	if (is_hsw_plus(devid))
		wm_misc = read_reg(0x45260);

	intel_register_access_fini();

	for (i = 0; i < num_pipes; i++)
		printf("    WM_PIPE_%c = 0x%08x\n", pipe_name(i), wm_pipe[i]);
	if (is_hsw_plus(devid)) {
		for (i = 0; i < num_pipes; i++)
			printf("WM_LINETIME_%c = 0x%08x\n", pipe_name(i), wm_linetime[i]);
	}
	printf("       WM_LP1 = 0x%08x\n", wm_lp[0]);
	printf("       WM_LP2 = 0x%08x\n", wm_lp[1]);
	printf("       WM_LP3 = 0x%08x\n", wm_lp[2]);
	printf("   WM_LP1_SPR = 0x%08x\n", wm_lp_spr[0]);
	if (is_gen7_plus(devid)) {
		printf("   WM_LP2_SPR = 0x%08x\n", wm_lp_spr[1]);
		printf("   WM_LP3_SPR = 0x%08x\n", wm_lp_spr[2]);
	}
	printf("      ARB_CTL = 0x%08x\n", arb_ctl);
	printf("     ARB_CTL2 = 0x%08x\n", arb_ctl2);
	if (is_hsw_plus(devid))
		printf("      WM_MISC = 0x%08x\n", wm_misc);

	for (i = 0 ; i < num_pipes; i++) {
		wm.pipe[i].primary = REG_DECODE1(wm_pipe[i], 16, 8);
		wm.pipe[i].sprite = REG_DECODE1(wm_pipe[i], 8, 8);
		wm.pipe[i].cursor = REG_DECODE1(wm_pipe[i], 0, 6);

		if (is_hsw_plus(devid)) {
			wm.linetime[i].linetime = REG_DECODE1(wm_linetime[i], 0, 9);
			wm.linetime[i].ips = REG_DECODE1(wm_linetime[i], 16, 9);
		}

		wm.pipe[i].primary_trickle_feed_dis =
			REG_DECODE1(dspcntr[i], 14, 1);
		if (!IS_GEN5(devid))
			wm.pipe[i].sprite_trickle_feed_dis =
				REG_DECODE1(spcntr[i], 14, 1);
	}

	for (i = 0; i < 3; i++) {
		wm.lp[i].enabled = REG_DECODE1(wm_lp[i], 31, 1);
		wm.lp[i].latency = REG_DECODE1(wm_lp[i], 24, 7);
		if (IS_GEN8(devid))
			wm.lp[i].fbc = REG_DECODE1(wm_lp[i], 19, 5);
		else
			wm.lp[i].fbc = REG_DECODE1(wm_lp[i], 20, 4);
		wm.lp[i].primary = REG_DECODE1(wm_lp[i], 8, 11);
		wm.lp[i].cursor = REG_DECODE1(wm_lp[i], 0, 8);

		if (i == 0 || is_gen7_plus(devid)) {
			if (!is_gen7_plus(devid))
				wm.lp[i].sprite_enabled = REG_DECODE1(wm_lp_spr[i], 31, 1);
			wm.lp[i].sprite = REG_DECODE1(wm_lp_spr[i], 0, 11);
		}
	}

	for (i = 0; i < num_pipes; i++) {
		printf("WM_PIPE_%c: primary=%d, cursor=%d, sprite=%d\n",
		       pipe_name(i), wm.pipe[i].primary, wm.pipe[i].cursor, wm.pipe[i].sprite);
	}
	if (is_hsw_plus(devid)) {
		for (i = 0; i < num_pipes; i++) {
			printf("WM_LINETIME_%c: line time=%d, ips line time=%d\n",
			       pipe_name(i), wm.linetime[i].linetime, wm.linetime[i].ips);
		}
	}
	if (is_gen7_plus(devid)) {
		for (i = 0; i < 3; i++) {
			printf("WM_LP%d: %s, latency=%d, fbc=%d, primary=%d, cursor=%d, sprite=%d\n",
			       i + 1, endis(wm.lp[i].enabled), wm.lp[i].latency, wm.lp[i].fbc,
			       wm.lp[i].primary, wm.lp[i].cursor, wm.lp[i].sprite);
		}
	} else {
		i = 0;
		printf("WM_LP%d: %s, latency=%d, fbc=%d, primary=%d, cursor=%d, sprite=%d (%s)\n",
		       i + 1, endis(wm.lp[i].enabled), wm.lp[i].latency, wm.lp[i].fbc,
		       wm.lp[i].primary, wm.lp[i].cursor, wm.lp[i].sprite,
		       endis(wm.lp[i].sprite_enabled));
		for (i = 1; i < 3; i++) {
			printf("WM_LP%d: %s, latency=%d, fbc=%d, primary=%d, cursor=%d\n",
			       i + 1, endis(wm.lp[i].enabled), wm.lp[i].latency, wm.lp[i].fbc,
			       wm.lp[i].primary, wm.lp[i].cursor);
		}
	}
	for (i = 0; i < num_pipes; i++) {
		printf("Primary %c trickle feed = %s\n",
		       pipe_name(i), endis(!wm.pipe[i].primary_trickle_feed_dis));
		if (!IS_GEN5(devid))
			printf("Sprite %c trickle feed = %s\n",
			       pipe_name(i), endis(!wm.pipe[i].sprite_trickle_feed_dis));
	}
	if (is_hsw_plus(devid)) {
		printf("DDB partitioning = %s\n",
		       REG_DECODE1(wm_misc, 0, 1) ? "5/6" : "1/2");
	} else if (is_gen7_plus(devid)) {
		printf("DDB partitioning = %s\n",
		       REG_DECODE1(arb_ctl2, 6, 1) ? "5/6" : "1/2");
	}
	printf("FBC watermark = %s\n",
	       endis(!REG_DECODE1(arb_ctl, 15, 1)));
}

static void vlv_wm_dump(void)
{
	int i;
	unsigned int num_pipes = IS_CHERRYVIEW(devid) ? 3 : 2;
	uint32_t dsparb, dsparb2, dsparb3;
	uint32_t fw1, fw2, fw3, fw4, fw5, fw6, fw7, fw8, fw9, howm, howm1;
	uint32_t ddl1, ddl2, ddl3;
	uint32_t fw_blc_self, mi_arb,cbr1;
	uint32_t dsp_ss_pm, ddr_setup2;
	struct gmch_wm wms[MAX_PLANE] = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	dsparb = read_reg(0x70030);
	dsparb2 = read_reg(0x70060);

	fw1 = read_reg(0x70034);
	fw2 = read_reg(0x70038);
	fw3 = read_reg(0x7003c);
	fw4 = read_reg(0x70070);
	fw5 = read_reg(0x70074);
	fw6 = read_reg(0x70078);

	howm = read_reg(0x70064);
	howm1 = read_reg(0x70068);

	ddl1 = read_reg(0x70050);
	ddl2 = read_reg(0x70054);

	fw_blc_self = read_reg(0x6500);
	mi_arb = read_reg(0x6504);
	cbr1 = read_reg(0x70400);

	if (IS_CHERRYVIEW(devid)) {
		dsparb3 = read_reg(0x7006c);

		fw7 = read_reg(0x700b4);
		fw8 = read_reg(0x700b8);
		fw9 = read_reg(0x7007c);

		ddl3 = read_reg(0x70058);

		intel_punit_read(0x36, &dsp_ss_pm);
		intel_punit_read(0x139, &ddr_setup2);
	} else {
		fw7 = read_reg(0x7007c);
	}

	intel_register_access_fini();

	printf("        FW1 = 0x%08x\n", fw1);
	printf("        FW2 = 0x%08x\n", fw2);
	printf("        FW3 = 0x%08x\n", fw3);
	printf("        FW4 = 0x%08x\n", fw4);
	printf("        FW5 = 0x%08x\n", fw5);
	printf("        FW6 = 0x%08x\n", fw6);
	printf("        FW7 = 0x%08x\n", fw7);
	if (IS_CHERRYVIEW(devid)) {
		printf("        FW8 = 0x%08x\n", fw8);
		printf("        FW9 = 0x%08x\n", fw9);
	}
	printf("       HOWM = 0x%08x\n", howm);
	printf("      HOWM1 = 0x%08x\n", howm1);
	printf("       DDL1 = 0x%08x\n", ddl1);
	printf("       DDL2 = 0x%08x\n", ddl2);
	if (IS_CHERRYVIEW(devid))
		printf("       DDL3 = 0x%08x\n", ddl3);
	printf("     DSPARB = 0x%08x\n", dsparb);
	printf("    DSPARB2 = 0x%08x\n", dsparb2);
	if (IS_CHERRYVIEW(devid))
		printf("    DSPARB3 = 0x%08x\n", dsparb3);
	printf("FW_BLC_SELF = 0x%08x\n", fw_blc_self);
	printf("     MI_ARB = 0x%08x\n", mi_arb);
	printf("       CBR1 = 0x%08x\n", cbr1);
	if (IS_CHERRYVIEW(devid)) {
		printf("  DSP_SS_PM = 0x%08x\n", dsp_ss_pm);
		printf(" DDR_SETUP2 = 0x%08x\n", ddr_setup2);
	}

	wms[PRI_A].valid = true;
	wms[PRI_B].valid = true;
	wms[CUR_A].valid = true;
	wms[CUR_B].valid = true;
	wms[SPR_A].valid = true;
	wms[SPR_B].valid = true;
	wms[SPR_C].valid = true;
	wms[SPR_D].valid = true;
	wms[PRI_SR].valid = true;
	wms[CUR_SR].valid = true;

	if (IS_CHERRYVIEW(devid)) {
		wms[PRI_C].valid = true;
		wms[CUR_C].valid = true;
		wms[SPR_E].valid = true;
		wms[SPR_F].valid = true;
	}

	wms[PRI_A].fifo = REG_DECODE2(dsparb, 0, 8, dsparb2, 0, 1) - 0;
	wms[SPR_A].fifo = REG_DECODE2(dsparb, 8, 8, dsparb2, 4, 1) - wms[PRI_A].fifo;
	wms[SPR_B].fifo = 512 - 1 - wms[SPR_A].fifo - wms[PRI_A].fifo;
	wms[CUR_A].fifo = 0x3f;

	wms[PRI_B].fifo = REG_DECODE2(dsparb, 16, 8, dsparb2, 8, 1) - 0;
	wms[SPR_C].fifo = REG_DECODE2(dsparb, 24, 8, dsparb2, 12, 1) - wms[PRI_B].fifo;
	wms[SPR_D].fifo = 512 - 1 - wms[SPR_C].fifo - wms[PRI_B].fifo;
	wms[CUR_B].fifo = 0x3f;

	if (IS_CHERRYVIEW(devid)) {
		wms[PRI_C].fifo = REG_DECODE2(dsparb3, 0, 8, dsparb2, 16, 1) - 0;
		wms[SPR_E].fifo = REG_DECODE2(dsparb3, 8, 8, dsparb2, 20, 1) - wms[PRI_C].fifo;
		wms[SPR_F].fifo = 512 - 1 - wms[SPR_E].fifo - wms[PRI_C].fifo;
		wms[CUR_C].fifo = 0x3f;
	}

	wms[PRI_SR].fifo = 512 * num_pipes - 1;
	wms[CUR_SR].fifo = 0x3f;

	wms[PRI_HPLL_SR].fifo = 512 * num_pipes - 1;
	wms[CUR_HPLL_SR].fifo = 0x3f;

	wms[PRI_A].wm  = REG_DECODE2(fw1, 0, 8, howm, 0, 1);
	wms[PRI_B].wm  = REG_DECODE2(fw1, 8, 8, howm, 12, 1);
	wms[CUR_B].wm  = REG_DECODE1(fw1, 16, 6);
	wms[PRI_SR].wm = REG_DECODE2(fw1, 23, 9, howm, 24, 2);

	wms[SPR_A].wm  = REG_DECODE2(fw2, 0, 8, howm, 4, 1);
	wms[CUR_A].wm  = REG_DECODE1(fw2, 8, 6);
	wms[SPR_B].wm  = REG_DECODE2(fw2, 16, 8, howm, 8, 1);

	wms[CUR_SR].wm   = REG_DECODE1(fw3, 24, 6);

	wms[SPR_A].wm1 = REG_DECODE2(fw4, 0, 8, howm1, 4, 1);
	wms[CUR_A].wm1 = REG_DECODE1(fw4, 8, 6);
	wms[SPR_B].wm1 = REG_DECODE2(fw4, 16, 8, howm1, 8, 1);

	wms[CUR_SR].wm1 = REG_DECODE1(fw5, 0, 6);
	wms[CUR_B].wm1  = REG_DECODE1(fw5, 8, 6);
	wms[PRI_A].wm1  = REG_DECODE2(fw5, 16, 8, howm1, 0, 1);
	wms[PRI_B].wm1  = REG_DECODE2(fw5, 24, 8, howm1, 12, 1);

	wms[PRI_SR].wm1 = REG_DECODE2(fw6, 0, 9, howm1, 24, 2);

	wms[SPR_C].wm  = REG_DECODE2(fw7, 0, 8, howm, 16, 1);
	wms[SPR_C].wm1 = REG_DECODE2(fw7, 8, 8, howm1, 16, 1);
	wms[SPR_D].wm  = REG_DECODE2(fw7, 16, 8, howm, 20, 1);
	wms[SPR_D].wm1 = REG_DECODE2(fw7, 24, 8, howm1, 20, 1);

	if (IS_CHERRYVIEW(devid)) {
		wms[SPR_E].wm  = REG_DECODE2(fw8, 0, 8, howm, 22, 1);
		wms[SPR_E].wm1 = REG_DECODE2(fw8, 8, 8, howm1, 22, 1);
		wms[SPR_F].wm  = REG_DECODE2(fw8, 16, 8, howm, 23, 1);
		wms[SPR_F].wm1 = REG_DECODE2(fw8, 24, 8, howm1, 23, 1);

		wms[CUR_C].wm  = REG_DECODE1(fw9, 0, 6);
		wms[CUR_C].wm1 = REG_DECODE1(fw9, 8, 6);
		wms[PRI_C].wm  = REG_DECODE2(fw9, 16, 8, howm, 21, 1);
		wms[PRI_C].wm1 = REG_DECODE2(fw9, 24, 8, howm1, 21, 1);
	}

	wms[PRI_A].dl = REG_DECODE1(ddl1, 0, 7);
	wms[SPR_A].dl = REG_DECODE1(ddl1, 8, 7);
	wms[SPR_B].dl = REG_DECODE1(ddl1, 16, 7);
	wms[CUR_A].dl = REG_DECODE1(ddl1, 24, 7);

	wms[PRI_A].dl_prec = REG_DECODE1(ddl1, 7, 1);
	wms[SPR_A].dl_prec = REG_DECODE1(ddl1, 15, 1);
	wms[SPR_B].dl_prec = REG_DECODE1(ddl1, 23, 1);
	wms[CUR_A].dl_prec = REG_DECODE1(ddl1, 31, 1);

	wms[PRI_B].dl = REG_DECODE1(ddl2, 0, 7);
	wms[SPR_C].dl = REG_DECODE1(ddl2, 8, 7);
	wms[SPR_D].dl = REG_DECODE1(ddl2, 16, 7);
	wms[CUR_B].dl = REG_DECODE1(ddl2, 24, 7);

	wms[PRI_B].dl_prec = REG_DECODE1(ddl2, 7, 1);
	wms[SPR_C].dl_prec = REG_DECODE1(ddl2, 15, 1);
	wms[SPR_D].dl_prec = REG_DECODE1(ddl2, 23, 1);
	wms[CUR_B].dl_prec = REG_DECODE1(ddl2, 31, 1);

	if (IS_CHERRYVIEW(devid)) {
		wms[PRI_C].dl = REG_DECODE1(ddl3, 0, 7);
		wms[SPR_E].dl = REG_DECODE1(ddl3, 8, 7);
		wms[SPR_F].dl = REG_DECODE1(ddl3, 16, 7);
		wms[CUR_C].dl = REG_DECODE1(ddl3, 24, 7);

		wms[PRI_C].dl_prec = REG_DECODE1(ddl3, 7, 1);
		wms[SPR_E].dl_prec = REG_DECODE1(ddl3, 15, 1);
		wms[SPR_F].dl_prec = REG_DECODE1(ddl3, 23, 1);
		wms[CUR_C].dl_prec = REG_DECODE1(ddl3, 31, 1);
	}

	for (i = 0; i < ARRAY_SIZE(wms); i++) {
		if (!wms[i].valid)
			continue;
		printf("%s: WM = %d, WM1 = %d, DDL = %d (prec=%d), FIFO = %d\n",
		       plane_name[i], wms[i].wm, wms[i].wm1, wms[i].dl, wms[i].dl_prec, wms[i].fifo);
	}

	printf("CxSR = %s\n",
	       endis(REG_DECODE1(fw_blc_self, 15, 1)));
	printf("Trickle feed = %s\n",
	       endis(!REG_DECODE1(mi_arb, 2, 1)));
	printf("PND deadline = %s\n",
	       endis(!REG_DECODE1(cbr1, 31, 1)));

	if (IS_CHERRYVIEW(devid)) {
		printf("PM5 = %s\n",
		       endis(REG_DECODE1(dsp_ss_pm, 6, 1)));
		printf("PM5 state = %s\n",
		       endis(REG_DECODE1(dsp_ss_pm, 22, 1)));
		printf("DDR force high frequency = %s\n",
		       endis(REG_DECODE1(ddr_setup2, 0, 1)));
		printf("DDR force low frequency = %s\n",
		       endis(REG_DECODE1(ddr_setup2, 1, 1)));
	}
}

static void g4x_wm_dump(void)
{
	int i;
	uint32_t dspacntr, dspbcntr;
	uint32_t dsparb;
	uint32_t fw1, fw2, fw3;
	uint32_t mi_display_power_down;
	uint32_t mi_arb_state;
	struct gmch_wm wms[MAX_PLANE] = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	dspacntr = read_reg(0x70180);
	dspbcntr = read_reg(0x71180);
	dsparb = read_reg(0x70030);
	fw1 = read_reg(0x70034);
	fw2 = read_reg(0x70038);
	fw3 = read_reg(0x7003c);
	mi_display_power_down = read_reg(0x20e0);
	mi_arb_state = read_reg(0x20e4);

	intel_register_access_fini();

	printf("             DSPACNTR = 0x%08x\n", dspacntr);
	printf("             DSPBCNTR = 0x%08x\n", dspbcntr);
	printf("                  FW1 = 0x%08x\n", fw1);
	printf("                  FW2 = 0x%08x\n", fw2);
	printf("                  FW3 = 0x%08x\n", fw3);
	printf("               DSPARB = 0x%08x\n", dsparb);
	printf("MI_DISPLAY_POWER_DOWN = 0x%08x\n", mi_display_power_down);
	printf("         MI_ARB_STATE = 0x%08x\n", mi_arb_state);

	wms[PRI_A].valid = true;
	wms[PRI_B].valid = true;
	wms[CUR_A].valid = true;
	wms[CUR_B].valid = true;
	wms[SPR_A].valid = true;
	wms[SPR_B].valid = true;
	wms[PRI_SR].valid = true;
	wms[CUR_SR].valid = true;
	wms[PRI_HPLL_SR].valid = true;
	wms[CUR_HPLL_SR].valid = true;

	wms[PRI_A].fifo = REG_DECODE1(dsparb, 0, 7) - 0;
	wms[PRI_B].fifo = REG_DECODE1(dsparb, 7, 7) - wms[PRI_A].fifo;

	wms[PRI_A].wm  = REG_DECODE1(fw1, 0, 7);
	wms[PRI_B].wm  = REG_DECODE1(fw1, 8, 7);
	wms[CUR_B].wm  = REG_DECODE1(fw1, 16, 6);
	wms[PRI_SR].wm = REG_DECODE1(fw1, 23, 9);

	wms[PRI_SR].fbc      = REG_DECODE1(fw2, 0, 8);
	wms[PRI_HPLL_SR].fbc = REG_DECODE1(fw2, 8, 6);

	wms[SPR_B].wm = REG_DECODE1(fw2, 16, 7);
	wms[CUR_A].wm = REG_DECODE1(fw2, 8, 6);
	wms[SPR_A].wm = REG_DECODE1(fw2, 0, 7);

	wms[CUR_SR].wm      = REG_DECODE1(fw3, 24, 6);
	wms[CUR_HPLL_SR].wm = REG_DECODE1(fw3, 16, 6);
	wms[PRI_HPLL_SR].wm = REG_DECODE1(fw3, 0, 9);

	for (i = 0; i < ARRAY_SIZE(wms); i++) {
		if (!wms[i].valid)
			continue;
		printf("%s: WM = %d, FBC = %d, FIFO = %d\n",
		       plane_name[i], wms[i].wm, wms[i].fbc, wms[i].fifo);
	}
	printf("CxSR = %s\n",
	       endis(REG_DECODE1(mi_display_power_down, 15, 1)));
	printf("HPLL SR = %s\n",
	       endis(REG_DECODE1(fw3, 31, 1)));
	printf("FBC SR = %s\n",
	       endis(REG_DECODE1(fw2, 31, 1)));
	printf("Display A trickle feed = %s\n",
	       endis(!REG_DECODE1(dspacntr, 14, 1)));
	printf("Display B trickle feed = %s\n",
	       endis(!REG_DECODE1(dspbcntr, 14, 1)));
	printf("Display A uses sprite data buffer = %s\n",
	       endis(!REG_DECODE1(dspacntr, 13, 1)));
	printf("Display B uses sprite data buffer = %s\n",
	       endis(!REG_DECODE1(dspbcntr, 13, 1)));
	printf("Primary display = %c\n",
	       REG_DECODE1(mi_arb_state, 0, 1) ? 'B' : 'A');
}

static void gen4_wm_dump(void)
{
	int i;
	int totalsize = IS_CRESTLINE(devid) ? 128 : 96;
	uint32_t dsparb;
	uint32_t fw1, fw2, fw3;
	uint32_t mi_display_power_down;
	uint32_t mi_arb_state;
	struct gmch_wm wms[MAX_PLANE] = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	dsparb = read_reg(0x70030);
	fw1 = read_reg(0x70034);
	fw2 = read_reg(0x70038);
	fw3 = read_reg(0x7003c);
	mi_display_power_down = read_reg(0x20e0);
	mi_arb_state = read_reg(0x20e4);

	intel_register_access_fini();

	printf("                  FW1 = 0x%08x\n", fw1);
	printf("                  FW2 = 0x%08x\n", fw2);
	printf("                  FW3 = 0x%08x\n", fw3);
	printf("               DSPARB = 0x%08x\n", dsparb);
	printf("MI_DISPLAY_POWER_DOWN = 0x%08x\n", mi_display_power_down);
	printf("         MI_ARB_STATE = 0x%08x\n", mi_arb_state);

	wms[PRI_A].valid = true;
	wms[PRI_B].valid = true;
	wms[PRI_C].valid = true;
	wms[CUR_A].valid = true;
	wms[CUR_B].valid = true;
	wms[PRI_SR].valid = true;
	wms[CUR_SR].valid = true;
	wms[PRI_HPLL_SR].valid = true;
	wms[CUR_HPLL_SR].valid = true;

	wms[PRI_A].fifo = REG_DECODE1(dsparb, 0, 7) - 0;
	wms[PRI_B].fifo = REG_DECODE1(dsparb, 7, 7) - wms[PRI_A].fifo;
	wms[PRI_C].fifo = totalsize - wms[PRI_B].fifo - wms[PRI_A].fifo - 1;

	wms[PRI_A].wm  = REG_DECODE1(fw1, 0, 7);
	wms[PRI_B].wm  = REG_DECODE1(fw1, 8, 7);
	wms[CUR_B].wm  = REG_DECODE1(fw1, 16, 6);
	wms[PRI_SR].wm = REG_DECODE1(fw1, 23, 9);

	wms[CUR_A].wm = REG_DECODE1(fw2, 8, 6);
	wms[PRI_C].wm = REG_DECODE1(fw2, 0, 7);

	wms[CUR_SR].wm      = REG_DECODE1(fw3, 24, 6);
	wms[CUR_HPLL_SR].wm = REG_DECODE1(fw3, 16, 6);
	wms[PRI_HPLL_SR].wm = REG_DECODE1(fw3, 0, 9);

	for (i = 0; i < ARRAY_SIZE(wms); i++) {
		if (!wms[i].valid)
			continue;
		printf("%s: WM = %d, FIFO = %d\n",
		       plane_name[i], wms[i].wm, wms[i].fifo);
	}
	printf("CxSR = %s\n",
	       endis(REG_DECODE1(mi_display_power_down, 15, 1)));
	printf("HPLL SR enable = %s\n",
	       endis(REG_DECODE1(fw3, 31, 1)));
	printf("Trickle feed = %s\n",
	       endis(!REG_DECODE1(mi_arb_state, 2, 1)));
	printf("Primary display = %c\n",
	       REG_DECODE1(mi_arb_state, 0, 1) + 'A');
}

static void pnv_wm_dump(void)
{
	int i;
	int totalsize = 96; /* FIXME? */
	uint32_t dsparb;
	uint32_t fw1, fw2, fw3;
	uint32_t mi_display_power_down;
	uint32_t mi_arb_state;
	uint32_t cbr;
	struct gmch_wm wms[MAX_PLANE] = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	dsparb = read_reg(0x70030);
	fw1 = read_reg(0x70034);
	fw2 = read_reg(0x70038);
	fw3 = read_reg(0x7003c);
	cbr = read_reg(0x70400);
	mi_display_power_down = read_reg(0x20e0);
	mi_arb_state = read_reg(0x20e4);

	intel_register_access_fini();

	printf("               DSPARB = 0x%08x\n", dsparb);
	printf("                  FW1 = 0x%08x\n", fw1);
	printf("                  FW2 = 0x%08x\n", fw2);
	printf("                  FW3 = 0x%08x\n", fw3);
	printf("                  CBR = 0x%08x\n", cbr);
	printf("MI_DISPLAY_POWER_DOWN = 0x%08x\n", mi_display_power_down);
	printf("         MI_ARB_STATE = 0x%08x\n", mi_arb_state);

	wms[PRI_A].valid = true;
	wms[PRI_B].valid = true;
	wms[PRI_C].valid = true;
	wms[CUR_A].valid = true;
	wms[CUR_B].valid = true;
	wms[PRI_SR].valid = true;
	wms[CUR_SR].valid = true;
	wms[PRI_HPLL_SR].valid = true;
	wms[CUR_HPLL_SR].valid = true;

	wms[PRI_A].fifo = REG_DECODE1(dsparb, 0, 7) - 0;
	wms[PRI_B].fifo = REG_DECODE1(dsparb, 7, 7) - wms[PRI_A].fifo;
	wms[PRI_C].fifo = totalsize - wms[PRI_B].fifo - wms[PRI_A].fifo - 1;

	wms[PRI_A].wm  = REG_DECODE1(fw1, 0, 7);
	wms[PRI_B].wm  = REG_DECODE1(fw1, 8, 7);
	wms[CUR_B].wm  = REG_DECODE1(fw1, 16, 6);
	wms[PRI_SR].wm = REG_DECODE1(fw1, 23, 9);

	wms[CUR_A].wm = REG_DECODE1(fw2, 8, 6);
	wms[PRI_C].wm = REG_DECODE1(fw2, 0, 7);

	switch ((REG_DECODE1(cbr, 30, 1) << 1) | REG_DECODE1(cbr, 25, 1)) {
	case 3:
	case 2:
		wms[PRI_SR].fifo = 8 * 1024 / 64;
		break;
	case 1:
		wms[PRI_SR].fifo = 16 * 1024 / 64;
		break;
	case 0:
		wms[PRI_SR].fifo = 32 * 1024 / 64;
		break;
	}

	wms[CUR_SR].wm      = REG_DECODE1(fw3, 24, 6);
	wms[CUR_HPLL_SR].wm = REG_DECODE1(fw3, 16, 6);
	wms[PRI_HPLL_SR].wm = REG_DECODE1(fw3, 0, 9);

	for (i = 0; i < ARRAY_SIZE(wms); i++) {
		if (!wms[i].valid)
			continue;
		printf("%s: WM = %d, FIFO = %d\n",
		       plane_name[i], wms[i].wm, wms[i].fifo);
	}
	printf("CxSR enable = %s\n",
	       endis(REG_DECODE1(fw3, 30, 1)));
	printf("HPLL SR enable = %s\n",
	       endis(REG_DECODE1(fw3, 31, 1)));
	printf("Trickle feed = %s\n",
	       endis(!REG_DECODE1(mi_arb_state, 2, 1)));
	printf("Primary display = %c\n",
	       REG_DECODE1(mi_arb_state, 0, 1) + 'A');
	printf("Display plane A throttling = %s\n",
	       endis(!REG_DECODE1(cbr, 0, 1)));
	printf("Display plane B throttling = %s\n",
	       endis(!REG_DECODE1(cbr, 1, 1)));
}

static void gen3_wm_dump(void)
{
	int i;
	int totalsize = IS_945GM(devid) ? 128 : 96; /* FIXME? */
	uint32_t dsparb;
	uint32_t instpm;
	uint64_t fw_blc;
	uint32_t fw_blc_self;
	uint32_t mi_arb_state;
	struct gmch_wm wms[MAX_PLANE] = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	dsparb = read_reg(0x70030);
	instpm = read_reg(0x20c0);
	fw_blc = read_reg(0x20d8) | ((uint64_t)read_reg(0x20dc) << 32);
	fw_blc_self = read_reg(0x20e0);
	mi_arb_state = read_reg(0x20e4);

	intel_register_access_fini();

	printf("      DSPARB = 0x%08x\n", dsparb);
	printf("      FW_BLC = 0x%016" PRIx64 "\n", fw_blc);
	printf(" FW_BLC_SELF = 0x%08x\n", fw_blc_self);
	printf("MI_ARB_STATE = 0x%08x\n", mi_arb_state);

	wms[PRI_A].valid = true;
	wms[PRI_B].valid = true;
	wms[PRI_C].valid = true;
	wms[PRI_SR].valid = true;

	wms[PRI_SR].wm = REG_DECODE1(fw_blc_self, 0, 8);

	wms[PRI_C].burst = (REG_DECODE1(fw_blc, 40, 2) + 1) * 4;
	wms[PRI_C].wm    = REG_DECODE1(fw_blc, 32, 8);

	wms[PRI_B].burst = (REG_DECODE1(fw_blc, 24, 2) + 1) * 4;
	wms[PRI_B].wm    = REG_DECODE1(fw_blc, 16, 8);

	wms[PRI_A].burst = (REG_DECODE1(fw_blc, 8, 2) + 1) * 4;
	wms[PRI_A].wm    = REG_DECODE1(fw_blc, 0, 8);

	wms[PRI_A].fifo = REG_DECODE1(dsparb, 0, 7) - 0;
	wms[PRI_B].fifo = REG_DECODE1(dsparb, 7, 7) - wms[PRI_A].fifo;
	wms[PRI_C].fifo = totalsize - wms[PRI_B].fifo - wms[PRI_A].fifo - 1;

	for (i = 0; i < ARRAY_SIZE(wms); i++) {
		if (!wms[i].valid)
			continue;
		printf("%s: WM = %d, FIFO = %d, burst = %d\n",
		       plane_name[i], wms[i].wm, wms[i].fifo, wms[i].burst);
	}
	/* FIXME G33 too perhaps? */
	if (devid == PCI_CHIP_I945_G || devid == PCI_CHIP_I945_GM ||
	    devid == PCI_CHIP_I945_GME) {
		printf("CxSR = %s\n",
		       endis(REG_DECODE1(fw_blc_self, 15, 1)));
	} else if (devid == PCI_CHIP_I915_GM) {
		printf("CxSR = %s\n",
		       endis(REG_DECODE1(instpm, 12, 1)));
	}
	printf("Trickle feed = %s\n",
	       endis(!REG_DECODE1(mi_arb_state, 2, 1)));
	printf("Primary display = %c\n",
	       REG_DECODE1(mi_arb_state, 0, 1) + 'A');
	printf("Display plane capability = %d planes\n",
	       3 - REG_DECODE1(mi_arb_state, 12, 2));
}

static void gen2_wm_dump(void)
{
	int i;
	int totalsize;
	uint32_t dsparb;
	uint32_t mem_mode;
	uint64_t fw_blc;
	uint32_t fw_blc_self;
	uint32_t mi_state;
	struct gmch_wm wms[MAX_PLANE] = {};

	intel_register_access_init(intel_get_pci_device(), 0);

	dsparb = read_reg(0x70030);
	mem_mode = read_reg(0x20cc);
	fw_blc = read_reg(0x20d8) | ((uint64_t)read_reg(0x20dc) << 32);
	fw_blc_self = read_reg(0x20e0);
	mi_state = read_reg(0x20e4);

	intel_register_access_fini();

	printf("     DSPARB = 0x%08x\n", dsparb);
	printf("   MEM_MODE = 0x%08x\n", mem_mode);
	printf("     FW_BLC = 0x%016" PRIx64 "\n", fw_blc);
	printf("FW_BLC_SELF = 0x%08x\n", fw_blc_self);
	printf("   MI_STATE = 0x%08x\n", mi_state);

	wms[PRI_C].burst = (REG_DECODE1(fw_blc, 40, 2) + 1) * 4;
	wms[PRI_C].wm    = REG_DECODE1(fw_blc, 32, 8);

	wms[PRI_B].burst = (REG_DECODE1(fw_blc, 24, 2) + 1) * 4;
	wms[PRI_B].wm    = REG_DECODE1(fw_blc, 16, 8);

	wms[PRI_A].burst = (REG_DECODE1(fw_blc, 8, 2) + 1) * 4;
	wms[PRI_A].wm    = REG_DECODE1(fw_blc, 0, 8);

	if (devid == PCI_CHIP_845_G || devid == PCI_CHIP_I865_G) {
		wms[PRI_A].valid = true;
		wms[PRI_C].valid = true;

		totalsize = 96; /* FIXME? */
		wms[PRI_A].fifo = REG_DECODE1(dsparb, 0, 7) - 0;
		wms[PRI_C].fifo = totalsize - wms[PRI_A].fifo - 1;
	} else {
		wms[PRI_A].valid = true;
		wms[PRI_B].valid = true;
		wms[PRI_C].valid = true;

		if (devid == PCI_CHIP_I830_M)
			totalsize = 288;
		else
			totalsize = 256;
		totalsize = (devid == PCI_CHIP_I855_GM) ? 256 : 288;
		wms[PRI_A].fifo = REG_DECODE1(dsparb, 0, 9) - 0;
		wms[PRI_B].fifo = REG_DECODE1(dsparb, 9, 9) - wms[PRI_A].fifo;
		wms[PRI_C].fifo = totalsize - wms[PRI_B].fifo - wms[PRI_A].fifo - 1;
	}

	for (i = 0; i < ARRAY_SIZE(wms); i++) {
		if (!wms[i].valid)
			continue;
		printf("%s: WM = %d, FIFO = %d, burst = %d\n",
		       plane_name[i], wms[i].wm, wms[i].fifo, wms[i].burst);
	}
	if (devid == PCI_CHIP_I855_GM || devid == PCI_CHIP_I854_G) {
		printf("CxSR = %s (%d)\n",
		       endis(REG_DECODE1(mi_state, 3, 2)),
		       REG_DECODE1(mi_state, 3, 2));
		printf("Trickle feed = %s\n",
		       endis(!REG_DECODE1(mem_mode, 2, 1)));
		printf("Display round robin = %s\n",
		       endis(REG_DECODE1(mem_mode, 14, 1)));
		printf("Primary display = %c\n",
		       REG_DECODE1(mem_mode, 15, 1) + 'A');
	} else {
		printf("Display A trickle feed = %s\n",
		       endis(!REG_DECODE1(mem_mode, 2, 1)));
		printf("Display B trickle feed = %s\n",
		       endis(!REG_DECODE1(mem_mode, 3, 1)));
		printf("Water mark fix = %s\n",
		       endis(!REG_DECODE1(mem_mode, 14, 1)));
	}
}

int main(int argc, char *argv[])
{
	devid = intel_get_pci_device()->device_id;

	if (HAS_PCH_SPLIT(devid)) {
		ilk_wm_dump();
	} else if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid)) {
		display_base = 0x180000;
		vlv_wm_dump();
	} else if (IS_G4X(devid)) {
		g4x_wm_dump();
	} else if (IS_GEN4(devid)) {
		gen4_wm_dump();
	} else if (IS_IGD(devid)) {
		pnv_wm_dump();
	} else if (IS_GEN3(devid)) {
		gen3_wm_dump();
	} else if (IS_GEN2(devid)) {
		gen2_wm_dump();
	} else {
		printf("unknown chip 0x%x\n", devid);
		return 1;
	}

	return 0;
}
