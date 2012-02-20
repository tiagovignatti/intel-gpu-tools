/*
 * Copyright © 2007,2009 Intel Corporation
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
 *
 */

#include <assert.h>
#include "instdone.h"

#include "intel_chipset.h"
#include "intel_reg.h"

struct instdone_bit instdone_bits[MAX_INSTDONE_BITS];
int num_instdone_bits = 0;

static void
add_instdone_bit(uint32_t reg, uint32_t bit, const char *name)
{
	instdone_bits[num_instdone_bits].reg = reg;
	instdone_bits[num_instdone_bits].bit = bit;
	instdone_bits[num_instdone_bits].name = name;
	num_instdone_bits++;
}

static void
gen3_instdone_bit(uint32_t bit, const char *name)
{
	add_instdone_bit(INST_DONE, bit, name);
}

static void
gen4_instdone_bit(uint32_t bit, const char *name)
{
	add_instdone_bit(INST_DONE_I965, bit, name);
}

static void
gen4_instdone1_bit(uint32_t bit, const char *name)
{
	add_instdone_bit(INST_DONE_1, bit, name);
}

static void
gen6_instdone1_bit(uint32_t bit, const char *name)
{
	add_instdone_bit(GEN6_INSTDONE_1, bit, name);
}

static void
gen6_instdone2_bit(uint32_t bit, const char *name)
{
	add_instdone_bit(GEN6_INSTDONE_2, bit, name);
}

static void
init_g965_instdone1(void)
{
	gen4_instdone1_bit(I965_GW_CS_DONE_CR, "GW CS CR");
	gen4_instdone1_bit(I965_SVSM_CS_DONE_CR, "SVSM CS CR");
	gen4_instdone1_bit(I965_SVDW_CS_DONE_CR, "SVDW CS CR");
	gen4_instdone1_bit(I965_SVDR_CS_DONE_CR, "SVDR CS CR");
	gen4_instdone1_bit(I965_SVRW_CS_DONE_CR, "SVRW CS CR");
	gen4_instdone1_bit(I965_SVRR_CS_DONE_CR, "SVRR CS CR");
	gen4_instdone1_bit(I965_SVTW_CS_DONE_CR, "SVTW CS CR");
	gen4_instdone1_bit(I965_MASM_CS_DONE_CR, "MASM CS CR");
	gen4_instdone1_bit(I965_MASF_CS_DONE_CR, "MASF CS CR");
	gen4_instdone1_bit(I965_MAW_CS_DONE_CR, "MAW CS CR");
	gen4_instdone1_bit(I965_EM1_CS_DONE_CR, "EM1 CS CR");
	gen4_instdone1_bit(I965_EM0_CS_DONE_CR, "EM0 CS CR");
	gen4_instdone1_bit(I965_UC1_CS_DONE, "UC1 CS");
	gen4_instdone1_bit(I965_UC0_CS_DONE, "UC0 CS");
	gen4_instdone1_bit(I965_URB_CS_DONE, "URB CS");
	gen4_instdone1_bit(I965_ISC_CS_DONE, "ISC CS");
	gen4_instdone1_bit(I965_CL_CS_DONE, "CL CS");
	gen4_instdone1_bit(I965_GS_CS_DONE, "GS CS");
	gen4_instdone1_bit(I965_VS0_CS_DONE, "VS0 CS");
	gen4_instdone1_bit(I965_VF_CS_DONE, "VF CS");
}

static void
init_g4x_instdone1(void)
{
	gen4_instdone1_bit(G4X_BCS_DONE, "BCS");
	gen4_instdone1_bit(G4X_CS_DONE, "CS");
	gen4_instdone1_bit(G4X_MASF_DONE, "MASF");
	gen4_instdone1_bit(G4X_SVDW_DONE, "SVDW");
	gen4_instdone1_bit(G4X_SVDR_DONE, "SVDR");
	gen4_instdone1_bit(G4X_SVRW_DONE, "SVRW");
	gen4_instdone1_bit(G4X_SVRR_DONE, "SVRR");
	gen4_instdone1_bit(G4X_ISC_DONE, "ISC");
	gen4_instdone1_bit(G4X_MT_DONE, "MT");
	gen4_instdone1_bit(G4X_RC_DONE, "RC");
	gen4_instdone1_bit(G4X_DAP_DONE, "DAP");
	gen4_instdone1_bit(G4X_MAWB_DONE, "MAWB");
	gen4_instdone1_bit(G4X_MT_IDLE, "MT idle");
	//gen4_instdone1_bit(G4X_GBLT_BUSY, "GBLT");
	gen4_instdone1_bit(G4X_SVSM_DONE, "SVSM");
	gen4_instdone1_bit(G4X_MASM_DONE, "MASM");
	gen4_instdone1_bit(G4X_QC_DONE, "QC");
	gen4_instdone1_bit(G4X_FL_DONE, "FL");
	gen4_instdone1_bit(G4X_SC_DONE, "SC");
	gen4_instdone1_bit(G4X_DM_DONE, "DM");
	gen4_instdone1_bit(G4X_FT_DONE, "FT");
	gen4_instdone1_bit(G4X_DG_DONE, "DG");
	gen4_instdone1_bit(G4X_SI_DONE, "SI");
	gen4_instdone1_bit(G4X_SO_DONE, "SO");
	gen4_instdone1_bit(G4X_PL_DONE, "PL");
	gen4_instdone1_bit(G4X_WIZ_DONE, "WIZ");
	gen4_instdone1_bit(G4X_URB_DONE, "URB");
	gen4_instdone1_bit(G4X_SF_DONE, "SF");
	gen4_instdone1_bit(G4X_CL_DONE, "CL");
	gen4_instdone1_bit(G4X_GS_DONE, "GS");
	gen4_instdone1_bit(G4X_VS0_DONE, "VS0");
	gen4_instdone1_bit(G4X_VF_DONE, "VF");
}

static void
init_gen7_instdone(void)
{
	gen6_instdone1_bit(1 << 19, "GAM");
	gen6_instdone1_bit(1 << 18, "GAFM");
	gen6_instdone1_bit(1 << 17, "TSG");
	gen6_instdone1_bit(1 << 16, "VFE");
	gen6_instdone1_bit(1 << 15, "GAFS");
	gen6_instdone1_bit(1 << 14, "SVG");
	gen6_instdone1_bit(1 << 13, "URBM");
	gen6_instdone1_bit(1 << 12, "TDG");
	gen6_instdone1_bit(1 << 9, "SF");
	gen6_instdone1_bit(1 << 8, "CL");
	gen6_instdone1_bit(1 << 7, "SOL");
	gen6_instdone1_bit(1 << 6, "GS");
	gen6_instdone1_bit(1 << 5, "DS");
	gen6_instdone1_bit(1 << 4, "TE");
	gen6_instdone1_bit(1 << 3, "HS");
	gen6_instdone1_bit(1 << 2, "VS");
	gen6_instdone1_bit(1 << 1, "VF");
}

void
init_instdone_definitions(uint32_t devid)
{
	if (IS_GEN7(devid)) {
		init_gen7_instdone();
	} else if (IS_GEN6(devid)) {
		/* Now called INSTDONE_1 in the docs. */
		gen6_instdone1_bit(GEN6_MA_3_DONE, "Message Arbiter 3");
		gen6_instdone1_bit(GEN6_EU_32_DONE, "EU 32");
		gen6_instdone1_bit(GEN6_EU_31_DONE, "EU 31");
		gen6_instdone1_bit(GEN6_EU_30_DONE, "EU 30");
		gen6_instdone1_bit(GEN6_MA_3_DONE, "Message Arbiter 2");
		gen6_instdone1_bit(GEN6_EU_22_DONE, "EU 22");
		gen6_instdone1_bit(GEN6_EU_21_DONE, "EU 21");
		gen6_instdone1_bit(GEN6_EU_20_DONE, "EU 20");
		gen6_instdone1_bit(GEN6_MA_3_DONE, "Message Arbiter 1");
		gen6_instdone1_bit(GEN6_EU_12_DONE, "EU 12");
		gen6_instdone1_bit(GEN6_EU_11_DONE, "EU 11");
		gen6_instdone1_bit(GEN6_EU_10_DONE, "EU 10");
		gen6_instdone1_bit(GEN6_MA_3_DONE, "Message Arbiter 0");
		gen6_instdone1_bit(GEN6_EU_02_DONE, "EU 02");
		gen6_instdone1_bit(GEN6_EU_01_DONE, "EU 01");
		gen6_instdone1_bit(GEN6_EU_00_DONE, "EU 00");

		gen6_instdone1_bit(GEN6_IC_3_DONE, "IC 3");
		gen6_instdone1_bit(GEN6_IC_2_DONE, "IC 2");
		gen6_instdone1_bit(GEN6_IC_1_DONE, "IC 1");
		gen6_instdone1_bit(GEN6_IC_0_DONE, "IC 0");
		gen6_instdone1_bit(GEN6_ISC_10_DONE, "ISC 1/0");
		gen6_instdone1_bit(GEN6_ISC_32_DONE, "ISC 3/2");

		gen6_instdone1_bit(GEN6_VSC_DONE, "VSC");
		gen6_instdone1_bit(GEN6_IEF_DONE, "IEF");
		gen6_instdone1_bit(GEN6_VFE_DONE, "VFE");
		gen6_instdone1_bit(GEN6_TD_DONE, "TD");
		gen6_instdone1_bit(GEN6_TS_DONE, "TS");
		gen6_instdone1_bit(GEN6_GW_DONE, "GW");
		gen6_instdone1_bit(GEN6_HIZ_DONE, "HIZ");
		gen6_instdone1_bit(GEN6_AVS_DONE, "AVS");

		/* Now called INSTDONE_2 in the docs. */
		gen6_instdone2_bit(GEN6_GAM_DONE, "GAM");
		gen6_instdone2_bit(GEN6_CS_DONE, "CS");
		gen6_instdone2_bit(GEN6_WMBE_DONE, "WMBE");
		gen6_instdone2_bit(GEN6_SVRW_DONE, "SVRW");
		gen6_instdone2_bit(GEN6_RCC_DONE, "RCC");
		gen6_instdone2_bit(GEN6_SVG_DONE, "SVG");
		gen6_instdone2_bit(GEN6_ISC_DONE, "ISC");
		gen6_instdone2_bit(GEN6_MT_DONE, "MT");
		gen6_instdone2_bit(GEN6_RCPFE_DONE, "RCPFE");
		gen6_instdone2_bit(GEN6_RCPBE_DONE, "RCPBE");
		gen6_instdone2_bit(GEN6_VDI_DONE, "VDI");
		gen6_instdone2_bit(GEN6_RCZ_DONE, "RCZ");
		gen6_instdone2_bit(GEN6_DAP_DONE, "DAP");
		gen6_instdone2_bit(GEN6_PSD_DONE, "PSD");
		gen6_instdone2_bit(GEN6_IZ_DONE, "IZ");
		gen6_instdone2_bit(GEN6_WMFE_DONE, "WMFE");
		gen6_instdone2_bit(GEN6_SVSM_DONE, "SVSM");
		gen6_instdone2_bit(GEN6_QC_DONE, "QC");
		gen6_instdone2_bit(GEN6_FL_DONE, "FL");
		gen6_instdone2_bit(GEN6_SC_DONE, "SC");
		gen6_instdone2_bit(GEN6_DM_DONE, "DM");
		gen6_instdone2_bit(GEN6_FT_DONE, "FT");
		gen6_instdone2_bit(GEN6_DG_DONE, "DG");
		gen6_instdone2_bit(GEN6_SI_DONE, "SI");
		gen6_instdone2_bit(GEN6_SO_DONE, "SO");
		gen6_instdone2_bit(GEN6_PL_DONE, "PL");
		gen6_instdone2_bit(GEN6_VME_DONE, "VME");
		gen6_instdone2_bit(GEN6_SF_DONE, "SF");
		gen6_instdone2_bit(GEN6_CL_DONE, "CL");
		gen6_instdone2_bit(GEN6_GS_DONE, "GS");
		gen6_instdone2_bit(GEN6_VS0_DONE, "VS0");
		gen6_instdone2_bit(GEN6_VF_DONE, "VF");
	} else if (IS_GEN5(devid)) {
		gen4_instdone_bit(ILK_ROW_0_EU_0_DONE, "Row 0, EU 0");
		gen4_instdone_bit(ILK_ROW_0_EU_1_DONE, "Row 0, EU 1");
		gen4_instdone_bit(ILK_ROW_0_EU_2_DONE, "Row 0, EU 2");
		gen4_instdone_bit(ILK_ROW_0_EU_3_DONE, "Row 0, EU 3");
		gen4_instdone_bit(ILK_ROW_1_EU_0_DONE, "Row 1, EU 0");
		gen4_instdone_bit(ILK_ROW_1_EU_1_DONE, "Row 1, EU 1");
		gen4_instdone_bit(ILK_ROW_1_EU_2_DONE, "Row 1, EU 2");
		gen4_instdone_bit(ILK_ROW_1_EU_3_DONE, "Row 1, EU 3");
		gen4_instdone_bit(ILK_ROW_2_EU_0_DONE, "Row 2, EU 0");
		gen4_instdone_bit(ILK_ROW_2_EU_1_DONE, "Row 2, EU 1");
		gen4_instdone_bit(ILK_ROW_2_EU_2_DONE, "Row 2, EU 2");
		gen4_instdone_bit(ILK_ROW_2_EU_3_DONE, "Row 2, EU 3");
		gen4_instdone_bit(ILK_VCP_DONE, "VCP");
		gen4_instdone_bit(ILK_ROW_0_MATH_DONE, "Row 0 math");
		gen4_instdone_bit(ILK_ROW_1_MATH_DONE, "Row 1 math");
		gen4_instdone_bit(ILK_ROW_2_MATH_DONE, "Row 2 math");
		gen4_instdone_bit(ILK_VC1_DONE, "VC1");
		gen4_instdone_bit(ILK_ROW_0_MA_DONE, "Row 0 MA");
		gen4_instdone_bit(ILK_ROW_1_MA_DONE, "Row 1 MA");
		gen4_instdone_bit(ILK_ROW_2_MA_DONE, "Row 2 MA");
		gen4_instdone_bit(ILK_ROW_0_ISC_DONE, "Row 0 ISC");
		gen4_instdone_bit(ILK_ROW_1_ISC_DONE, "Row 1 ISC");
		gen4_instdone_bit(ILK_ROW_2_ISC_DONE, "Row 2 ISC");
		gen4_instdone_bit(ILK_VFE_DONE, "VFE");
		gen4_instdone_bit(ILK_TD_DONE, "TD");
		gen4_instdone_bit(ILK_SVTS_DONE, "SVTS");
		gen4_instdone_bit(ILK_TS_DONE, "TS");
		gen4_instdone_bit(ILK_GW_DONE, "GW");
		gen4_instdone_bit(ILK_AI_DONE, "AI");
		gen4_instdone_bit(ILK_AC_DONE, "AC");
		gen4_instdone_bit(ILK_AM_DONE, "AM");

		init_g4x_instdone1();
	} else if (IS_GEN4(devid)) {
		gen4_instdone_bit(I965_ROW_0_EU_0_DONE, "Row 0, EU 0");
		gen4_instdone_bit(I965_ROW_0_EU_1_DONE, "Row 0, EU 1");
		gen4_instdone_bit(I965_ROW_0_EU_2_DONE, "Row 0, EU 2");
		gen4_instdone_bit(I965_ROW_0_EU_3_DONE, "Row 0, EU 3");
		gen4_instdone_bit(I965_ROW_1_EU_0_DONE, "Row 1, EU 0");
		gen4_instdone_bit(I965_ROW_1_EU_1_DONE, "Row 1, EU 1");
		gen4_instdone_bit(I965_ROW_1_EU_2_DONE, "Row 1, EU 2");
		gen4_instdone_bit(I965_ROW_1_EU_3_DONE, "Row 1, EU 3");
		gen4_instdone_bit(I965_SF_DONE, "Strips and Fans");
		gen4_instdone_bit(I965_SE_DONE, "Setup Engine");
		gen4_instdone_bit(I965_WM_DONE, "Windowizer");
		gen4_instdone_bit(I965_DISPATCHER_DONE, "Dispatcher");
		gen4_instdone_bit(I965_PROJECTION_DONE, "Projection and LOD");
		gen4_instdone_bit(I965_DG_DONE, "Dependent address generator");
		gen4_instdone_bit(I965_QUAD_CACHE_DONE, "Texture fetch");
		gen4_instdone_bit(I965_TEXTURE_FETCH_DONE, "Texture fetch");
		gen4_instdone_bit(I965_TEXTURE_DECOMPRESS_DONE, "Texture decompress");
		gen4_instdone_bit(I965_SAMPLER_CACHE_DONE, "Sampler cache");
		gen4_instdone_bit(I965_FILTER_DONE, "Filtering");
		gen4_instdone_bit(I965_BYPASS_DONE, "Bypass FIFO");
		gen4_instdone_bit(I965_PS_DONE, "Pixel shader");
		gen4_instdone_bit(I965_CC_DONE, "Color calculator");
		gen4_instdone_bit(I965_MAP_FILTER_DONE, "Map filter");
		gen4_instdone_bit(I965_MAP_L2_IDLE, "Map L2");
		gen4_instdone_bit(I965_MA_ROW_0_DONE, "Message Arbiter row 0");
		gen4_instdone_bit(I965_MA_ROW_1_DONE, "Message Arbiter row 1");
		gen4_instdone_bit(I965_IC_ROW_0_DONE, "Instruction cache row 0");
		gen4_instdone_bit(I965_IC_ROW_1_DONE, "Instruction cache row 1");
		gen4_instdone_bit(I965_CP_DONE, "Command Processor");

		if (IS_G4X(devid)) {
			init_g4x_instdone1();
		} else {
			init_g965_instdone1();
		}
	} else if (IS_GEN3(devid)) {
		gen3_instdone_bit(IDCT_DONE, "IDCT");
		gen3_instdone_bit(IQ_DONE, "IQ");
		gen3_instdone_bit(PR_DONE, "PR");
		gen3_instdone_bit(VLD_DONE, "VLD");
		gen3_instdone_bit(IP_DONE, "Instruction parser");
		gen3_instdone_bit(FBC_DONE, "Framebuffer Compression");
		gen3_instdone_bit(BINNER_DONE, "Binner");
		gen3_instdone_bit(SF_DONE, "Strips and fans");
		gen3_instdone_bit(SE_DONE, "Setup engine");
		gen3_instdone_bit(WM_DONE, "Windowizer");
		gen3_instdone_bit(IZ_DONE, "Intermediate Z");
		gen3_instdone_bit(PERSPECTIVE_INTERP_DONE, "Perspective interpolation");
		gen3_instdone_bit(DISPATCHER_DONE, "Dispatcher");
		gen3_instdone_bit(PROJECTION_DONE, "Projection and LOD");
		gen3_instdone_bit(DEPENDENT_ADDRESS_DONE, "Dependent address calculation");
		gen3_instdone_bit(TEXTURE_FETCH_DONE, "Texture fetch");
		gen3_instdone_bit(TEXTURE_DECOMPRESS_DONE, "Texture decompression");
		gen3_instdone_bit(SAMPLER_CACHE_DONE, "Sampler Cache");
		gen3_instdone_bit(FILTER_DONE, "Filtering");
		gen3_instdone_bit(BYPASS_FIFO_DONE, "Bypass FIFO");
		gen3_instdone_bit(PS_DONE, "Pixel shader");
		gen3_instdone_bit(CC_DONE, "Color calculator");
		gen3_instdone_bit(MAP_FILTER_DONE, "Map filter");
		gen3_instdone_bit(MAP_L2_IDLE, "Map L2");
	} else {
		assert(IS_GEN2(devid));
		gen3_instdone_bit(I830_GMBUS_DONE, "GMBUS");
		gen3_instdone_bit(I830_FBC_DONE, "FBC");
		gen3_instdone_bit(I830_BINNER_DONE, "BINNER");
		gen3_instdone_bit(I830_MPEG_DONE, "MPEG");
		gen3_instdone_bit(I830_MECO_DONE, "MECO");
		gen3_instdone_bit(I830_MCD_DONE, "MCD");
		gen3_instdone_bit(I830_MCSTP_DONE, "MCSTP");
		gen3_instdone_bit(I830_CC_DONE, "CC");
		gen3_instdone_bit(I830_DG_DONE, "DG");
		gen3_instdone_bit(I830_DCMP_DONE, "DCMP");
		gen3_instdone_bit(I830_FTCH_DONE, "FTCH");
		gen3_instdone_bit(I830_IT_DONE, "IT");
		gen3_instdone_bit(I830_MG_DONE, "MG");
		gen3_instdone_bit(I830_MEC_DONE, "MEC");
		gen3_instdone_bit(I830_PC_DONE, "PC");
		gen3_instdone_bit(I830_QCC_DONE, "QCC");
		gen3_instdone_bit(I830_TB_DONE, "TB");
		gen3_instdone_bit(I830_WM_DONE, "WM");
		gen3_instdone_bit(I830_EF_DONE, "EF");
		gen3_instdone_bit(I830_BLITTER_DONE, "Blitter");
		gen3_instdone_bit(I830_MAP_L2_DONE, "Map L2 cache");
		gen3_instdone_bit(I830_SECONDARY_RING_3_DONE, "Secondary ring 3");
		gen3_instdone_bit(I830_SECONDARY_RING_2_DONE, "Secondary ring 2");
		gen3_instdone_bit(I830_SECONDARY_RING_1_DONE, "Secondary ring 1");
		gen3_instdone_bit(I830_SECONDARY_RING_0_DONE, "Secondary ring 0");
		gen3_instdone_bit(I830_PRIMARY_RING_1_DONE, "Primary ring 1");
		gen3_instdone_bit(I830_PRIMARY_RING_0_DONE, "Primary ring 0");
	}
}
