/*
 * Copyright © 2009 Intel Corporation
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
 *    Zhenyu Wang <zhenyu.z.wang@intel.com>
 *    Wu Fengguang <fengguang.wu@intel.com>
 *
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <arpa/inet.h>
#include "intel_io.h"
#include "intel_reg.h"
#include "intel_chipset.h"
#include "drmtest.h"

static uint32_t devid;

static int aud_reg_base = 0;	/* base address of audio registers */
static int disp_reg_base = 0;	/* base address of display registers */

#define IS_HASWELL_PLUS(devid)  (IS_HASWELL(devid) || IS_BROADWELL(devid))

#define BITSTO(n)		(n >= sizeof(long) * 8 ? ~0 : (1UL << (n)) - 1)
#define BITMASK(high, low)	(BITSTO(high+1) & ~BITSTO(low))
#define BITS(reg, high, low)	(((reg) & (BITMASK(high, low))) >> (low))
#define BIT(reg, n)		BITS(reg, n, n)

#define min_t(type, x, y) ({                    \
		type __min1 = (x);                      \
		type __min2 = (y);                      \
		__min1 < __min2 ? __min1 : __min2; })

#define OPNAME(names, index)   \
	names[min_t(unsigned int, index, ARRAY_SIZE(names) - 1)]

#define set_aud_reg_base(base)    (aud_reg_base = (base))

#define set_reg_base(base, audio_offset)	\
	do {					\
		disp_reg_base = (base);		\
		set_aud_reg_base((base) + (audio_offset));	\
	} while (0)

#define dump_reg(reg, desc)					\
	do {							\
		dword = INREG(reg);	\
		printf("%-21s 0x%08x  %s\n", # reg, dword, desc);	\
	} while (0)

#define dump_disp_reg(reg, desc)					\
	do {							\
		dword = INREG(disp_reg_base + reg);	\
		printf("%-21s 0x%08x  %s\n", # reg, dword, desc);	\
	} while (0)

#define dump_aud_reg(reg, desc)					\
	do {							\
		dword = INREG(aud_reg_base + reg);	\
		printf("%-21s 0x%08x  %s\n", # reg, dword, desc);	\
	} while (0)

#define read_aud_reg(reg)	INREG(aud_reg_base + (reg))

static int get_num_pipes(void)
{
	int num_pipes;

	if (IS_VALLEYVIEW(devid))
		num_pipes = 2;  /* Valleyview is Gen 7 but only has 2 pipes */
	else if (IS_G4X(devid) || IS_GEN5(devid))
		num_pipes = 2;
	else
		num_pipes = 3;

	return num_pipes;
}

static const char * const cts_m_value_index[] = {
	[0] = "CTS",
	[1] = "M",
};

static const char * const pixel_clock[] = {
	[0] = "25.2 / 1.001 MHz",
	[1] = "25.2 MHz",
	[2] = "27 MHz",
	[3] = "27 * 1.001 MHz",
	[4] = "54 MHz",
	[5] = "54 * 1.001 MHz",
	[6] = "74.25 / 1.001 MHz",
	[7] = "74.25 MHz",
	[8] = "148.5 / 1.001 MHz",
	[9] = "148.5 MHz",
	[10] = "Reserved",
};

static const char * const power_state[] = {
	[0] = "D0",
	[1] = "D1",
	[2] = "D2",
	[3] = "D3",
};

static const char * const stream_type[] = {
	[0] = "default samples",
	[1] = "one bit stream",
	[2] = "DST stream",
	[3] = "MLP stream",
	[4] = "Reserved",
};

static const char * const dip_port[] = {
	[0] = "Reserved",
	[1] = "Digital Port B",
	[2] = "Digital Port C",
	[3] = "Digital Port D",
};

static const char * const dip_type[] = {
	[0] = "Audio DIP Disabled",
	[1] = "Audio DIP Enabled",
};

static const char * const dip_gen1_state[] = {
	[0] = "Generic 1 (ACP) DIP Disabled",
	[1] = "Generic 1 (ACP) DIP Enabled",
};

static const char * const dip_gen2_state[] = {
	[0] = "Generic 2 DIP Disabled",
	[1] = "Generic 2 DIP Enabled",
};

static const char * const dip_index[] = {
	[0] = "Audio DIP",
	[1] = "ACP DIP",
	[2] = "ISRC1 DIP",
	[3] = "ISRC2 DIP",
	[4] = "Reserved",
};

static const char * const dip_trans[] = {
	[0] = "disabled",
	[1] = "reserved",
	[2] = "send once",
	[3] = "best effort",
};

static const char * const video_dip_index[] = {
	[0] = "AVI DIP",
	[1] = "Vendor-specific DIP",
	[2] = "Gamut Metadata DIP",
	[3] = "Source Product Description DIP",
};

static const char * const video_dip_trans[] = {
	[0] = "send once",
	[1] = "send every vsync",
	[2] = "send at least every other vsync",
	[3] = "reserved",
};

static const char * const trans_to_port_sel[] = {
	[0] = "no port",
	[1] = "Digital Port B",
	[2] = "Digital Port C",
	[3] = "Digital Port D",
	[4] = "reserved",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char * const ddi_mode[] = {
	[0] = "HDMI mode",
	[1] = "DVI mode",
	[2] = "DP SST mode",
	[3] = "DP MST mode",
	[4] = "DP FDI mode",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char * const bits_per_color[] = {
	[0] = "8 bpc",
	[1] = "10 bpc",
	[2] = "6 bpc",
	[3] = "12 bpc",
	[4] = "reserved",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char * const transcoder_select[] = {
	[0] = "Transcoder A",
	[1] = "Transcoder B",
	[2] = "Transcoder C",
	[3] = "reserved",
};

static const char * const dp_port_width[] = {
	[0] = "x1 mode",
	[1] = "x2 mode",
	[2] = "reserved",
	[3] = "x4 mode",
	[4] = "reserved",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char * const sample_base_rate[] = {
	[0] = "48 kHz",
	[1] = "44.1 kHz",
};

static const char * const sample_base_rate_mult[] = {
	[0] = "x1 (48 kHz, 44.1 kHz or less)",
	[1] = "x2 (96 kHz, 88.2 kHz, 32 kHz)",
	[2] = "x3 (144 kHz)",
	[3] = "x4 (192 kHz, 176.4 kHz)",
	[4] = "Reserved",
};

static const char * const sample_base_rate_divisor[] = {
	[0] = "Divided by 1 (48 kHz, 44.1 kHz)",
	[1] = "Divided by 2 (24 kHz, 22.05 kHz)",
	[2] = "Divided by 3 (16 kHz, 32 kHz)",
	[3] = "Divided by 4 (11.025 kHz)",
	[4] = "Divided by 5 (9.6 kHz)",
	[5] = "Divided by 6 (8 kHz)",
	[6] = "Divided by 7",
	[7] = "Divided by 8 (6 kHz)",
};

static const char * const connect_list_form[] = {
	[0] = "Short Form",
	[1] = "Long Form",
};


static const char * const bits_per_sample[] = {
	[0] = "reserved",
	[1] = "16 bits",
	[2] = "24 bits",
	[3] = "32 bits",
	[4] = "20 bits",
	[5] = "reserved",
};

static const char * const sdvo_hdmi_encoding[] = {
	[0] = "SDVO",
	[1] = "reserved",
	[2] = "TMDS",
	[3] = "reserved",
};

static const char * const n_index_value[] = {
	[0] = "HDMI",
	[1] = "DisplayPort",
};

static const char * const immed_result_valid[] = {
	[0] = "No immediate response is available",
	[1] = "Immediate response is available",
};

static const char * const immed_cmd_busy[] = {
	[0] = "Can accept an immediate command",
	[1] = "Immediate command is available",
};

static const char * const vanilla_dp12_en[] = {
	[0] = "DP 1.2 features are disabled",
	[1] = "DP 1.2 features are enabled",
};

static const char * const vanilla_3_widgets_en[] = {
	[0] = "2nd & 3rd pin/convertor widgets are disabled",
	[1] = "All three pin/convertor widgets are enabled",
};

static const char * const block_audio[] = {
	[0] = "Allow audio data to reach the port",
	[1] = "Block audio data from reaching the port",
};

static const char * const dis_eld_valid_pulse_trans[] = {
	[0] = "Enable ELD valid pulse transition when unsol is disabled",
	[1] = "Disable ELD valid pulse transition when unsol is disabled",
};

static const char * const dis_pd_pulse_trans[] = {
	[0] = "Enable Presense Detect pulse transition when unsol is disabled",
	[1] = "Disable Presense Detect pulse transition when unsol is disabled",
};

static const char * const dis_ts_delta_err[] = {
	[0] = "Enable timestamp delta error for 32/44 KHz",
	[1] = "Disable timestamp delta error for 32/44 KHz",
};

static const char * const dis_ts_fix_dp_hbr[] = {
	[0] = "Enable timestamp fix for DP HBR",
	[1] = "Disable timestamp fix for DP HBR",
};

static const char * const pattern_gen_8_ch_en[] = {
	[0] = "Disable 8-channel pattern generator",
	[1] = "Enable 8-channel pattern generator",
};

static const char * const pattern_gen_2_ch_en[] = {
	[0] = "Disable 2-channel pattern generator",
	[1] = "Enable 2-channel pattern generator",
};

static const char * const fabric_32_44_dis[] = {
	[0] = "Allow sample fabrication for 32/44 KHz",
	[1] = "Disable sample fabrication for 32/44 KHz",
};

static const char * const epss_dis[] = {
	[0] = "Allow audio EPSS",
	[1] = "Disable audio EPSS",
};

static const char * const ts_test_mode[] = {
	[0] = "Default time stamp mode",
	[1] = "Audio time stamp test mode for audio only feature",
};

static const char * const en_mmio_program[] = {
	[0] = "Programming by HD-Audio Azalia",
	[1] = "Programming by MMIO debug registers",
};

static const char * const audio_dp_dip_status[] = {
	[0] = "audfc dp fifo full",
	[1] = "audfc dp fifo empty",
	[2] = "audfc dp fifo overrun",
	[3] = "audfc dip fifo full",
	[4] = "audfc dp fifo empty cd",
	[5] = "audfb dp fifo full",
	[6] = "audfb dp fifo empty",
	[7] = "audfb dp fifo overrun",
	[8] = "audfb dip fifo full",
	[9] = "audfb dp fifo empty cd",
	[10] = "audfa dp fifo full",
	[11] = "audfa dp fifo empty",
	[12] = "audfa dp fifo overrun",
	[13] = "audfa dip fifo full",
	[14] = "audfa dp fifo empty cd",
	[15] = "Pipe c audio overflow",
	[16] = "Pipe b audio overflow",
	[17] = "Pipe a audio overflow",
	[31] = 0,
};

#undef TRANSCODER_A
#undef TRANSCODER_B
#undef TRANSCODER_C

enum {
	TRANSCODER_A = 0,
	TRANSCODER_B,
	TRANSCODER_C,
};

enum {
	PIPE_A = 0,
	PIPE_B,
	PIPE_C,
};

enum {
	PORT_A = 0,
	PORT_B,
	PORT_C,
	PORT_D,
	PORT_E,
};

enum {
	CONVERTER_1 = 0,
	CONVERTER_2,
	CONVERTER_3,
};

static void do_self_tests(void)
{
	if (BIT(1, 0) != 1)
		exit(1);
	if (BIT(0x80000000, 31) != 1)
		exit(2);
	if (BITS(0xc0000000, 31, 30) != 3)
		exit(3);
}

/*
 * EagleLake registers
 */
#define AUD_CONFIG		0x62000
#define AUD_DEBUG		0x62010
#define AUD_VID_DID		0x62020
#define AUD_RID			0x62024
#define AUD_SUBN_CNT		0x62028
#define AUD_FUNC_GRP		0x62040
#define AUD_SUBN_CNT2		0x62044
#define AUD_GRP_CAP		0x62048
#define AUD_PWRST		0x6204c
#define AUD_SUPPWR		0x62050
#define AUD_SID			0x62054
#define AUD_OUT_CWCAP		0x62070
#define AUD_OUT_PCMSIZE		0x62074
#define AUD_OUT_STR		0x62078
#define AUD_OUT_DIG_CNVT	0x6207c
#define AUD_OUT_CH_STR		0x62080
#define AUD_OUT_STR_DESC	0x62084
#define AUD_PINW_CAP		0x620a0
#define AUD_PIN_CAP		0x620a4
#define AUD_PINW_CONNLNG	0x620a8
#define AUD_PINW_CONNLST	0x620ac
#define AUD_PINW_CNTR		0x620b0
#define AUD_PINW_UNSOLRESP	0x620b8
#define AUD_CNTL_ST		0x620b4
#define AUD_PINW_CONFIG		0x620bc
#define AUD_HDMIW_STATUS	0x620d4
#define AUD_HDMIW_HDMIEDID	0x6210c
#define AUD_HDMIW_INFOFR	0x62118
#define AUD_CONV_CHCNT 		0x62120
#define AUD_CTS_ENABLE		0x62128

#define VIDEO_DIP_CTL		0x61170
#define VIDEO_DIP_ENABLE	(1<<31)
#define VIDEO_DIP_ENABLE_AVI	(1<<21)
#define VIDEO_DIP_ENABLE_VENDOR	(1<<22)
#define VIDEO_DIP_ENABLE_SPD	(1<<24)
#define VIDEO_DIP_BUF_AVI	(0<<19)
#define VIDEO_DIP_BUF_VENDOR	(1<<19)
#define VIDEO_DIP_BUF_SPD	(3<<19)
#define VIDEO_DIP_TRANS_ONCE	(0<<16)
#define VIDEO_DIP_TRANS_1	(1<<16)
#define VIDEO_DIP_TRANS_2	(2<<16)

#define AUDIO_HOTPLUG_EN	(1<<24)


static void dump_eaglelake(void)
{
	uint32_t dword;
	int i;

	/* printf("%-18s   %8s  %s\n\n", "register name", "raw value", "description"); */

	dump_reg(VIDEO_DIP_CTL,	"Video DIP Control");
	dump_reg(SDVOB,		"Digital Display Port B Control Register");
	dump_reg(SDVOC,		"Digital Display Port C Control Register");
	dump_reg(PORT_HOTPLUG_EN,	"Hot Plug Detect Enable");

	dump_reg(AUD_CONFIG,	"Audio Configuration");
	dump_reg(AUD_DEBUG,		"Audio Debug");
	dump_reg(AUD_VID_DID,	"Audio Vendor ID / Device ID");
	dump_reg(AUD_RID,		"Audio Revision ID");
	dump_reg(AUD_SUBN_CNT,	"Audio Subordinate Node Count");
	dump_reg(AUD_FUNC_GRP,	"Audio Function Group Type");
	dump_reg(AUD_SUBN_CNT2,	"Audio Subordinate Node Count");
	dump_reg(AUD_GRP_CAP,	"Audio Function Group Capabilities");
	dump_reg(AUD_PWRST,		"Audio Power State");
	dump_reg(AUD_SUPPWR,	"Audio Supported Power States");
	dump_reg(AUD_SID,		"Audio Root Node Subsystem ID");
	dump_reg(AUD_OUT_CWCAP,	"Audio Output Converter Widget Capabilities");
	dump_reg(AUD_OUT_PCMSIZE,	"Audio PCM Size and Rates");
	dump_reg(AUD_OUT_STR,	"Audio Stream Formats");
	dump_reg(AUD_OUT_DIG_CNVT,	"Audio Digital Converter");
	dump_reg(AUD_OUT_CH_STR,	"Audio Channel ID and Stream ID");
	dump_reg(AUD_OUT_STR_DESC,	"Audio Stream Descriptor Format");
	dump_reg(AUD_PINW_CAP,	"Audio Pin Complex Widget Capabilities");
	dump_reg(AUD_PIN_CAP,	"Audio Pin Capabilities");
	dump_reg(AUD_PINW_CONNLNG,	"Audio Connection List Length");
	dump_reg(AUD_PINW_CONNLST,	"Audio Connection List Entry");
	dump_reg(AUD_PINW_CNTR,	"Audio Pin Widget Control");
	dump_reg(AUD_PINW_UNSOLRESP, "Audio Unsolicited Response Enable");
	dump_reg(AUD_CNTL_ST,	"Audio Control State Register");
	dump_reg(AUD_PINW_CONFIG,	"Audio Configuration Default");
	dump_reg(AUD_HDMIW_STATUS,	"Audio HDMI Status");
	dump_reg(AUD_HDMIW_HDMIEDID, "Audio HDMI Data EDID Block");
	dump_reg(AUD_HDMIW_INFOFR,	"Audio HDMI Widget Data Island Packet");
	dump_reg(AUD_CONV_CHCNT,	"Audio Converter Channel Count");
	dump_reg(AUD_CTS_ENABLE,	"Audio CTS Programming Enable");

	printf("\nDetails:\n\n");

	dword = INREG(AUD_VID_DID);
	printf("AUD_VID_DID vendor id\t\t\t0x%x\n", dword >> 16);
	printf("AUD_VID_DID device id\t\t\t0x%x\n", dword & 0xffff);

	dword = INREG(AUD_RID);
	printf("AUD_RID major revision\t\t\t0x%lx\n", BITS(dword, 23, 20));
	printf("AUD_RID minor revision\t\t\t0x%lx\n", BITS(dword, 19, 16));
	printf("AUD_RID revision id\t\t\t0x%lx\n",    BITS(dword, 15, 8));
	printf("AUD_RID stepping id\t\t\t0x%lx\n",    BITS(dword, 7, 0));

	dword = INREG(SDVOB);
	printf("SDVOB enable\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
	printf("SDVOB HDMI encoding\t\t\t%u\n", !!(dword & SDVO_ENCODING_HDMI));
	printf("SDVOB SDVO encoding\t\t\t%u\n", !!(dword & SDVO_ENCODING_SDVO));
	printf("SDVOB null packets\t\t\t%u\n",  !!(dword & SDVO_NULL_PACKETS_DURING_VSYNC));
	printf("SDVOB audio enabled\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

	dword = INREG(SDVOC);
	printf("SDVOC enable\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
	printf("SDVOC HDMI encoding\t\t\t%u\n", !!(dword & SDVO_ENCODING_HDMI));
	printf("SDVOC SDVO encoding\t\t\t%u\n", !!(dword & SDVO_ENCODING_SDVO));
	printf("SDVOC null packets\t\t\t%u\n",  !!(dword & SDVO_NULL_PACKETS_DURING_VSYNC));
	printf("SDVOC audio enabled\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

	dword = INREG(PORT_HOTPLUG_EN);
	printf("PORT_HOTPLUG_EN DisplayPort/HDMI port B\t%ld\n", BIT(dword, 29)),
	printf("PORT_HOTPLUG_EN DisplayPort/HDMI port C\t%ld\n", BIT(dword, 28)),
	printf("PORT_HOTPLUG_EN DisplayPort port D\t%ld\n",      BIT(dword, 27)),
	printf("PORT_HOTPLUG_EN SDVOB\t\t\t%ld\n", BIT(dword, 26)),
	printf("PORT_HOTPLUG_EN SDVOC\t\t\t%ld\n", BIT(dword, 25)),
	printf("PORT_HOTPLUG_EN audio\t\t\t%ld\n", BIT(dword, 24)),
	printf("PORT_HOTPLUG_EN TV\t\t\t%ld\n",    BIT(dword, 23)),
	printf("PORT_HOTPLUG_EN CRT\t\t\t%ld\n",   BIT(dword, 9)),

	dword = INREG(VIDEO_DIP_CTL);
	printf("VIDEO_DIP_CTL enable graphics DIP\t%ld\n",     BIT(dword, 31)),
	printf("VIDEO_DIP_CTL port select\t\t[0x%lx] %s\n",
				BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);
	printf("VIDEO_DIP_CTL DIP buffer trans active\t%lu\n", BIT(dword, 28));
	printf("VIDEO_DIP_CTL AVI DIP enabled\t\t%lu\n",       BIT(dword, 21));
	printf("VIDEO_DIP_CTL vendor DIP enabled\t%lu\n",      BIT(dword, 22));
	printf("VIDEO_DIP_CTL SPD DIP enabled\t\t%lu\n",       BIT(dword, 24));
	printf("VIDEO_DIP_CTL DIP buffer index\t\t[0x%lx] %s\n",
			BITS(dword, 20, 19), video_dip_index[BITS(dword, 20, 19)]);
	printf("VIDEO_DIP_CTL DIP trans freq\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), video_dip_trans[BITS(dword, 17, 16)]);
	printf("VIDEO_DIP_CTL DIP buffer size\t\t%lu\n", BITS(dword, 11, 8));
	printf("VIDEO_DIP_CTL DIP address\t\t%lu\n", BITS(dword, 3, 0));

	dword = INREG(AUD_CONFIG);
	printf("AUD_CONFIG pixel clock\t\t\t[0x%lx] %s\n", BITS(dword, 19, 16),
			OPNAME(pixel_clock, BITS(dword, 19, 16)));
	printf("AUD_CONFIG fabrication enabled\t\t%lu\n", BITS(dword, 2, 2));
	printf("AUD_CONFIG professional use allowed\t%lu\n", BIT(dword, 1));
	printf("AUD_CONFIG fuse enabled\t\t\t%lu\n", BIT(dword, 0));

	dword = INREG(AUD_DEBUG);
	printf("AUD_DEBUG function reset\t\t%lu\n", BIT(dword, 0));

	dword = INREG(AUD_SUBN_CNT);
	printf("AUD_SUBN_CNT starting node number\t0x%lx\n",  BITS(dword, 23, 16));
	printf("AUD_SUBN_CNT total number of nodes\t0x%lx\n", BITS(dword, 7, 0));

	dword = INREG(AUD_SUBN_CNT2);
	printf("AUD_SUBN_CNT2 starting node number\t0x%lx\n",  BITS(dword, 24, 16));
	printf("AUD_SUBN_CNT2 total number of nodes\t0x%lx\n", BITS(dword, 7, 0));

	dword = INREG(AUD_FUNC_GRP);
	printf("AUD_FUNC_GRP unsol capable\t\t%lu\n", BIT(dword, 8));
	printf("AUD_FUNC_GRP node type\t\t\t0x%lx\n", BITS(dword, 7, 0));

	dword = INREG(AUD_GRP_CAP);
	printf("AUD_GRP_CAP beep 0\t\t\t%lu\n",       BIT(dword, 16));
	printf("AUD_GRP_CAP input delay\t\t\t%lu\n",  BITS(dword, 11, 8));
	printf("AUD_GRP_CAP output delay\t\t%lu\n",   BITS(dword, 3, 0));

	dword = INREG(AUD_PWRST);
	printf("AUD_PWRST device power state\t\t%s\n",
			power_state[BITS(dword, 5, 4)]);
	printf("AUD_PWRST device power state setting\t%s\n",
			power_state[BITS(dword, 1, 0)]);

	dword = INREG(AUD_SUPPWR);
	printf("AUD_SUPPWR support D0\t\t\t%lu\n", BIT(dword, 0));
	printf("AUD_SUPPWR support D1\t\t\t%lu\n", BIT(dword, 1));
	printf("AUD_SUPPWR support D2\t\t\t%lu\n", BIT(dword, 2));
	printf("AUD_SUPPWR support D3\t\t\t%lu\n", BIT(dword, 3));

	dword = INREG(AUD_OUT_CWCAP);
	printf("AUD_OUT_CWCAP widget type\t\t0x%lx\n",  BITS(dword, 23, 20));
	printf("AUD_OUT_CWCAP sample delay\t\t0x%lx\n", BITS(dword, 19, 16));
	printf("AUD_OUT_CWCAP channel count\t\t%lu\n",
			BITS(dword, 15, 13) * 2 + BIT(dword, 0) + 1);
	printf("AUD_OUT_CWCAP L-R swap\t\t\t%lu\n",       BIT(dword, 11));
	printf("AUD_OUT_CWCAP power control\t\t%lu\n",    BIT(dword, 10));
	printf("AUD_OUT_CWCAP digital\t\t\t%lu\n",        BIT(dword, 9));
	printf("AUD_OUT_CWCAP conn list\t\t\t%lu\n",      BIT(dword, 8));
	printf("AUD_OUT_CWCAP unsol\t\t\t%lu\n",          BIT(dword, 7));
	printf("AUD_OUT_CWCAP mute\t\t\t%lu\n",           BIT(dword, 5));
	printf("AUD_OUT_CWCAP format override\t\t%lu\n",  BIT(dword, 4));
	printf("AUD_OUT_CWCAP amp param override\t%lu\n", BIT(dword, 3));
	printf("AUD_OUT_CWCAP out amp present\t\t%lu\n",  BIT(dword, 2));
	printf("AUD_OUT_CWCAP in amp present\t\t%lu\n",   BIT(dword, 1));

	dword = INREG(AUD_OUT_DIG_CNVT);
	printf("AUD_OUT_DIG_CNVT SPDIF category\t\t0x%lx\n", BITS(dword, 14, 8));
	printf("AUD_OUT_DIG_CNVT SPDIF level\t\t%lu\n",      BIT(dword, 7));
	printf("AUD_OUT_DIG_CNVT professional\t\t%lu\n",     BIT(dword, 6));
	printf("AUD_OUT_DIG_CNVT non PCM\t\t%lu\n",          BIT(dword, 5));
	printf("AUD_OUT_DIG_CNVT copyright asserted\t%lu\n", BIT(dword, 4));
	printf("AUD_OUT_DIG_CNVT filter preemphasis\t%lu\n", BIT(dword, 3));
	printf("AUD_OUT_DIG_CNVT validity config\t%lu\n",    BIT(dword, 2));
	printf("AUD_OUT_DIG_CNVT validity flag\t\t%lu\n",    BIT(dword, 1));
	printf("AUD_OUT_DIG_CNVT digital enable\t\t%lu\n",   BIT(dword, 0));

	dword = INREG(AUD_OUT_CH_STR);
	printf("AUD_OUT_CH_STR stream id\t\t0x%lx\n",        BITS(dword, 7, 4));
	printf("AUD_OUT_CH_STR lowest channel\t\t%lu\n",     BITS(dword, 3, 0));

	dword = INREG(AUD_OUT_STR_DESC);
	printf("AUD_OUT_STR_DESC stream channels\t%lu\n",    BITS(dword, 3, 0) + 1);
	printf("AUD_OUT_STR_DESC Bits per Sample\t[%#lx] %s\n",
			BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));

	dword = INREG(AUD_PINW_CAP);
	printf("AUD_PINW_CAP widget type\t\t0x%lx\n",        BITS(dword, 23, 20));
	printf("AUD_PINW_CAP sample delay\t\t0x%lx\n",       BITS(dword, 19, 16));
	printf("AUD_PINW_CAP channel count\t\t%lu\n",
			BITS(dword, 15, 13) * 2 + BIT(dword, 0) + 1);
	printf("AUD_PINW_CAP HDCP\t\t\t%lu\n",               BIT(dword, 12));
	printf("AUD_PINW_CAP L-R swap\t\t\t%lu\n",           BIT(dword, 11));
	printf("AUD_PINW_CAP power control\t\t%lu\n",        BIT(dword, 10));
	printf("AUD_PINW_CAP digital\t\t\t%lu\n",            BIT(dword, 9));
	printf("AUD_PINW_CAP conn list\t\t\t%lu\n",          BIT(dword, 8));
	printf("AUD_PINW_CAP unsol\t\t\t%lu\n",              BIT(dword, 7));
	printf("AUD_PINW_CAP mute\t\t\t%lu\n",               BIT(dword, 5));
	printf("AUD_PINW_CAP format override\t\t%lu\n",      BIT(dword, 4));
	printf("AUD_PINW_CAP amp param override\t\t%lu\n",   BIT(dword, 3));
	printf("AUD_PINW_CAP out amp present\t\t%lu\n",      BIT(dword, 2));
	printf("AUD_PINW_CAP in amp present\t\t%lu\n",       BIT(dword, 1));


	dword = INREG(AUD_PIN_CAP);
	printf("AUD_PIN_CAP EAPD\t\t\t%lu\n",          BIT(dword, 16));
	printf("AUD_PIN_CAP HDMI\t\t\t%lu\n",          BIT(dword, 7));
	printf("AUD_PIN_CAP output\t\t\t%lu\n",        BIT(dword, 4));
	printf("AUD_PIN_CAP presence detect\t\t%lu\n", BIT(dword, 2));

	dword = INREG(AUD_PINW_CNTR);
	printf("AUD_PINW_CNTR mute status\t\t%lu\n",     BIT(dword, 8));
	printf("AUD_PINW_CNTR out enable\t\t%lu\n",      BIT(dword, 6));
	printf("AUD_PINW_CNTR amp mute status\t\t%lu\n", BIT(dword, 8));
	printf("AUD_PINW_CNTR amp mute status\t\t%lu\n", BIT(dword, 8));
	printf("AUD_PINW_CNTR stream type\t\t[0x%lx] %s\n",
			BITS(dword, 2, 0),
			OPNAME(stream_type, BITS(dword, 2, 0)));

	dword = INREG(AUD_PINW_UNSOLRESP);
	printf("AUD_PINW_UNSOLRESP enable unsol resp\t%lu\n", BIT(dword, 31));

	dword = INREG(AUD_CNTL_ST);
	printf("AUD_CNTL_ST DIP audio enabled\t\t%lu\n", BIT(dword, 21));
	printf("AUD_CNTL_ST DIP ACP enabled\t\t%lu\n",   BIT(dword, 22));
	printf("AUD_CNTL_ST DIP ISRCx enabled\t\t%lu\n", BIT(dword, 23));
	printf("AUD_CNTL_ST DIP port select\t\t[0x%lx] %s\n",
			BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);
	printf("AUD_CNTL_ST DIP buffer index\t\t[0x%lx] %s\n",
			BITS(dword, 20, 18), OPNAME(dip_index, BITS(dword, 20, 18)));
	printf("AUD_CNTL_ST DIP trans freq\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
	printf("AUD_CNTL_ST DIP address\t\t\t%lu\n", BITS(dword, 3, 0));
	printf("AUD_CNTL_ST CP ready\t\t\t%lu\n",    BIT(dword, 15));
	printf("AUD_CNTL_ST ELD valid\t\t\t%lu\n",   BIT(dword, 14));
	printf("AUD_CNTL_ST ELD ack\t\t\t%lu\n",     BIT(dword, 4));
	printf("AUD_CNTL_ST ELD bufsize\t\t\t%lu\n", BITS(dword, 13, 9));
	printf("AUD_CNTL_ST ELD address\t\t\t%lu\n", BITS(dword, 8, 5));

	dword = INREG(AUD_HDMIW_STATUS);
	printf("AUD_HDMIW_STATUS CDCLK/DOTCLK underrun\t%lu\n", BIT(dword, 31));
	printf("AUD_HDMIW_STATUS CDCLK/DOTCLK overrun\t%lu\n",  BIT(dword, 30));
	printf("AUD_HDMIW_STATUS BCLK/CDCLK underrun\t%lu\n",   BIT(dword, 29));
	printf("AUD_HDMIW_STATUS BCLK/CDCLK overrun\t%lu\n",    BIT(dword, 28));

	dword = INREG(AUD_CONV_CHCNT);
	printf("AUD_CONV_CHCNT HDMI HBR enabled\t\t%lu\n", BITS(dword, 15, 14));
	printf("AUD_CONV_CHCNT HDMI channel count\t%lu\n", BITS(dword, 11, 8) + 1);

	printf("AUD_CONV_CHCNT HDMI channel mapping:\n");
	for (i = 0; i < 8; i++) {
		OUTREG(AUD_CONV_CHCNT, i);
		dword = INREG(AUD_CONV_CHCNT);
		printf("\t\t\t\t\t[0x%x] %u => %lu\n", dword, i, BITS(dword, 7, 4));
	}

	printf("AUD_HDMIW_HDMIEDID HDMI ELD:\n\t");
	dword = INREG(AUD_CNTL_ST);
	dword &= ~BITMASK(8, 5);
	OUTREG(AUD_CNTL_ST, dword);
	for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID)));
	printf("\n");

	printf("AUD_HDMIW_INFOFR HDMI audio Infoframe:\n\t");
	dword = INREG(AUD_CNTL_ST);
	dword &= ~BITMASK(20, 18);
	dword &= ~BITMASK(3, 0);
	OUTREG(AUD_CNTL_ST, dword);
	for (i = 0; i < 8; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR)));
	printf("\n");
}

#undef AUD_RID
#undef AUD_VID_DID
#undef AUD_PWRST
#undef AUD_OUT_CH_STR
#undef AUD_HDMIW_STATUS

/*
 * CougarPoint registers
 */
#define DP_CTL_B              0xE4100
#define DP_CTL_C              0xE4200
#define DP_AUX_CTL_C          0xE4210
#define DP_AUX_TST_C          0xE4228
#define SPORT_DDI_CRC_C       0xE4250
#define SPORT_DDI_CRC_R       0xE4264
#define DP_CTL_D              0xE4300
#define DP_AUX_CTL_D          0xE4310
#define DP_AUX_TST_D          0xE4328
#define SPORT_DDI_CRC_CTL_D   0xE4350
#define AUD_CONFIG_A          0xE5000
#define AUD_MISC_CTRL_A       0xE5010
#define AUD_VID_DID           0xE5020
#define AUD_RID               0xE5024
#define AUD_CTS_ENABLE_A      0xE5028
#define AUD_PWRST             0xE504C
#define AUD_HDMIW_HDMIEDID_A  0xE5050
#define AUD_HDMIW_INFOFR_A    0xE5054
#define AUD_PORT_EN_HD_CFG    0xE507C
#define AUD_OUT_DIG_CNVT_A    0xE5080
#define AUD_OUT_STR_DESC_A    0xE5084
#define AUD_OUT_CH_STR        0xE5088
#define AUD_PINW_CONNLNG_LIST 0xE50A8
#define AUD_PINW_CONNLNG_SEL  0xE50AC
#define AUD_CNTL_ST_A         0xE50B4
#define AUD_CNTRL_ST2         0xE50C0
#define AUD_CNTRL_ST3         0xE50C4
#define AUD_HDMIW_STATUS      0xE50D4
#define AUD_CONFIG_B          0xE5100
#define AUD_MISC_CTRL_B       0xE5110
#define AUD_CTS_ENABLE_B      0xE5128
#define AUD_HDMIW_HDMIEDID_B  0xE5150
#define AUD_HDMIW_INFOFR_B    0xE5154
#define AUD_OUT_DIG_CNVT_B    0xE5180
#define AUD_OUT_STR_DESC_B    0xE5184
#define AUD_CNTL_ST_B         0xE51B4
#define AUD_CONFIG_C          0xE5200
#define AUD_MISC_CTRL_C       0xE5210
#define AUD_CTS_ENABLE_C      0xE5228
#define AUD_HDMIW_HDMIEDID_C  0xE5250
#define AUD_HDMIW_INFOFR_C    0xE5254
#define AUD_OUT_DIG_CNVT_C    0xE5280
#define AUD_OUT_STR_DESC_C    0xE5284
#define AUD_CNTL_ST_C         0xE52B4
#define AUD_CONFIG_D          0xE5300
#define AUD_MISC_CTRL_D       0xE5310
#define AUD_CTS_ENABLE_D      0xE5328
#define AUD_HDMIW_HDMIEDID_D  0xE5350
#define AUD_HDMIW_INFOFR_D    0xE5354
#define AUD_OUT_DIG_CNVT_D    0xE5380
#define AUD_OUT_STR_DESC_D    0xE5384
#define AUD_CNTL_ST_D         0xE53B4

#define VIDEO_DIP_CTL_A		0xE0200
#define VIDEO_DIP_CTL_B		0xE1200
#define VIDEO_DIP_CTL_C		0xE2200
#define VIDEO_DIP_CTL_D		0xE3200


static void dump_cpt(void)
{
	uint32_t dword;
	int i;

	dump_reg(HDMIB,			"sDVO/HDMI Port B Control");
	dump_reg(HDMIC,			"HDMI Port C Control");
	dump_reg(HDMID,			"HDMI Port D Control");
	dump_reg(DP_CTL_B,			"DisplayPort B Control");
	dump_reg(DP_CTL_C,			"DisplayPort C Control");
	dump_reg(DP_CTL_D,			"DisplayPort D Control");
	dump_reg(TRANS_DP_CTL_A,		"Transcoder A DisplayPort Control");
	dump_reg(TRANS_DP_CTL_B,		"Transcoder B DisplayPort Control");
	dump_reg(TRANS_DP_CTL_C,		"Transcoder C DisplayPort Control");
	dump_reg(AUD_CONFIG_A,		"Audio Configuration - Transcoder A");
	dump_reg(AUD_CONFIG_B,		"Audio Configuration - Transcoder B");
	dump_reg(AUD_CONFIG_C,		"Audio Configuration - Transcoder C");
	dump_reg(AUD_CTS_ENABLE_A,		"Audio CTS Programming Enable - Transcoder A");
	dump_reg(AUD_CTS_ENABLE_B,		"Audio CTS Programming Enable - Transcoder B");
	dump_reg(AUD_CTS_ENABLE_C,		"Audio CTS Programming Enable - Transcoder C");
	dump_reg(AUD_MISC_CTRL_A,		"Audio MISC Control for Transcoder A");
	dump_reg(AUD_MISC_CTRL_B,		"Audio MISC Control for Transcoder B");
	dump_reg(AUD_MISC_CTRL_C,		"Audio MISC Control for Transcoder C");
	dump_reg(AUD_VID_DID,		"Audio Vendor ID / Device ID");
	dump_reg(AUD_RID,			"Audio Revision ID");
	dump_reg(AUD_PWRST,			"Audio Power State (Function Group, Convertor, Pin Widget)");
	dump_reg(AUD_PORT_EN_HD_CFG,	"Audio Port Enable HDAudio Config");
	dump_reg(AUD_OUT_DIG_CNVT_A,	"Audio Digital Converter - Conv A");
	dump_reg(AUD_OUT_DIG_CNVT_B,	"Audio Digital Converter - Conv B");
	dump_reg(AUD_OUT_DIG_CNVT_C,	"Audio Digital Converter - Conv C");
	dump_reg(AUD_OUT_CH_STR,		"Audio Channel ID and Stream ID");
	dump_reg(AUD_OUT_STR_DESC_A,	"Audio Stream Descriptor Format - Conv A");
	dump_reg(AUD_OUT_STR_DESC_B,	"Audio Stream Descriptor Format - Conv B");
	dump_reg(AUD_OUT_STR_DESC_C,	"Audio Stream Descriptor Format - Conv C");
	dump_reg(AUD_PINW_CONNLNG_LIST,	"Audio Connection List");
	dump_reg(AUD_PINW_CONNLNG_SEL,	"Audio Connection Select");
	dump_reg(AUD_CNTL_ST_A,		"Audio Control State Register - Transcoder A");
	dump_reg(AUD_CNTL_ST_B,		"Audio Control State Register - Transcoder B");
	dump_reg(AUD_CNTL_ST_C,		"Audio Control State Register - Transcoder C");
	dump_reg(AUD_CNTRL_ST2,		"Audio Control State 2");
	dump_reg(AUD_CNTRL_ST3,		"Audio Control State 3");
	dump_reg(AUD_HDMIW_STATUS,		"Audio HDMI Status");
	dump_reg(AUD_HDMIW_HDMIEDID_A,	"HDMI Data EDID Block - Transcoder A");
	dump_reg(AUD_HDMIW_HDMIEDID_B,	"HDMI Data EDID Block - Transcoder B");
	dump_reg(AUD_HDMIW_HDMIEDID_C,	"HDMI Data EDID Block - Transcoder C");
	dump_reg(AUD_HDMIW_INFOFR_A,	"Audio Widget Data Island Packet - Transcoder A");
	dump_reg(AUD_HDMIW_INFOFR_B,	"Audio Widget Data Island Packet - Transcoder B");
	dump_reg(AUD_HDMIW_INFOFR_C,	"Audio Widget Data Island Packet - Transcoder C");

	printf("\nDetails:\n\n");

	dword = INREG(VIDEO_DIP_CTL_A);
	printf("VIDEO_DIP_CTL_A Enable_Graphics_DIP\t\t\t%ld\n",     BIT(dword, 31)),
	printf("VIDEO_DIP_CTL_A GCP_DIP_enable\t\t\t\t%ld\n",     BIT(dword, 25)),
	printf("VIDEO_DIP_CTL_A Video_DIP_type_enable AVI\t\t%lu\n",       BIT(dword, 21));
	printf("VIDEO_DIP_CTL_A Video_DIP_type_enable Vendor\t\t%lu\n",      BIT(dword, 22));
	printf("VIDEO_DIP_CTL_A Video_DIP_type_enable Gamut\t\t%lu\n",       BIT(dword, 23));
	printf("VIDEO_DIP_CTL_A Video_DIP_type_enable Source \t\t%lu\n",       BIT(dword, 24));
	printf("VIDEO_DIP_CTL_A Video_DIP_buffer_index\t\t\t[0x%lx] %s\n",
			BITS(dword, 20, 19), video_dip_index[BITS(dword, 20, 19)]);
	printf("VIDEO_DIP_CTL_A Video_DIP_frequency\t\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), video_dip_trans[BITS(dword, 17, 16)]);
	printf("VIDEO_DIP_CTL_A Video_DIP_buffer_size\t\t\t%lu\n", BITS(dword, 11, 8));
	printf("VIDEO_DIP_CTL_A Video_DIP_access_address\t\t%lu\n", BITS(dword, 3, 0));

	dword = INREG(VIDEO_DIP_CTL_B);
	printf("VIDEO_DIP_CTL_B Enable_Graphics_DIP\t\t\t%ld\n",     BIT(dword, 31)),
	printf("VIDEO_DIP_CTL_B GCP_DIP_enable\t\t\t\t%ld\n",     BIT(dword, 25)),
	printf("VIDEO_DIP_CTL_B Video_DIP_type_enable AVI\t\t%lu\n",       BIT(dword, 21));
	printf("VIDEO_DIP_CTL_B Video_DIP_type_enable Vendor\t\t%lu\n",      BIT(dword, 22));
	printf("VIDEO_DIP_CTL_B Video_DIP_type_enable Gamut\t\t%lu\n",       BIT(dword, 23));
	printf("VIDEO_DIP_CTL_B Video_DIP_type_enable Source \t\t%lu\n",       BIT(dword, 24));
	printf("VIDEO_DIP_CTL_B Video_DIP_buffer_index\t\t\t[0x%lx] %s\n",
			BITS(dword, 20, 19), video_dip_index[BITS(dword, 20, 19)]);
	printf("VIDEO_DIP_CTL_B Video_DIP_frequency\t\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), video_dip_trans[BITS(dword, 17, 16)]);
	printf("VIDEO_DIP_CTL_B Video_DIP_buffer_size\t\t\t%lu\n", BITS(dword, 11, 8));
	printf("VIDEO_DIP_CTL_B Video_DIP_access_address\t\t%lu\n", BITS(dword, 3, 0));

	dword = INREG(VIDEO_DIP_CTL_C);
	printf("VIDEO_DIP_CTL_C Enable_Graphics_DIP\t\t\t%ld\n",     BIT(dword, 31)),
	printf("VIDEO_DIP_CTL_C GCP_DIP_enable\t\t\t\t%ld\n",     BIT(dword, 25)),
	printf("VIDEO_DIP_CTL_C Video_DIP_type_enable AVI\t\t%lu\n",       BIT(dword, 21));
	printf("VIDEO_DIP_CTL_C Video_DIP_type_enable Vendor\t\t%lu\n",      BIT(dword, 22));
	printf("VIDEO_DIP_CTL_C Video_DIP_type_enable Gamut\t\t%lu\n",       BIT(dword, 23));
	printf("VIDEO_DIP_CTL_C Video_DIP_type_enable Source \t\t%lu\n",       BIT(dword, 24));
	printf("VIDEO_DIP_CTL_C Video_DIP_buffer_index\t\t\t[0x%lx] %s\n",
			BITS(dword, 20, 19), video_dip_index[BITS(dword, 20, 19)]);
	printf("VIDEO_DIP_CTL_C Video_DIP_frequency\t\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), video_dip_trans[BITS(dword, 17, 16)]);
	printf("VIDEO_DIP_CTL_C Video_DIP_buffer_size\t\t\t%lu\n", BITS(dword, 11, 8));
	printf("VIDEO_DIP_CTL_C Video_DIP_access_address\t\t%lu\n", BITS(dword, 3, 0));

	dword = INREG(AUD_VID_DID);
	printf("AUD_VID_DID vendor id\t\t\t\t\t0x%x\n", dword >> 16);
	printf("AUD_VID_DID device id\t\t\t\t\t0x%x\n", dword & 0xffff);

	dword = INREG(AUD_RID);
	printf("AUD_RID Major_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 23, 20));
	printf("AUD_RID Minor_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 19, 16));
	printf("AUD_RID Revision_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 15, 8));
	printf("AUD_RID Stepping_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 7, 0));

	dword = INREG(HDMIB);
	printf("HDMIB Port_Enable\t\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
	printf("HDMIB Transcoder_Select\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 30, 29), transcoder_select[BITS(dword, 30, 29)]);
	printf("HDMIB sDVO_Border_Enable\t\t\t\t%lu\n", BIT(dword, 7));
	printf("HDMIB HDCP_Port_Select\t\t\t\t\t%lu\n", BIT(dword, 5));
	printf("HDMIB SDVO_HPD_Interrupt_Enable\t\t\t\t%lu\n", BIT(dword, 23));
	printf("HDMIB Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
	printf("HDMIB Encoding\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 11, 10), sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
	printf("HDMIB HDMI_or_DVI_Select\t\t\t\t%s\n", BIT(dword, 9) ? "HDMI" : "DVI");
	printf("HDMIB Audio_Output_Enable\t\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

	dword = INREG(HDMIC);
	printf("HDMIC Port_Enable\t\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
	printf("HDMIC Transcoder_Select\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 30, 29), transcoder_select[BITS(dword, 30, 29)]);
	printf("HDMIC sDVO_Border_Enable\t\t\t\t%lu\n", BIT(dword, 7));
	printf("HDMIC HDCP_Port_Select\t\t\t\t\t%lu\n", BIT(dword, 5));
	printf("HDMIC SDVO_HPD_Interrupt_Enable\t\t\t\t%lu\n", BIT(dword, 23));
	printf("HDMIC Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
	printf("HDMIC Encoding\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 11, 10), sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
	printf("HDMIC HDMI_or_DVI_Select\t\t\t\t%s\n", BIT(dword, 9) ? "HDMI" : "DVI");
	printf("HDMIC Audio_Output_Enable\t\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

	dword = INREG(HDMID);
	printf("HDMID Port_Enable\t\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
	printf("HDMID Transcoder_Select\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 30, 29), transcoder_select[BITS(dword, 30, 29)]);
	printf("HDMID sDVO_Border_Enable\t\t\t\t%lu\n", BIT(dword, 7));
	printf("HDMID HDCP_Port_Select\t\t\t\t\t%lu\n", BIT(dword, 5));
	printf("HDMID SDVO_HPD_Interrupt_Enable\t\t\t\t%lu\n", BIT(dword, 23));
	printf("HDMID Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
	printf("HDMID Encoding\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 11, 10), sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
	printf("HDMID HDMI_or_DVI_Select\t\t\t\t%s\n", BIT(dword, 9) ? "HDMI" : "DVI");
	printf("HDMID Audio_Output_Enable\t\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

	dword = INREG(DP_CTL_B);
	printf("DP_CTL_B DisplayPort_Enable\t\t\t\t%lu\n", BIT(dword, 31));
	printf("DP_CTL_B Port_Width_Selection\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 21, 19), dp_port_width[BITS(dword, 21, 19)]);
	printf("DP_CTL_B Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
	printf("DP_CTL_B HDCP_Port_Select\t\t\t\t%lu\n", BIT(dword, 5));
	printf("DP_CTL_B Audio_Output_Enable\t\t\t\t%lu\n", BIT(dword, 6));

	dword = INREG(DP_CTL_C);
	printf("DP_CTL_C DisplayPort_Enable\t\t\t\t%lu\n", BIT(dword, 31));
	printf("DP_CTL_C Port_Width_Selection\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 21, 19), dp_port_width[BITS(dword, 21, 19)]);
	printf("DP_CTL_C Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
	printf("DP_CTL_C HDCP_Port_Select\t\t\t\t%lu\n", BIT(dword, 5));
	printf("DP_CTL_C Audio_Output_Enable\t\t\t\t%lu\n", BIT(dword, 6));

	dword = INREG(DP_CTL_D);
	printf("DP_CTL_D DisplayPort_Enable\t\t\t\t%lu\n", BIT(dword, 31));
	printf("DP_CTL_D Port_Width_Selection\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 21, 19), dp_port_width[BITS(dword, 21, 19)]);
	printf("DP_CTL_D Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
	printf("DP_CTL_D HDCP_Port_Select\t\t\t\t%lu\n", BIT(dword, 5));
	printf("DP_CTL_D Audio_Output_Enable\t\t\t\t%lu\n", BIT(dword, 6));

	dword = INREG(AUD_CONFIG_A);
	printf("AUD_CONFIG_A  N_index_value\t\t\t\t[0x%lx] %s\n", BIT(dword, 29),
			n_index_value[BIT(dword, 29)]);
	printf("AUD_CONFIG_A  N_programming_enable\t\t\t%lu\n", BIT(dword, 28));
	printf("AUD_CONFIG_A  Upper_N_value\t\t\t\t0x%02lx\n", BITS(dword, 27, 20));
	printf("AUD_CONFIG_A  Lower_N_value\t\t\t\t0x%03lx\n", BITS(dword, 15, 4));
	printf("AUD_CONFIG_A  Pixel_Clock_HDMI\t\t\t\t[0x%lx] %s\n", BITS(dword, 19, 16),
			OPNAME(pixel_clock, BITS(dword, 19, 16)));
	printf("AUD_CONFIG_A  Disable_NCTS\t\t\t\t%lu\n", BIT(dword, 3));
	dword = INREG(AUD_CONFIG_B);
	printf("AUD_CONFIG_B  N_index_value\t\t\t\t[0x%lx] %s\n", BIT(dword, 29),
			n_index_value[BIT(dword, 29)]);
	printf("AUD_CONFIG_B  N_programming_enable\t\t\t%lu\n", BIT(dword, 28));
	printf("AUD_CONFIG_B  Upper_N_value\t\t\t\t0x%02lx\n", BITS(dword, 27, 20));
	printf("AUD_CONFIG_B  Lower_N_value\t\t\t\t0x%03lx\n", BITS(dword, 15, 4));
	printf("AUD_CONFIG_B  Pixel_Clock_HDMI\t\t\t\t[0x%lx] %s\n", BITS(dword, 19, 16),
			OPNAME(pixel_clock, BITS(dword, 19, 16)));
	printf("AUD_CONFIG_B  Disable_NCTS\t\t\t\t%lu\n", BIT(dword, 3));
	dword = INREG(AUD_CONFIG_C);
	printf("AUD_CONFIG_C  N_index_value\t\t\t\t[0x%lx] %s\n", BIT(dword, 29),
			n_index_value[BIT(dword, 29)]);
	printf("AUD_CONFIG_C  N_programming_enable\t\t\t%lu\n", BIT(dword, 28));
	printf("AUD_CONFIG_C  Upper_N_value\t\t\t\t0x%02lx\n", BITS(dword, 27, 20));
	printf("AUD_CONFIG_C  Lower_N_value\t\t\t\t0x%03lx\n", BITS(dword, 15, 4));
	printf("AUD_CONFIG_C  Pixel_Clock_HDMI\t\t\t\t[0x%lx] %s\n", BITS(dword, 19, 16),
			OPNAME(pixel_clock, BITS(dword, 19, 16)));
	printf("AUD_CONFIG_C  Disable_NCTS\t\t\t\t%lu\n", BIT(dword, 3));

	dword = INREG(AUD_CTS_ENABLE_A);
	printf("AUD_CTS_ENABLE_A  Enable_CTS_or_M_programming\t\t%lu\n", BIT(dword, 20));
	printf("AUD_CTS_ENABLE_A  CTS_M value Index\t\t\t%s\n", BIT(dword, 21) ? "CTS" : "M");
	printf("AUD_CTS_ENABLE_A  CTS_programming\t\t\t%#lx\n", BITS(dword, 19, 0));
	dword = INREG(AUD_CTS_ENABLE_B);
	printf("AUD_CTS_ENABLE_B  Enable_CTS_or_M_programming\t\t%lu\n", BIT(dword, 20));
	printf("AUD_CTS_ENABLE_B  CTS_M value Index\t\t\t%s\n", BIT(dword, 21) ? "CTS" : "M");
	printf("AUD_CTS_ENABLE_B  CTS_programming\t\t\t%#lx\n", BITS(dword, 19, 0));
	dword = INREG(AUD_CTS_ENABLE_C);
	printf("AUD_CTS_ENABLE_C  Enable_CTS_or_M_programming\t\t%lu\n", BIT(dword, 20));
	printf("AUD_CTS_ENABLE_C  CTS_M value Index\t\t\t%s\n", BIT(dword, 21) ? "CTS" : "M");
	printf("AUD_CTS_ENABLE_C  CTS_programming\t\t\t%#lx\n", BITS(dword, 19, 0));

	dword = INREG(AUD_MISC_CTRL_A);
	printf("AUD_MISC_CTRL_A  Sample_Fabrication_EN_bit\t\t%lu\n",	BIT(dword, 2));
	printf("AUD_MISC_CTRL_A  Sample_present_Disable\t\t\t%lu\n",	BIT(dword, 8));
	printf("AUD_MISC_CTRL_A  Output_Delay\t\t\t\t%lu\n",		BITS(dword, 7, 4));
	printf("AUD_MISC_CTRL_A  Pro_Allowed\t\t\t\t%lu\n",			BIT(dword, 1));
	dword = INREG(AUD_MISC_CTRL_B);
	printf("AUD_MISC_CTRL_B  Sample_Fabrication_EN_bit\t\t%lu\n",	BIT(dword, 2));
	printf("AUD_MISC_CTRL_B  Sample_present_Disable\t\t\t%lu\n",	BIT(dword, 8));
	printf("AUD_MISC_CTRL_B  Output_Delay\t\t\t\t%lu\n",		BITS(dword, 7, 4));
	printf("AUD_MISC_CTRL_B  Pro_Allowed\t\t\t\t%lu\n",			BIT(dword, 1));
	dword = INREG(AUD_MISC_CTRL_C);
	printf("AUD_MISC_CTRL_C  Sample_Fabrication_EN_bit\t\t%lu\n",	BIT(dword, 2));
	printf("AUD_MISC_CTRL_C  Sample_present_Disable\t\t\t%lu\n",	BIT(dword, 8));
	printf("AUD_MISC_CTRL_C  Output_Delay\t\t\t\t%lu\n",		BITS(dword, 7, 4));
	printf("AUD_MISC_CTRL_C  Pro_Allowed\t\t\t\t%lu\n",			BIT(dword, 1));

	dword = INREG(AUD_PWRST);
	printf("AUD_PWRST  Func_Grp_Dev_PwrSt_Curr                  \t%s\n", power_state[BITS(dword, 27, 26)]);
	printf("AUD_PWRST  Func_Grp_Dev_PwrSt_Set                   \t%s\n", power_state[BITS(dword, 25, 24)]);
	printf("AUD_PWRST  ConvertorA_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 15, 14)]);
	printf("AUD_PWRST  ConvertorA_Widget_Power_State_Requsted   \t%s\n", power_state[BITS(dword, 13, 12)]);
	printf("AUD_PWRST  ConvertorB_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 19, 18)]);
	printf("AUD_PWRST  ConvertorB_Widget_Power_State_Requested  \t%s\n", power_state[BITS(dword, 17, 16)]);
	printf("AUD_PWRST  ConvC_Widget_PwrSt_Curr                  \t%s\n", power_state[BITS(dword, 23, 22)]);
	printf("AUD_PWRST  ConvC_Widget_PwrSt_Req                   \t%s\n", power_state[BITS(dword, 21, 20)]);
	printf("AUD_PWRST  PinB_Widget_Power_State_Current          \t%s\n", power_state[BITS(dword,  3,  2)]);
	printf("AUD_PWRST  PinB_Widget_Power_State_Set              \t%s\n", power_state[BITS(dword,  1,  0)]);
	printf("AUD_PWRST  PinC_Widget_Power_State_Current          \t%s\n", power_state[BITS(dword,  7,  6)]);
	printf("AUD_PWRST  PinC_Widget_Power_State_Set              \t%s\n", power_state[BITS(dword,  5,  4)]);
	printf("AUD_PWRST  PinD_Widget_Power_State_Current          \t%s\n", power_state[BITS(dword, 11, 10)]);
	printf("AUD_PWRST  PinD_Widget_Power_State_Set              \t%s\n", power_state[BITS(dword,  9,  8)]);

	dword = INREG(AUD_PORT_EN_HD_CFG);
	printf("AUD_PORT_EN_HD_CFG  Convertor_A_Digen\t\t\t%lu\n",	BIT(dword, 0));
	printf("AUD_PORT_EN_HD_CFG  Convertor_B_Digen\t\t\t%lu\n",	BIT(dword, 1));
	printf("AUD_PORT_EN_HD_CFG  Convertor_C_Digen\t\t\t%lu\n",	BIT(dword, 2));
	printf("AUD_PORT_EN_HD_CFG  ConvertorA_Stream_ID\t\t%lu\n",	BITS(dword,  7, 4));
	printf("AUD_PORT_EN_HD_CFG  ConvertorB_Stream_ID\t\t%lu\n",	BITS(dword, 11, 8));
	printf("AUD_PORT_EN_HD_CFG  ConvertorC_Stream_ID\t\t%lu\n",	BITS(dword, 15, 12));
	printf("AUD_PORT_EN_HD_CFG  Port_B_Out_Enable\t\t\t%lu\n",	BIT(dword, 16));
	printf("AUD_PORT_EN_HD_CFG  Port_C_Out_Enable\t\t\t%lu\n",	BIT(dword, 17));
	printf("AUD_PORT_EN_HD_CFG  Port_D_Out_Enable\t\t\t%lu\n",	BIT(dword, 18));
	printf("AUD_PORT_EN_HD_CFG  Port_B_Amp_Mute_Status\t\t%lu\n", BIT(dword, 20));
	printf("AUD_PORT_EN_HD_CFG  Port_C_Amp_Mute_Status\t\t%lu\n", BIT(dword, 21));
	printf("AUD_PORT_EN_HD_CFG  Port_D_Amp_Mute_Status\t\t%lu\n", BIT(dword, 22));

	dword = INREG(AUD_OUT_DIG_CNVT_A);
	printf("AUD_OUT_DIG_CNVT_A  V\t\t\t\t\t%lu\n",		BIT(dword, 1));
	printf("AUD_OUT_DIG_CNVT_A  VCFG\t\t\t\t%lu\n",		BIT(dword, 2));
	printf("AUD_OUT_DIG_CNVT_A  PRE\t\t\t\t\t%lu\n",		BIT(dword, 3));
	printf("AUD_OUT_DIG_CNVT_A  Copy\t\t\t\t%lu\n",		BIT(dword, 4));
	printf("AUD_OUT_DIG_CNVT_A  NonAudio\t\t\t\t%lu\n",		BIT(dword, 5));
	printf("AUD_OUT_DIG_CNVT_A  PRO\t\t\t\t\t%lu\n",		BIT(dword, 6));
	printf("AUD_OUT_DIG_CNVT_A  Level\t\t\t\t%lu\n",		BIT(dword, 7));
	printf("AUD_OUT_DIG_CNVT_A  Category_Code\t\t\t%lu\n",	BITS(dword, 14, 8));
	printf("AUD_OUT_DIG_CNVT_A  Lowest_Channel_Number\t\t%lu\n", BITS(dword, 19, 16));
	printf("AUD_OUT_DIG_CNVT_A  Stream_ID\t\t\t\t%lu\n",	BITS(dword, 23, 20));

	dword = INREG(AUD_OUT_DIG_CNVT_B);
	printf("AUD_OUT_DIG_CNVT_B  V\t\t\t\t\t%lu\n",		BIT(dword, 1));
	printf("AUD_OUT_DIG_CNVT_B  VCFG\t\t\t\t%lu\n",		BIT(dword, 2));
	printf("AUD_OUT_DIG_CNVT_B  PRE\t\t\t\t\t%lu\n",		BIT(dword, 3));
	printf("AUD_OUT_DIG_CNVT_B  Copy\t\t\t\t%lu\n",		BIT(dword, 4));
	printf("AUD_OUT_DIG_CNVT_B  NonAudio\t\t\t\t%lu\n",		BIT(dword, 5));
	printf("AUD_OUT_DIG_CNVT_B  PRO\t\t\t\t\t%lu\n",		BIT(dword, 6));
	printf("AUD_OUT_DIG_CNVT_B  Level\t\t\t\t%lu\n",		BIT(dword, 7));
	printf("AUD_OUT_DIG_CNVT_B  Category_Code\t\t\t%lu\n",	BITS(dword, 14, 8));
	printf("AUD_OUT_DIG_CNVT_B  Lowest_Channel_Number\t\t%lu\n", BITS(dword, 19, 16));
	printf("AUD_OUT_DIG_CNVT_B  Stream_ID\t\t\t\t%lu\n",	BITS(dword, 23, 20));

	dword = INREG(AUD_OUT_DIG_CNVT_C);
	printf("AUD_OUT_DIG_CNVT_C  V\t\t\t\t\t%lu\n",		BIT(dword, 1));
	printf("AUD_OUT_DIG_CNVT_C  VCFG\t\t\t\t%lu\n",		BIT(dword, 2));
	printf("AUD_OUT_DIG_CNVT_C  PRE\t\t\t\t\t%lu\n",		BIT(dword, 3));
	printf("AUD_OUT_DIG_CNVT_C  Copy\t\t\t\t%lu\n",		BIT(dword, 4));
	printf("AUD_OUT_DIG_CNVT_C  NonAudio\t\t\t\t%lu\n",		BIT(dword, 5));
	printf("AUD_OUT_DIG_CNVT_C  PRO\t\t\t\t\t%lu\n",		BIT(dword, 6));
	printf("AUD_OUT_DIG_CNVT_C  Level\t\t\t\t%lu\n",		BIT(dword, 7));
	printf("AUD_OUT_DIG_CNVT_C  Category_Code\t\t\t%lu\n",	BITS(dword, 14, 8));
	printf("AUD_OUT_DIG_CNVT_C  Lowest_Channel_Number\t\t%lu\n", BITS(dword, 19, 16));
	printf("AUD_OUT_DIG_CNVT_C  Stream_ID\t\t\t\t%lu\n",	BITS(dword, 23, 20));

	printf("AUD_OUT_CH_STR  Converter_Channel_MAP	PORTB	PORTC	PORTD\n");
	for (i = 0; i < 8; i++) {
		OUTREG(AUD_OUT_CH_STR, i | (i << 8) | (i << 16));
		dword = INREG(AUD_OUT_CH_STR);
		printf("\t\t\t\t%lu\t%lu\t%lu\t%lu\n",
				1 + BITS(dword,  3,  0),
				1 + BITS(dword,  7,  4),
				1 + BITS(dword, 15, 12),
				1 + BITS(dword, 23, 20));
	}

	dword = INREG(AUD_OUT_STR_DESC_A);
	printf("AUD_OUT_STR_DESC_A  HBR_enable\t\t\t\t%lu\n",	 BITS(dword, 28, 27));
	printf("AUD_OUT_STR_DESC_A  Convertor_Channel_Count\t\t%lu\n", BITS(dword, 20, 16) + 1);
	printf("AUD_OUT_STR_DESC_A  Bits_per_Sample\t\t\t[%#lx] %s\n",
			BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));
	printf("AUD_OUT_STR_DESC_A  Number_of_Channels_in_a_Stream\t%lu\n", 1 + BITS(dword, 3, 0));

	dword = INREG(AUD_OUT_STR_DESC_B);
	printf("AUD_OUT_STR_DESC_B  HBR_enable\t\t\t\t%lu\n",	 BITS(dword, 28, 27));
	printf("AUD_OUT_STR_DESC_B  Convertor_Channel_Count\t\t%lu\n", BITS(dword, 20, 16) + 1);
	printf("AUD_OUT_STR_DESC_B  Bits_per_Sample\t\t\t[%#lx] %s\n",
			BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));
	printf("AUD_OUT_STR_DESC_B  Number_of_Channels_in_a_Stream\t%lu\n", 1 + BITS(dword, 3, 0));

	dword = INREG(AUD_OUT_STR_DESC_C);
	printf("AUD_OUT_STR_DESC_C  HBR_enable\t\t\t\t%lu\n",	 BITS(dword, 28, 27));
	printf("AUD_OUT_STR_DESC_C  Convertor_Channel_Count\t\t%lu\n", BITS(dword, 20, 16) + 1);
	printf("AUD_OUT_STR_DESC_C  Bits_per_Sample\t\t\t[%#lx] %s\n",
			BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));
	printf("AUD_OUT_STR_DESC_C  Number_of_Channels_in_a_Stream\t%lu\n", 1 + BITS(dword, 3, 0));

	dword = INREG(AUD_PINW_CONNLNG_SEL);
	printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_B\t%#lx\n", BITS(dword,  7,  0));
	printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_C\t%#lx\n", BITS(dword, 15,  8));
	printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_D\t%#lx\n", BITS(dword, 23, 16));

	dword = INREG(AUD_CNTL_ST_A);
	printf("AUD_CNTL_ST_A  DIP_Port_Select\t\t\t\t[%#lx] %s\n",
			BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);
	printf("AUD_CNTL_ST_A  DIP_type_enable_status Audio DIP\t\t%lu\n", BIT(dword, 21));
	printf("AUD_CNTL_ST_A  DIP_type_enable_status ACP DIP\t\t%lu\n", BIT(dword, 22));
	printf("AUD_CNTL_ST_A  DIP_type_enable_status Generic 2 DIP\t%lu\n", BIT(dword, 23));
	printf("AUD_CNTL_ST_A  DIP_transmission_frequency\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
	printf("AUD_CNTL_ST_A  ELD_ACK\t\t\t\t\t%lu\n", BIT(dword, 4));
	printf("AUD_CNTL_ST_A  ELD_buffer_size\t\t\t\t%lu\n", BITS(dword, 14, 10));

	dword = INREG(AUD_CNTL_ST_B);
	printf("AUD_CNTL_ST_B  DIP_Port_Select\t\t\t\t[%#lx] %s\n",
			BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);
	printf("AUD_CNTL_ST_B  DIP_type_enable_status Audio DIP\t\t%lu\n", BIT(dword, 21));
	printf("AUD_CNTL_ST_B  DIP_type_enable_status ACP DIP\t\t%lu\n", BIT(dword, 22));
	printf("AUD_CNTL_ST_B  DIP_type_enable_status Generic 2 DIP\t%lu\n", BIT(dword, 23));
	printf("AUD_CNTL_ST_B  DIP_transmission_frequency\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
	printf("AUD_CNTL_ST_B  ELD_ACK\t\t\t\t\t%lu\n", BIT(dword, 4));
	printf("AUD_CNTL_ST_B  ELD_buffer_size\t\t\t\t%lu\n", BITS(dword, 14, 10));

	dword = INREG(AUD_CNTL_ST_C);
	printf("AUD_CNTL_ST_C  DIP_Port_Select\t\t\t\t[%#lx] %s\n",
			BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);
	printf("AUD_CNTL_ST_C  DIP_type_enable_status Audio DIP\t\t%lu\n", BIT(dword, 21));
	printf("AUD_CNTL_ST_C  DIP_type_enable_status ACP DIP\t\t%lu\n", BIT(dword, 22));
	printf("AUD_CNTL_ST_C  DIP_type_enable_status Generic 2 DIP\t%lu\n", BIT(dword, 23));
	printf("AUD_CNTL_ST_C  DIP_transmission_frequency\t\t[0x%lx] %s\n",
			BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
	printf("AUD_CNTL_ST_C  ELD_ACK\t\t\t\t\t%lu\n", BIT(dword, 4));
	printf("AUD_CNTL_ST_C  ELD_buffer_size\t\t\t\t%lu\n", BITS(dword, 14, 10));

	dword = INREG(AUD_CNTRL_ST2);
	printf("AUD_CNTRL_ST2  CP_ReadyB\t\t\t\t%lu\n",	BIT(dword, 1));
	printf("AUD_CNTRL_ST2  ELD_validB\t\t\t\t%lu\n",	BIT(dword, 0));
	printf("AUD_CNTRL_ST2  CP_ReadyC\t\t\t\t%lu\n",	BIT(dword, 5));
	printf("AUD_CNTRL_ST2  ELD_validC\t\t\t\t%lu\n",	BIT(dword, 4));
	printf("AUD_CNTRL_ST2  CP_ReadyD\t\t\t\t%lu\n",	BIT(dword, 9));
	printf("AUD_CNTRL_ST2  ELD_validD\t\t\t\t%lu\n",	BIT(dword, 8));

	dword = INREG(AUD_CNTRL_ST3);
	printf("AUD_CNTRL_ST3  TransA_DPT_Audio_Output_En\t\t%lu\n",	BIT(dword, 3));
	printf("AUD_CNTRL_ST3  TransA_to_Port_Sel\t\t\t[%#lx] %s\n",
			BITS(dword, 2, 0), trans_to_port_sel[BITS(dword, 2, 0)]);
	printf("AUD_CNTRL_ST3  TransB_DPT_Audio_Output_En\t\t%lu\n",	BIT(dword, 7));
	printf("AUD_CNTRL_ST3  TransB_to_Port_Sel\t\t\t[%#lx] %s\n",
			BITS(dword, 6, 4), trans_to_port_sel[BITS(dword, 6, 4)]);
	printf("AUD_CNTRL_ST3  TransC_DPT_Audio_Output_En\t\t%lu\n",	BIT(dword, 11));
	printf("AUD_CNTRL_ST3  TransC_to_Port_Sel\t\t\t[%#lx] %s\n",
			BITS(dword, 10, 8), trans_to_port_sel[BITS(dword, 10, 8)]);

	dword = INREG(AUD_HDMIW_STATUS);
	printf("AUD_HDMIW_STATUS  Conv_A_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 27));
	printf("AUD_HDMIW_STATUS  Conv_A_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 26));
	printf("AUD_HDMIW_STATUS  Conv_B_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 29));
	printf("AUD_HDMIW_STATUS  Conv_B_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 28));
	printf("AUD_HDMIW_STATUS  Conv_C_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 31));
	printf("AUD_HDMIW_STATUS  Conv_C_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 30));
	printf("AUD_HDMIW_STATUS  BCLK/CDCLK_FIFO_Overrun\t\t%lu\n",	 BIT(dword, 25));
	printf("AUD_HDMIW_STATUS  Function_Reset\t\t\t%lu\n",		 BIT(dword, 24));

	printf("AUD_HDMIW_HDMIEDID_A HDMI ELD:\n\t");
	dword = INREG(AUD_CNTL_ST_A);
	dword &= ~BITMASK(9, 5);
	OUTREG(AUD_CNTL_ST_A, dword);
	for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID_A)));
	printf("\n");

	printf("AUD_HDMIW_HDMIEDID_B HDMI ELD:\n\t");
	dword = INREG(AUD_CNTL_ST_B);
	dword &= ~BITMASK(9, 5);
	OUTREG(AUD_CNTL_ST_B, dword);
	for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID_B)));
	printf("\n");

	printf("AUD_HDMIW_HDMIEDID_C HDMI ELD:\n\t");
	dword = INREG(AUD_CNTL_ST_C);
	dword &= ~BITMASK(9, 5);
	OUTREG(AUD_CNTL_ST_C, dword);
	for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID_C)));
	printf("\n");

	printf("AUD_HDMIW_INFOFR_A HDMI audio Infoframe:\n\t");
	dword = INREG(AUD_CNTL_ST_A);
	dword &= ~BITMASK(20, 18);
	dword &= ~BITMASK(3, 0);
	OUTREG(AUD_CNTL_ST_A, dword);
	for (i = 0; i < 8; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR_A)));
	printf("\n");

	printf("AUD_HDMIW_INFOFR_B HDMI audio Infoframe:\n\t");
	dword = INREG(AUD_CNTL_ST_B);
	dword &= ~BITMASK(20, 18);
	dword &= ~BITMASK(3, 0);
	OUTREG(AUD_CNTL_ST_B, dword);
	for (i = 0; i < 8; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR_B)));
	printf("\n");

	printf("AUD_HDMIW_INFOFR_C HDMI audio Infoframe:\n\t");
	dword = INREG(AUD_CNTL_ST_C);
	dword &= ~BITMASK(20, 18);
	dword &= ~BITMASK(3, 0);
	OUTREG(AUD_CNTL_ST_C, dword);
	for (i = 0; i < 8; i++)
		printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR_C)));
	printf("\n");

}

/* Audio config registers of Ironlake */
#undef AUD_CONFIG_A
#undef AUD_CONFIG_B
#undef AUD_MISC_CTRL_A
#undef AUD_MISC_CTRL_B
#undef AUD_VID_DID
#undef AUD_RID
#undef AUD_CTS_ENABLE_A
#undef AUD_CTS_ENABLE_B
#undef AUD_PWRST
#undef AUD_HDMIW_HDMIEDID_A
#undef AUD_HDMIW_HDMIEDID_B
#undef AUD_HDMIW_INFOFR_A
#undef AUD_HDMIW_INFOFR_B
#undef AUD_PORT_EN_HD_CFG
#undef AUD_OUT_DIG_CNVT_A
#undef AUD_OUT_DIG_CNVT_B
#undef AUD_OUT_STR_DESC_A
#undef AUD_OUT_STR_DESC_B
#undef AUD_OUT_CH_STR
#undef AUD_PINW_CONNLNG_LIST
#undef AUD_PINW_CONNLNG_SEL
#undef AUD_CNTL_ST_A
#undef AUD_CNTL_ST_B
#undef AUD_CNTL_ST2
#undef AUD_HDMIW_STATUS

#define PIPE_OFS                0x100

#define AUD_CONFIG_A            0x0
#define AUD_CONFIG_B            (AUD_CONFIG_A + PIPE_OFS)
#define AUD_MISC_CTRL_A         0x010
#define AUD_MISC_CTRL_B         (AUD_MISC_CTRL_A + PIPE_OFS)
#define AUD_VID_DID             0x020
#define AUD_RID                 0x024
#define AUD_CTS_ENABLE_A        0x028
#define AUD_CTS_ENABLE_B        (AUD_CTS_ENABLE_A + PIPE_OFS)
#define AUD_PWRST               0x04C
#define AUD_HDMIW_HDMIEDID_A    0x050
#define AUD_HDMIW_HDMIEDID_B    (AUD_HDMIW_HDMIEDID_A + PIPE_OFS)
#define AUD_HDMIW_INFOFR_A      0x054
#define AUD_HDMIW_INFOFR_B      (AUD_HDMIW_INFOFR_A + PIPE_OFS)
#define AUD_PORT_EN_HD_CFG      0x07c
#define AUD_OUT_DIG_CNVT_A      0x080
#define AUD_OUT_DIG_CNVT_B      (AUD_OUT_DIG_CNVT_A + PIPE_OFS)
#define AUD_OUT_STR_DESC_A      0x084
#define AUD_OUT_STR_DESC_B      (AUD_OUT_STR_DESC_A + PIPE_OFS)
#define AUD_OUT_CH_STR          0x088
#define AUD_PINW_CONNLNG_LIST   0x0a8
#define AUD_PINW_CONNLNG_SEL    0x0aC
#define AUD_CNTL_ST_A           0x0b4
#define AUD_CNTL_ST_B           (AUD_CNTL_ST_A + PIPE_OFS)
#define AUD_CNTL_ST2            0x0c0
#define AUD_HDMIW_STATUS        0x0d4

/* Audio config registers of Haswell+ */
#define AUD_TCA_CONFIG          AUD_CONFIG_A
#define AUD_TCB_CONFIG          (AUD_TCA_CONFIG + PIPE_OFS)
#define AUD_TCC_CONFIG          (AUD_TCA_CONFIG + PIPE_OFS * 2)
#define AUD_C1_MISC_CTRL        AUD_MISC_CTRL_A
#define AUD_C2_MISC_CTRL        (AUD_MISC_CTRL_A + PIPE_OFS)
#define AUD_C3_MISC_CTRL        (AUD_MISC_CTRL_A + PIPE_OFS * 2)
#define AUD_TCA_M_CTS_ENABLE    AUD_CTS_ENABLE_A
#define AUD_TCB_M_CTS_ENABLE    (AUD_TCA_M_CTS_ENABLE + PIPE_OFS)
#define AUD_TCC_M_CTS_ENABLE    (AUD_TCA_M_CTS_ENABLE + PIPE_OFS * 2)
#define AUD_TCA_EDID_DATA       AUD_HDMIW_HDMIEDID_A
#define AUD_TCB_EDID_DATA       (AUD_TCA_EDID_DATA + PIPE_OFS)
#define AUD_TCC_EDID_DATA       (AUD_TCA_EDID_DATA + PIPE_OFS * 2)
#define AUD_TCA_INFOFR          AUD_HDMIW_INFOFR_A
#define AUD_TCB_INFOFR          (AUD_TCA_INFOFR +  PIPE_OFS)
#define AUD_TCC_INFOFR          (AUD_TCA_INFOFR +  PIPE_OFS * 2)
#define AUD_PIPE_CONV_CFG       AUD_PORT_EN_HD_CFG
#define AUD_C1_DIG_CNVT         AUD_OUT_DIG_CNVT_A
#define AUD_C2_DIG_CNVT         (AUD_C1_DIG_CNVT + PIPE_OFS)
#define AUD_C3_DIG_CNVT         (AUD_C1_DIG_CNVT + PIPE_OFS * 2)
#define AUD_C1_STR_DESC         AUD_OUT_STR_DESC_A
#define AUD_C2_STR_DESC         (AUD_C1_STR_DESC + PIPE_OFS)
#define AUD_C3_STR_DESC         (AUD_C1_STR_DESC + PIPE_OFS * 2)
#define AUD_OUT_CHAN_MAP        AUD_OUT_CH_STR
#define AUD_TCA_PIN_PIPE_CONN_ENTRY_LNGTH       AUD_PINW_CONNLNG_LIST
#define AUD_TCB_PIN_PIPE_CONN_ENTRY_LNGTH       (AUD_TCA_PIN_PIPE_CONN_ENTRY_LNGTH + PIPE_OFS)
#define AUD_TCC_PIN_PIPE_CONN_ENTRY_LNGTH       (AUD_TCA_PIN_PIPE_CONN_ENTRY_LNGTH + PIPE_OFS * 2)
#define AUD_PIPE_CONN_SEL_CTRL  AUD_PINW_CONNLNG_SEL
#define AUD_TCA_DIP_ELD_CTRL_ST AUD_CNTL_ST_A
#define AUD_TCB_DIP_ELD_CTRL_ST (AUD_TCA_DIP_ELD_CTRL_ST +  PIPE_OFS)
#define AUD_TCC_DIP_ELD_CTRL_ST (AUD_TCA_DIP_ELD_CTRL_ST +  PIPE_OFS * 2)
#define AUD_PIN_ELD_CP_VLD      AUD_CNTL_ST2
#define AUD_HDMI_FIFO_STATUS    AUD_HDMIW_STATUS
#define AUD_ICOI                0xf00
#define AUD_IRII                0xf04
#define AUD_ICS                 0xf08
#define AUD_CHICKENBIT_REG      0xf10
#define AUD_DP_DIP_STATUS       0xf20
#define AUD_TCA_M_CTS           0xf44
#define AUD_TCB_M_CTS           0xf54
#define AUD_TCC_M_CTS           0xf64

/* Common functions to dump audio registers */
#define MAX_PREFIX_SIZE		128

static void dump_aud_config(int index)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	if (!IS_HASWELL_PLUS(devid)) {
		dword = INREG(aud_reg_base + AUD_CONFIG_A + (index - PIPE_A) * 0x100);
		sprintf(prefix, "AUD_CONFIG_%c  ", 'A' + index - PIPE_A);
	} else {
		dword = INREG(aud_reg_base + AUD_TCA_CONFIG + (index - TRANSCODER_A) * 0x100);
		sprintf(prefix, "AUD_TC%c_CONFIG", 'A' + index - TRANSCODER_A);
	}

	printf("%s  Disable_NCTS\t\t\t\t%lu\n",          prefix, BIT(dword, 3));
	printf("%s  Lower_N_value\t\t\t\t0x%03lx\n",     prefix, BITS(dword, 15, 4));
	printf("%s  Pixel_Clock_HDMI\t\t\t[0x%lx] %s\n", prefix, BITS(dword, 19, 16),
		OPNAME(pixel_clock, BITS(dword, 19, 16)));
	printf("%s  Upper_N_value\t\t\t\t0x%02lx\n",     prefix, BITS(dword, 27, 20));
	printf("%s  N_programming_enable\t\t\t%lu\n",    prefix, BIT(dword, 28));
	printf("%s  N_index_value\t\t\t\t[0x%lx] %s\n",  prefix, BIT(dword, 29),
		OPNAME(n_index_value, BIT(dword, 29)));
}

static void dump_aud_misc_control(int index)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	if (!IS_HASWELL_PLUS(devid)) {
		dword = INREG(aud_reg_base + AUD_MISC_CTRL_A + (index - PIPE_A) * 0x100);
		sprintf(prefix, "AUD_MISC_CTRL_%c ", 'A' + index - PIPE_A);
	} else {
		dword = INREG(aud_reg_base + AUD_C1_MISC_CTRL + (index - CONVERTER_1) * 0x100);
		sprintf(prefix, "AUD_C%c_MISC_CTRL", '1' + index - CONVERTER_1);
	}

	printf("%s   Pro_Allowed\t\t\t\t%lu\n",           prefix, BIT(dword, 1));
	printf("%s   Sample_Fabrication_EN_bit\t\t%lu\n", prefix, BIT(dword, 2));
	printf("%s   Output_Delay\t\t\t\t%lu\n",          prefix, BITS(dword, 7, 4));
	printf("%s   Sample_present_Disable\t\t%lu\n",    prefix, BIT(dword, 8));
}

static void dump_aud_vendor_device_id(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_VID_DID);
	printf("AUD_VID_DID device id\t\t\t\t\t0x%lx\n", BITS(dword, 15, 0));
	printf("AUD_VID_DID vendor id\t\t\t\t\t0x%lx\n", BITS(dword, 31, 16));
}

static void dump_aud_revision_id(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_RID);
	printf("AUD_RID Stepping_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 7, 0));
	printf("AUD_RID Revision_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 15, 8));
	printf("AUD_RID Minor_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 19, 16));
	printf("AUD_RID Major_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 23, 20));
}

static void dump_aud_m_cts_enable(int index)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	if (!IS_HASWELL_PLUS(devid)) {
		dword = INREG(aud_reg_base + AUD_CTS_ENABLE_A  + (index - PIPE_A) * 0x100);
		sprintf(prefix, "AUD_CTS_ENABLE_%c    ", 'A' + index - PIPE_A);
	} else {
		dword = INREG(aud_reg_base + AUD_TCA_M_CTS_ENABLE  + (index - TRANSCODER_A) * 0x100);
		sprintf(prefix, "AUD_TC%c_M_CTS_ENABLE", 'A' + index - TRANSCODER_A);
	}

	printf("%s  CTS_programming\t\t\t%#lx\n",        prefix, BITS(dword, 19, 0));
	printf("%s  Enable_CTS_or_M_programming\t%lu\n", prefix, BIT(dword, 20));
	printf("%s  CTS_M value Index\t\t\t[0x%lx] %s\n",prefix, BIT(dword, 21),
		OPNAME(cts_m_value_index, BIT(dword, 21)));
}

static void dump_aud_power_state(void)
{
	uint32_t dword;
	int num_pipes;

	dword = INREG(aud_reg_base + AUD_PWRST);
	printf("AUD_PWRST  PinB_Widget_Power_State_Set              \t%s\n",         power_state[BITS(dword,  1,  0)]);
	printf("AUD_PWRST  PinB_Widget_Power_State_Current          \t%s\n",         power_state[BITS(dword,  3,  2)]);
	printf("AUD_PWRST  PinC_Widget_Power_State_Set              \t%s\n",         power_state[BITS(dword,  5,  4)]);
	printf("AUD_PWRST  PinC_Widget_Power_State_Current          \t%s\n",         power_state[BITS(dword,  7,  6)]);
	printf("AUD_PWRST  PinD_Widget_Power_State_Set              \t%s\n",         power_state[BITS(dword,  9,  8)]);
	printf("AUD_PWRST  PinD_Widget_Power_State_Current          \t%s\n",         power_state[BITS(dword, 11, 10)]);

	if (!IS_HASWELL_PLUS(devid)) {
		printf("AUD_PWRST  ConvertorA_Widget_Power_State_Requsted   \t%s\n", power_state[BITS(dword, 13, 12)]);
		printf("AUD_PWRST  ConvertorA_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 15, 14)]);
		printf("AUD_PWRST  ConvertorB_Widget_Power_State_Requested  \t%s\n", power_state[BITS(dword, 17, 16)]);
		printf("AUD_PWRST  ConvertorB_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 19, 18)]);
	} else {
		printf("AUD_PWRST  Convertor1_Widget_Power_State_Requsted   \t%s\n", power_state[BITS(dword, 13, 12)]);
		printf("AUD_PWRST  Convertor1_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 15, 14)]);
		printf("AUD_PWRST  Convertor2_Widget_Power_State_Requested  \t%s\n", power_state[BITS(dword, 17, 16)]);
		printf("AUD_PWRST  Convertor2_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 19, 18)]);
	}

	num_pipes = get_num_pipes();
	if (num_pipes == 2) {
		printf("AUD_PWRST  Func_Grp_Dev_PwrSt_Set                   \t%s\n", power_state[BITS(dword, 21, 20)]);
		printf("AUD_PWRST  Func_Grp_Dev_PwrSt_Curr                  \t%s\n", power_state[BITS(dword, 23, 22)]);
	} else {	/* 3 pipes */
		if (!IS_HASWELL_PLUS(devid)) {
			printf("AUD_PWRST  ConvertorC_Widget_Power_State_Requested  \t%s\n", power_state[BITS(dword, 21, 20)]);
			printf("AUD_PWRST  ConvertorC_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 23, 22)]);
		} else {
			printf("AUD_PWRST  Convertor3_Widget_Power_State_Requested  \t%s\n", power_state[BITS(dword, 21, 20)]);
			printf("AUD_PWRST  Convertor3_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 23, 22)]);
		}
		printf("AUD_PWRST  Func_Grp_Dev_PwrSt_Set                   \t%s\n", power_state[BITS(dword, 25, 24)]);
		printf("AUD_PWRST  Func_Grp_Dev_PwrSt_Curr                  \t%s\n", power_state[BITS(dword, 27, 26)]);
	}
}

static void dump_aud_edid_data(int index)
{
	uint32_t dword;
	int i;
	int offset;
	int aud_ctrl_st, edid_data;

	if (IS_HASWELL_PLUS(devid)) {
		offset = (index - TRANSCODER_A) * 0x100;
		aud_ctrl_st = aud_reg_base + AUD_TCA_DIP_ELD_CTRL_ST + offset;
		edid_data =  aud_reg_base + AUD_TCA_EDID_DATA + offset;
		printf("AUD_TC%c_EDID_DATA ELD:\n\t",  'A' + index - TRANSCODER_A);
	} else {
		offset = (index - PIPE_A) * 0x100;
		aud_ctrl_st = aud_reg_base + AUD_CNTL_ST_A + offset;
		edid_data =  aud_reg_base + AUD_HDMIW_HDMIEDID_A + offset;
		printf("AUD_HDMIW_HDMIEDID_%c HDMI ELD:\n\t",  'A' + index - PIPE_A);
	}

	dword = INREG(aud_ctrl_st);
	dword &= ~BITMASK(9, 5);
	OUTREG(aud_ctrl_st, dword);
	for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
		printf("%08x ", htonl(INREG(edid_data)));
	printf("\n");
}

static void dump_aud_infoframe(int index)
{
	uint32_t dword;
	int i;
	int offset;
	int aud_ctrl_st, info_frm;

	if (IS_HASWELL_PLUS(devid))  {
		offset = (index - TRANSCODER_A) * 0x100;
		aud_ctrl_st = aud_reg_base + AUD_TCA_DIP_ELD_CTRL_ST + offset;
		info_frm = aud_reg_base + AUD_TCA_INFOFR + offset;
		printf("AUD_TC%c_INFOFR audio Infoframe:\n\t",  'A' + index - TRANSCODER_A);
	} else {
		offset = (index - PIPE_A) * 0x100;
		aud_ctrl_st = aud_reg_base + AUD_CNTL_ST_A + offset;
		info_frm = aud_reg_base + AUD_HDMIW_INFOFR_A + offset;
		printf("AUD_HDMIW_INFOFR_%c HDMI audio Infoframe:\n\t",  'A' + index - PIPE_A);
	}

	dword = INREG(aud_ctrl_st);
	dword &= ~BITMASK(20, 18);
	dword &= ~BITMASK(3, 0);
	OUTREG(aud_ctrl_st, dword);
	for (i = 0; i < 8; i++)
		printf("%08x ", htonl(INREG(info_frm)));
	printf("\n");
}

static void dump_aud_port_en_hd_cfg(void)
{
	uint32_t dword;
	int num_pipes = get_num_pipes();

	dword = INREG(aud_reg_base + AUD_PORT_EN_HD_CFG);
	if (num_pipes == 2) {
		printf("AUD_PORT_EN_HD_CFG  Convertor_A_Digen\t\t\t%lu\n",    BIT(dword, 0));
		printf("AUD_PORT_EN_HD_CFG  Convertor_B_Digen\t\t\t%lu\n",    BIT(dword, 1));
		printf("AUD_PORT_EN_HD_CFG  Convertor_A_Stream_ID\t\t%lu\n",  BITS(dword,  7, 4));
		printf("AUD_PORT_EN_HD_CFG  Convertor_B_Stream_ID\t\t%lu\n",  BITS(dword, 11, 8));

		printf("AUD_PORT_EN_HD_CFG  Port_B_Out_Enable\t\t\t%lu\n",    BIT(dword, 12));
		printf("AUD_PORT_EN_HD_CFG  Port_C_Out_Enable\t\t\t%lu\n",    BIT(dword, 13));
		printf("AUD_PORT_EN_HD_CFG  Port_D_Out_Enable\t\t\t%lu\n",    BIT(dword, 14));
		printf("AUD_PORT_EN_HD_CFG  Port_B_Amp_Mute_Status\t\t%lu\n", BIT(dword, 16));
		printf("AUD_PORT_EN_HD_CFG  Port_C_Amp_Mute_Status\t\t%lu\n", BIT(dword, 17));
		printf("AUD_PORT_EN_HD_CFG  Port_D_Amp_Mute_Status\t\t%lu\n", BIT(dword, 18));
	} else { /* three pipes */
		printf("AUD_PORT_EN_HD_CFG  Convertor_A_Digen\t\t\t%lu\n",    BIT(dword, 0));
		printf("AUD_PORT_EN_HD_CFG  Convertor_B_Digen\t\t\t%lu\n",    BIT(dword, 1));
		printf("AUD_PORT_EN_HD_CFG  Convertor_C_Digen\t\t\t%lu\n",    BIT(dword, 2));
		printf("AUD_PORT_EN_HD_CFG  Convertor_A_Stream_ID\t\t%lu\n",  BITS(dword,  7, 4));
		printf("AUD_PORT_EN_HD_CFG  Convertor_B_Stream_ID\t\t%lu\n",  BITS(dword, 11, 8));
		printf("AUD_PORT_EN_HD_CFG  Convertor_C_Stream_ID\t\t%lu\n",  BITS(dword, 15, 12));

		printf("AUD_PORT_EN_HD_CFG  Port_B_Out_Enable\t\t\t%lu\n",    BIT(dword, 16));
		printf("AUD_PORT_EN_HD_CFG  Port_C_Out_Enable\t\t\t%lu\n",    BIT(dword, 17));
		printf("AUD_PORT_EN_HD_CFG  Port_D_Out_Enable\t\t\t%lu\n",    BIT(dword, 18));
		printf("AUD_PORT_EN_HD_CFG  Port_B_Amp_Mute_Status\t\t%lu\n", BIT(dword, 20));
		printf("AUD_PORT_EN_HD_CFG  Port_C_Amp_Mute_Status\t\t%lu\n", BIT(dword, 21));
		printf("AUD_PORT_EN_HD_CFG  Port_D_Amp_Mute_Status\t\t%lu\n", BIT(dword, 22));
	}
}

static void dump_aud_pipe_conv_cfg(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_PIPE_CONV_CFG);
	printf("AUD_PIPE_CONV_CFG  Convertor_1_Digen\t\t\t%lu\n",    BIT(dword, 0));
	printf("AUD_PIPE_CONV_CFG  Convertor_2_Digen\t\t\t%lu\n",    BIT(dword, 1));
	printf("AUD_PIPE_CONV_CFG  Convertor_3_Digen\t\t\t%lu\n",    BIT(dword, 2));
	printf("AUD_PIPE_CONV_CFG  Convertor_1_Stream_ID\t\t%lu\n",  BITS(dword,  7, 4));
	printf("AUD_PIPE_CONV_CFG  Convertor_2_Stream_ID\t\t%lu\n",  BITS(dword, 11, 8));
	printf("AUD_PIPE_CONV_CFG  Convertor_3_Stream_ID\t\t%lu\n",  BITS(dword, 15, 12));

	printf("AUD_PIPE_CONV_CFG  Port_B_Out_Enable\t\t\t%lu\n",    BIT(dword, 16));
	printf("AUD_PIPE_CONV_CFG  Port_C_Out_Enable\t\t\t%lu\n",    BIT(dword, 17));
	printf("AUD_PIPE_CONV_CFG  Port_D_Out_Enable\t\t\t%lu\n",    BIT(dword, 18));
	printf("AUD_PIPE_CONV_CFG  Port_B_Amp_Mute_Status\t\t%lu\n", BIT(dword, 20));
	printf("AUD_PIPE_CONV_CFG  Port_C_Amp_Mute_Status\t\t%lu\n", BIT(dword, 21));
	printf("AUD_PIPE_CONV_CFG  Port_D_Amp_Mute_Status\t\t%lu\n", BIT(dword, 22));
}

static void dump_aud_dig_cnvt(int index)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	if (!IS_HASWELL_PLUS(devid)) {
		dword = INREG(aud_reg_base + AUD_OUT_DIG_CNVT_A  + (index - PIPE_A) * 0x100);
		sprintf(prefix, "AUD_OUT_DIG_CNVT_%c", 'A' + index - PIPE_A);
	} else {
		dword = INREG(aud_reg_base + AUD_C1_DIG_CNVT + (index - CONVERTER_1) * 0x100);
		sprintf(prefix, "AUD_C%c_DIG_CNVT   ", '1' + index - CONVERTER_1);
	}

	printf("%s  V\t\t\t\t\t%lu\n",               prefix, BIT(dword, 1));
	printf("%s  VCFG\t\t\t\t%lu\n",              prefix, BIT(dword, 2));
	printf("%s  PRE\t\t\t\t\t%lu\n",             prefix, BIT(dword, 3));
	printf("%s  Copy\t\t\t\t%lu\n",              prefix, BIT(dword, 4));
	printf("%s  NonAudio\t\t\t\t%lu\n",          prefix, BIT(dword, 5));
	printf("%s  PRO\t\t\t\t\t%lu\n",             prefix, BIT(dword, 6));
	printf("%s  Level\t\t\t\t%lu\n",             prefix, BIT(dword, 7));
	printf("%s  Category_Code\t\t\t%lu\n",       prefix, BITS(dword, 14, 8));
	printf("%s  Lowest_Channel_Number\t\t%lu\n", prefix, BITS(dword, 19, 16));
	printf("%s  Stream_ID\t\t\t\t%lu\n",         prefix, BITS(dword, 23, 20));
}

static void dump_aud_str_desc(int index)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];
	uint32_t rate;

	if (!IS_HASWELL_PLUS(devid)) {
		dword = INREG(aud_reg_base + AUD_OUT_STR_DESC_A + (index - PIPE_A) * 0x100);
		sprintf(prefix, "AUD_OUT_STR_DESC_%c", 'A' + index - PIPE_A);
	} else {
		dword = INREG(aud_reg_base + AUD_C1_STR_DESC + (index - CONVERTER_1) * 0x100);
		sprintf(prefix, "AUD_C%c_STR_DESC  ", '1' + index - CONVERTER_1);
	}

	printf("%s  Number_of_Channels_in_a_Stream\t%lu\n",   prefix, BITS(dword, 3, 0) + 1);
	printf("%s  Bits_per_Sample\t\t\t[%#lx] %s\n",        prefix, BITS(dword, 6, 4),
		OPNAME(bits_per_sample, BITS(dword, 6, 4)));

	printf("%s  Sample_Base_Rate_Divisor\t\t[%#lx] %s\n", prefix, BITS(dword, 10, 8),
		OPNAME(sample_base_rate_divisor, BITS(dword, 10, 8)));
	printf("%s  Sample_Base_Rate_Mult\t\t[%#lx] %s\n",    prefix, BITS(dword, 13, 11),
		OPNAME(sample_base_rate_mult, BITS(dword, 13, 11)));
	printf("%s  Sample_Base_Rate\t\t\t[%#lx] %s\t",       prefix, BIT(dword, 14),
		OPNAME(sample_base_rate, BIT(dword, 14)));
	rate = (BIT(dword, 14) ? 44100 : 48000) * (BITS(dword, 13, 11) + 1)
		/(BITS(dword, 10, 8) + 1);
	printf("=> Sample Rate %d Hz\n", rate);

	printf("%s  Convertor_Channel_Count\t\t%lu\n",        prefix, BITS(dword, 20, 16) + 1);

	if (!IS_HASWELL_PLUS(devid))
		printf("%s  HBR_enable\t\t\t\t%lu\n",         prefix, BITS(dword, 28, 27));
}

#define dump_aud_out_ch_str		dump_aud_out_chan_map
static void dump_aud_out_chan_map(void)
{
	uint32_t dword;
	int i;

	printf("AUD_OUT_CHAN_MAP  Converter_Channel_MAP	PORTB	PORTC	PORTD\n");
	for (i = 0; i < 8; i++) {
		OUTREG(aud_reg_base + AUD_OUT_CHAN_MAP, i | (i << 8) | (i << 16));
		dword = INREG(aud_reg_base + AUD_OUT_CHAN_MAP);
		printf("\t\t\t\t%lu\t%lu\t%lu\t%lu\n",
				1 + BITS(dword,  3,  0),
				1 + BITS(dword,  7,  4),
				1 + BITS(dword, 15, 12),
				1 + BITS(dword, 23, 20));
	}
}

static void dump_aud_connect_list(void)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	dword = INREG(aud_reg_base + AUD_PINW_CONNLNG_LIST);
	sprintf(prefix, "AUD_PINW_CONNLNG_LIST");

	printf("%s  Connect_List_Length\t\t%lu\n",     prefix, BITS(dword, 6, 0));
	printf("%s  Form \t\t\t\t[%#lx] %s\n",         prefix, BIT(dword, 7),
		OPNAME(connect_list_form, BIT(dword, 7)));
	printf("%s  Connect_List_Entry\t\t%lu, %lu\n", prefix, BITS(dword, 15, 8), BITS(dword, 23, 16));
}

static void dump_aud_connect_select(void)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	if (IS_HASWELL_PLUS(devid)) {
		dword = INREG(aud_reg_base + AUD_PIPE_CONN_SEL_CTRL);
		sprintf(prefix, "AUD_PIPE_CONN_SEL_CTRL");

	} else {
		dword = INREG(aud_reg_base + AUD_PINW_CONNLNG_SEL);
		sprintf(prefix, "AUD_PINW_CONNLNG_SEL  ");
	}

	printf("%s  Connection_select_Port_B\t%#lx\n", prefix, BITS(dword,  7,  0));
	printf("%s  Connection_select_Port_C\t%#lx\n", prefix, BITS(dword, 15,  8));
	printf("%s  Connection_select_Port_D\t%#lx\n", prefix, BITS(dword, 23, 16));
}

static void dump_aud_ctrl_state(int index)
{
	uint32_t dword;
	int offset;

	if (IS_HASWELL_PLUS(devid)) {
		offset = (index - TRANSCODER_A) * 0x100;
		dword = INREG(aud_reg_base + AUD_TCA_DIP_ELD_CTRL_ST + offset);
		printf("Audio DIP and ELD control state for Transcoder %c\n",  'A' + index - TRANSCODER_A);
	} else {
		offset = (index - PIPE_A) * 0x100;
		dword = INREG(aud_reg_base + AUD_CNTL_ST_A + offset);
		printf("Audio control state - Pipe %c\n",  'A' + index - PIPE_A);
	}

	printf("\tELD_ACK\t\t\t\t\t\t%lu\n",                                 BIT(dword, 4));
	printf("\tELD_buffer_size\t\t\t\t\t%lu\n",                           BITS(dword, 14, 10));
	printf("\tDIP_transmission_frequency\t\t\t[0x%lx] %s\n",             BITS(dword, 17, 16),
		dip_trans[BITS(dword, 17, 16)]);
	printf("\tDIP Buffer Index \t\t\t\t[0x%lx] %s\n",                    BITS(dword, 20, 18),
		dip_index[BITS(dword, 20, 18)]);
	printf("\tAudio DIP type enable status\t\t\t[0x%04lx] %s, %s, %s\n", BITS(dword, 24, 21),
		dip_type[BIT(dword, 21)], dip_gen1_state[BIT(dword, 22)],  dip_gen2_state[BIT(dword, 23)]);
	printf("\tAudio DIP port select\t\t\t\t[0x%lx] %s\n",                BITS(dword, 30, 29),
		dip_port[BITS(dword, 30, 29)]);
	printf("\n");
}

static void dump_aud_ctrl_state2(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_CNTL_ST2);
	printf("AUD_CNTL_ST2  ELD_validB\t\t\t\t%lu\n",  BIT(dword, 0));
	printf("AUD_CNTL_ST2  CP_ReadyB\t\t\t\t\t%lu\n", BIT(dword, 1));
	printf("AUD_CNTL_ST2  ELD_validC\t\t\t\t%lu\n",  BIT(dword, 4));
	printf("AUD_CNTL_ST2  CP_ReadyC\t\t\t\t\t%lu\n", BIT(dword, 5));
	printf("AUD_CNTL_ST2  ELD_validD\t\t\t\t%lu\n",  BIT(dword, 8));
	printf("AUD_CNTL_ST2  CP_ReadyD\t\t\t\t\t%lu\n", BIT(dword, 9));
}

/* for hsw+ */
static void dump_aud_eld_cp_vld(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_PIN_ELD_CP_VLD);
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_A ELD_valid\t\t%lu\n",	BIT(dword, 0));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_A CP_Ready \t\t%lu\n",	BIT(dword, 1));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_A Out_enable\t\t%lu\n",	BIT(dword, 2));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_A Inactive\t\t%lu\n",	BIT(dword, 3));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_B ELD_valid\t\t%lu\n",	BIT(dword, 4));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_B CP_Ready\t\t%lu\n",	BIT(dword, 5));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_B OUT_enable\t\t%lu\n",	BIT(dword, 6));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_B Inactive\t\t%lu\n",    BIT(dword, 7));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_C ELD_valid\t\t%lu\n",	BIT(dword, 8));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_C CP_Ready\t\t%lu\n",	BIT(dword, 9));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_C OUT_enable\t\t%lu\n",	BIT(dword, 10));
	printf("AUD_PIN_ELD_CP_VLD  Transcoder_C Inactive\t\t%lu\n",    BIT(dword, 11));
}

static void dump_aud_hdmi_status(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_HDMIW_STATUS);
	printf("AUD_HDMIW_STATUS  Function_Reset\t\t\t%lu\n",                BIT(dword, 24));
	printf("AUD_HDMIW_STATUS  BCLK/CDCLK_FIFO_Overrun\t\t%lu\n",	     BIT(dword, 25));
	printf("AUD_HDMIW_STATUS  Conv_A_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 28));
	printf("AUD_HDMIW_STATUS  Conv_A_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 29));
	printf("AUD_HDMIW_STATUS  Conv_B_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 30));
	printf("AUD_HDMIW_STATUS  Conv_B_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 31));
}

/*
 * Display registers of Ironlake and Valleyview
 */
#undef DP_CTL_B
#undef DP_CTL_C
#undef DP_CTL_D

#define DP_CTL_B           0x4100
#define DP_CTL_C           0x4200
#define DP_CTL_D           0x4300

/* ILK HDMI port ctrl */
#define HDMI_CTL_B         0x1140
#define HDMI_CTL_C         0x1150
#define HDMI_CTL_D         0x1160

/* VLV HDMI port ctrl */
#define SDVO_HDMI_CTL_B    0x1140
#define SDVO_HDMI_CTL_C    0x1160

static void dump_dp_port_ctrl(int port)
{
	uint32_t dword;
	int port_ctrl;
	char prefix[MAX_PREFIX_SIZE];

	sprintf(prefix, "DP_%c", 'B' + port - PORT_B);

	port_ctrl = disp_reg_base + DP_CTL_B + (port - PORT_B) * 0x100;
	dword = INREG(port_ctrl);
	printf("%s DisplayPort_Enable\t\t\t\t\t%lu\n",        prefix, BIT(dword, 31));
	printf("%s Transcoder_Select\t\t\t\t\t%s\n",          prefix, BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
	printf("%s Port_Width_Selection\t\t\t\t[0x%lx] %s\n", prefix, BITS(dword, 21, 19),
		dp_port_width[BITS(dword, 21, 19)]);
	printf("%s Port_Detected\t\t\t\t\t%lu\n",             prefix, BIT(dword, 2));
	printf("%s HDCP_Port_Select\t\t\t\t\t%lu\n",          prefix, BIT(dword, 5));
	printf("%s Audio_Output_Enable\t\t\t\t%lu\n",         prefix, BIT(dword, 6));
}

static void dump_hdmi_port_ctrl(int port)
{
	uint32_t dword;
	int port_ctrl;
	char prefix[MAX_PREFIX_SIZE];

	if (IS_VALLEYVIEW(devid)) {
		sprintf(prefix, "SDVO/HDMI%c", 'B' + port - PORT_B);
		port_ctrl = disp_reg_base + SDVO_HDMI_CTL_B + (port - PORT_B) * 0x20;
	} else {
		sprintf(prefix, "HDMI%c     ", 'B' + port - PORT_B);
		port_ctrl = disp_reg_base + HDMI_CTL_B + (port - PORT_B) * 0x10;
	}

	dword = INREG(port_ctrl);
	printf("%s HDMI_Enable\t\t\t\t\t%u\n",                 prefix, !!(dword & SDVO_ENABLE));
	printf("%s Transcoder_Select\t\t\t\t%s\n",             prefix, BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
	printf("%s HDCP_Port_Select\t\t\t\t%lu\n",             prefix, BIT(dword, 5));
	if (port == PORT_B) /* TODO: check spec, not found in Ibx b-spec, and only for port B? */
		printf("%s SDVO Hot Plug Interrupt Detect Enable\t%lu\n", prefix, BIT(dword, 23));
	printf("%s Digital_Port_Detected\t\t\t%lu\n",          prefix, BIT(dword, 2));
	printf("%s Encoding\t\t\t\t\t[0x%lx] %s\n",            prefix, BITS(dword, 11, 10),
		sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
	printf("%s Null_packets_enabled_during_Vsync\t\t%u\n", prefix, !!(dword & SDVO_NULL_PACKETS_DURING_VSYNC));
	printf("%s Audio_Output_Enable\t\t\t\t%u\n",           prefix, !!(dword & SDVO_AUDIO_ENABLE));
}

static void dump_ironlake(void)
{
	uint32_t dword;

	if (!IS_VALLEYVIEW(devid))
		set_reg_base(0xe0000, 0x2000);   /* ironlake */
	else
		set_reg_base(0x60000 + VLV_DISPLAY_BASE, 0x2000);

	if (!IS_VALLEYVIEW(devid)) {
		dump_disp_reg(HDMI_CTL_B,       "sDVO/HDMI Port B Control");
		dump_disp_reg(HDMI_CTL_C,       "HDMI Port C Control");
		dump_disp_reg(HDMI_CTL_D,       "HDMI Port D Control");
	} else {
		dump_disp_reg(SDVO_HDMI_CTL_B,  "sDVO/HDMI Port B Control");
		dump_disp_reg(SDVO_HDMI_CTL_C,  "sDVO/HDMI Port C Control");
	}

	dump_disp_reg(DP_CTL_B,                 "DisplayPort B Control Register");
	dump_disp_reg(DP_CTL_C,                 "DisplayPort C Control Register");
	if (!IS_VALLEYVIEW(devid))
		dump_disp_reg(DP_CTL_D,         "DisplayPort D Control Register");

	dump_aud_reg(AUD_CONFIG_A,              "Audio Configuration - Transcoder A");
	dump_aud_reg(AUD_CONFIG_B,              "Audio Configuration - Transcoder B");
	dump_aud_reg(AUD_CTS_ENABLE_A,          "Audio CTS Programming Enable - Transcoder A");
	dump_aud_reg(AUD_CTS_ENABLE_B,          "Audio CTS Programming Enable - Transcoder B");
	dump_aud_reg(AUD_MISC_CTRL_A,           "Audio MISC Control for Transcoder A");
	dump_aud_reg(AUD_MISC_CTRL_B,           "Audio MISC Control for Transcoder B");
	dump_aud_reg(AUD_VID_DID,               "Audio Vendor ID / Device ID");
	dump_aud_reg(AUD_RID,                   "Audio Revision ID");
	dump_aud_reg(AUD_PWRST,                 "Audio Power State (Function Group, Convertor, Pin Widget)");
	dump_aud_reg(AUD_PORT_EN_HD_CFG,        "Audio Port Enable HDAudio Config");
	dump_aud_reg(AUD_OUT_DIG_CNVT_A,        "Audio Digital Converter - Conv A");
	dump_aud_reg(AUD_OUT_DIG_CNVT_B,        "Audio Digital Converter - Conv B");
	dump_aud_reg(AUD_OUT_CH_STR,            "Audio Channel ID and Stream ID");
	dump_aud_reg(AUD_OUT_STR_DESC_A,        "Audio Stream Descriptor Format - Conv A");
	dump_aud_reg(AUD_OUT_STR_DESC_B,        "Audio Stream Descriptor Format - Conv B");
	dump_aud_reg(AUD_PINW_CONNLNG_LIST,     "Audio Connection List");
	dump_aud_reg(AUD_PINW_CONNLNG_SEL,      "Audio Connection Select");
	dump_aud_reg(AUD_CNTL_ST_A,             "Audio Control State Register - Transcoder A");
	dump_aud_reg(AUD_CNTL_ST_B,             "Audio Control State Register - Transcoder B");
	dump_aud_reg(AUD_CNTL_ST2,              "Audio Control State 2");
	dump_aud_reg(AUD_HDMIW_STATUS,          "Audio HDMI Status");
	dump_aud_reg(AUD_HDMIW_HDMIEDID_A,      "HDMI Data EDID Block - Transcoder A");
	dump_aud_reg(AUD_HDMIW_HDMIEDID_B,      "HDMI Data EDID Block - Transcoder B");
	dump_aud_reg(AUD_HDMIW_INFOFR_A,        "Audio Widget Data Island Packet - Transcoder A");
	dump_aud_reg(AUD_HDMIW_INFOFR_B,        "Audio Widget Data Island Packet - Transcoder B");

	printf("\nDetails:\n\n");

	dump_aud_vendor_device_id();
	dump_aud_revision_id();

	dump_hdmi_port_ctrl(PORT_B);
	dump_hdmi_port_ctrl(PORT_C);
	if (!IS_VALLEYVIEW(devid))
		dump_hdmi_port_ctrl(PORT_D);

	dump_dp_port_ctrl(PORT_B);
	dump_dp_port_ctrl(PORT_C);
	if (!IS_VALLEYVIEW(devid))
		dump_dp_port_ctrl(PORT_D);

	dump_aud_config(PIPE_A);
	dump_aud_config(PIPE_B);

	dump_aud_m_cts_enable(PIPE_A);
	dump_aud_m_cts_enable(PIPE_B);

	dump_aud_misc_control(PIPE_A);
	dump_aud_misc_control(PIPE_B);

	dump_aud_power_state();
	dump_aud_port_en_hd_cfg();

	dump_aud_dig_cnvt(PIPE_A);
	dump_aud_dig_cnvt(PIPE_B);

	dump_aud_out_ch_str();

	dump_aud_str_desc(PIPE_A);
	dump_aud_str_desc(PIPE_B);

	dump_aud_connect_list();
	dump_aud_connect_select();

	dump_aud_ctrl_state(PIPE_A);
	dump_aud_ctrl_state(PIPE_B);
	dump_aud_ctrl_state2();

	dump_aud_hdmi_status();

	dump_aud_edid_data(PIPE_A);
	dump_aud_edid_data(PIPE_B);

	dump_aud_infoframe(PIPE_A);
	dump_aud_infoframe(PIPE_B);
}

#undef VIDEO_DIP_CTL_A
#undef VIDEO_DIP_CTL_B
#undef VIDEO_DIP_CTL_C
#undef VIDEO_DIP_CTL_D
#undef VIDEO_DIP_DATA

/*
 * Haswell+ display registers
 */

/* DisplayPort Transport Control */
#define DP_TP_CTL_A           0x64040
#define DP_TP_CTL_B           0x64140
#define DP_TP_CTL_C           0x64240
#define DP_TP_CTL_D           0x64340
#define DP_TP_CTL_E           0x64440

/* DisplayPort Transport Status */
#define DP_TP_ST_A            0x64044
#define DP_TP_ST_B            0x64144
#define DP_TP_ST_C            0x64244
#define DP_TP_ST_D            0x64344
#define DP_TP_ST_E            0x64444

/* DDI Buffer Control */
#define DDI_BUF_CTL_A         0x64000
#define DDI_BUF_CTL_B         0x64100
#define DDI_BUF_CTL_C         0x64200
#define DDI_BUF_CTL_D         0x64300
#define DDI_BUF_CTL_E         0x64400

/* DDI Buffer Translation */
#define DDI_BUF_TRANS_A       0x64e00
#define DDI_BUF_TRANS_B       0x64e60
#define DDI_BUF_TRANS_C       0x64ec0
#define DDI_BUF_TRANS_D       0x64f20
#define DDI_BUF_TRANS_E       0x64f80

/* DDI Aux Channel */
#define DDI_AUX_CHANNEL_CTRL  0x64010
#define DDI_AUX_DATA          0x64014
#define DDI_AUX_TST           0x64028

/* DDI CRC Control */
#define DDI_CRC_CTL_A         0x64050
#define DDI_CRC_CTL_B         0x64150
#define DDI_CRC_CTL_C         0x64250
#define DDI_CRC_CTL_D         0x64350
#define DDI_CRC_CTL_E         0x64450

/* Pipe DDI Function Control */
#define PIPE_DDI_FUNC_CTL_A   0x60400
#define PIPE_DDI_FUNC_CTL_B   0x61400
#define PIPE_DDI_FUNC_CTL_C   0x62400
#define PIPE_DDI_FUNC_CTL_EDP 0x6F400

/* Pipe Configuration */
#define PIPE_CONF_A           0x70008
#define PIPE_CONF_B           0x71008
#define PIPE_CONF_C           0x72008
#define PIPE_CONF_EDP         0x7F008

/* Video DIP Control */
#define VIDEO_DIP_CTL_A       0x60200
#define VIDEO_DIP_CTL_B       0x61200
#define VIDEO_DIP_CTL_C       0x62200
#define VIDEO_DIP_CTL_D       0x63200

#define VIDEO_DIP_DATA        0x60220
#define VIDEO_DIP_ECC         0x60240

static void dump_ddi_buf_ctl(int port)
{
	uint32_t dword;

	dword = INREG(DDI_BUF_CTL_A + (port - PORT_A) * 0x100);
	printf("DDI %c Buffer control\n", 'A' + port - PORT_A);

	printf("\tDP port width\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 3, 1),
		OPNAME(dp_port_width, BITS(dword, 3, 1)));
	printf("\tDDI Buffer Enable\t\t\t\t%ld\n",      BIT(dword, 31));
}

static void dump_ddi_func_ctl(int pipe)
{
	uint32_t dword;

	dword = INREG(PIPE_DDI_FUNC_CTL_A + (pipe - PIPE_A) * 0x1000);
	printf("Pipe %c DDI Function Control\n", 'A' + pipe - PIPE_A);

	printf("\tBITS per color\t\t\t\t\t[0x%lx] %s\n",    BITS(dword, 22, 20),
		OPNAME(bits_per_color, BITS(dword, 22, 20)));
	printf("\tPIPE DDI Mode\t\t\t\t\t[0x%lx] %s\n",     BITS(dword, 26, 24),
		OPNAME(ddi_mode, BITS(dword, 26, 24)));
	printf("\tPIPE DDI selection\t\t\t\t[0x%lx] %s\n",  BITS(dword, 30, 28),
		OPNAME(trans_to_port_sel, BITS(dword, 30, 28)));
	printf("\tPIPE DDI Function Enable\t\t\t[0x%lx]\n", BIT(dword, 31));
}

static void dump_aud_connect_list_entry_length(int transcoder)
{
	uint32_t dword;
	char prefix[MAX_PREFIX_SIZE];

	dword = INREG(aud_reg_base + AUD_TCA_PIN_PIPE_CONN_ENTRY_LNGTH + (transcoder - TRANSCODER_A) * 0x100);
	sprintf(prefix, "AUD_TC%c_PIN_PIPE_CONN_ENTRY_LNGTH", 'A' + transcoder - TRANSCODER_A);

	printf("%s  Connect_List_Length\t%lu\n", prefix, BITS(dword, 6, 0));
	printf("%s  Form \t\t[%#lx] %s\n",       prefix, BIT(dword, 7),
		OPNAME(connect_list_form, BIT(dword, 7)));
	printf("%s  Connect_List_Entry\t%lu\n",  prefix, BITS(dword, 15, 8));
}

static void dump_aud_connect_select_ctrl(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_PIPE_CONN_SEL_CTRL);
	printf("AUD_PIPE_CONN_SEL_CTRL  Connection_select_Port_B\t%#lx\n", BITS(dword,  7,  0));
	printf("AUD_PIPE_CONN_SEL_CTRL  Connection_select_Port_C\t%#lx\n", BITS(dword, 15,  8));
	printf("AUD_PIPE_CONN_SEL_CTRL  Connection_select_Port_D\t%#lx\n", BITS(dword, 23, 16));
}

static void dump_aud_dip_eld_ctrl_st(int transcoder)
{
	uint32_t dword;
	int offset = (transcoder - TRANSCODER_A) * 0x100;

	dword = INREG(aud_reg_base + AUD_TCA_DIP_ELD_CTRL_ST + offset);
	printf("Audio DIP and ELD control state for Transcoder %c\n",  'A' + transcoder - TRANSCODER_A);

	printf("\tELD_ACK\t\t\t\t\t\t%lu\n",                                 BIT(dword, 4));
	printf("\tELD_buffer_size\t\t\t\t\t%lu\n",                           BITS(dword, 14, 10));
	printf("\tDIP_transmission_frequency\t\t\t[0x%lx] %s\n",             BITS(dword, 17, 16),
		dip_trans[BITS(dword, 17, 16)]);
	printf("\tDIP Buffer Index \t\t\t\t[0x%lx] %s\n",                    BITS(dword, 20, 18),
		dip_index[BITS(dword, 20, 18)]);
	printf("\tAudio DIP type enable status\t\t\t[0x%04lx] %s, %s, %s\n", BITS(dword, 24, 21),
		dip_type[BIT(dword, 21)], dip_gen1_state[BIT(dword, 22)],  dip_gen2_state[BIT(dword, 23)]);
	printf("\tAudio DIP port select\t\t\t\t[0x%lx] %s\n",                BITS(dword, 30, 29),
		dip_port[BITS(dword, 30, 29)]);
	printf("\n");
}

static void dump_aud_hdmi_fifo_status(void)
{
	uint32_t dword;

	dword = INREG(aud_reg_base + AUD_HDMI_FIFO_STATUS);
	printf("AUD_HDMI_FIFO_STATUS  Function_Reset\t\t\t%lu\n",                BIT(dword, 24));
	printf("AUD_HDMI_FIFO_STATUS  Conv_1_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 26));
	printf("AUD_HDMI_FIFO_STATUS  Conv_1_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 27));
	printf("AUD_HDMI_FIFO_STATUS  Conv_2_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 28));
	printf("AUD_HDMI_FIFO_STATUS  Conv_2_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 29));
	printf("AUD_HDMI_FIFO_STATUS  Conv_3_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 30));
	printf("AUD_HDMI_FIFO_STATUS  Conv_3_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 31));
}

static void parse_bdw_audio_chicken_bit_reg(uint32_t dword)
{
	printf("\t");
	printf("%s\n\t", OPNAME(vanilla_dp12_en,           BIT(dword, 31)));
	printf("%s\n\t", OPNAME(vanilla_3_widgets_en,      BIT(dword, 30)));
	printf("%s\n\t", OPNAME(block_audio,               BIT(dword, 10)));
	printf("%s\n\t", OPNAME(dis_eld_valid_pulse_trans, BIT(dword, 9)));
	printf("%s\n\t", OPNAME(dis_pd_pulse_trans,        BIT(dword, 8)));
	printf("%s\n\t", OPNAME(dis_ts_delta_err,          BIT(dword, 7)));
	printf("%s\n\t", OPNAME(dis_ts_fix_dp_hbr,         BIT(dword, 6)));
	printf("%s\n\t", OPNAME(pattern_gen_8_ch_en,       BIT(dword, 5)));
	printf("%s\n\t", OPNAME(pattern_gen_2_ch_en,       BIT(dword, 4)));
	printf("%s\n\t", OPNAME(fabric_32_44_dis,          BIT(dword, 3)));
	printf("%s\n\t", OPNAME(epss_dis,                  BIT(dword, 2)));
	printf("%s\n\t", OPNAME(ts_test_mode,              BIT(dword, 1)));
	printf("%s\n",   OPNAME(en_mmio_program,           BIT(dword, 0)));
}

/* Dump audio registers for Haswell and its successors (eg. Broadwell).
 * Their register layout are same in the north display engine.
 */
static void dump_hsw_plus(void)
{
	uint32_t dword;
	int i;

	set_aud_reg_base(0x65000);

	/* HSW DDI Buffer */
	dump_reg(DDI_BUF_CTL_A,                "DDI Buffer Controler A");
	dump_reg(DDI_BUF_CTL_B,                "DDI Buffer Controler B");
	dump_reg(DDI_BUF_CTL_C,                "DDI Buffer Controler C");
	dump_reg(DDI_BUF_CTL_D,                "DDI Buffer Controler D");
	dump_reg(DDI_BUF_CTL_E,                "DDI Buffer Controler E");

	/* HSW Pipe Function */
	dump_reg(PIPE_CONF_A,                  "PIPE Configuration A");
	dump_reg(PIPE_CONF_B,                  "PIPE Configuration B");
	dump_reg(PIPE_CONF_C,                  "PIPE Configuration C");
	dump_reg(PIPE_CONF_EDP,                "PIPE Configuration EDP");

	dump_reg(PIPE_DDI_FUNC_CTL_A,          "PIPE DDI Function Control A");
	dump_reg(PIPE_DDI_FUNC_CTL_B,          "PIPE DDI Function Control B");
	dump_reg(PIPE_DDI_FUNC_CTL_C,          "PIPE DDI Function Control C");
	dump_reg(PIPE_DDI_FUNC_CTL_EDP,        "PIPE DDI Function Control EDP");

	/* HSW Display port */
	dump_reg(DP_TP_CTL_A,                  "DisplayPort Transport A Control");
	dump_reg(DP_TP_CTL_B,                  "DisplayPort Transport B Control");
	dump_reg(DP_TP_CTL_C,                  "DisplayPort Transport C Control");
	dump_reg(DP_TP_CTL_D,                  "DisplayPort Transport D Control");
	dump_reg(DP_TP_CTL_E,                  "DisplayPort Transport E Control");

	dump_reg(DP_TP_ST_A,                   "DisplayPort Transport A Status");
	dump_reg(DP_TP_ST_B,                   "DisplayPort Transport B Status");
	dump_reg(DP_TP_ST_C,                   "DisplayPort Transport C Status");
	dump_reg(DP_TP_ST_D,                   "DisplayPort Transport D Status");
	dump_reg(DP_TP_ST_E,                   "DisplayPort Transport E Status");

	/* HSW North Display Audio */
	dump_aud_reg(AUD_TCA_CONFIG,           "Audio Configuration - Transcoder A");
	dump_aud_reg(AUD_TCB_CONFIG,           "Audio Configuration - Transcoder B");
	dump_aud_reg(AUD_TCC_CONFIG,           "Audio Configuration - Transcoder C");
	dump_aud_reg(AUD_C1_MISC_CTRL,         "Audio Converter 1 MISC Control");
	dump_aud_reg(AUD_C2_MISC_CTRL,         "Audio Converter 2 MISC Control");
	dump_aud_reg(AUD_C3_MISC_CTRL,         "Audio Converter 3 MISC Control");
	dump_aud_reg(AUD_VID_DID,              "Audio Vendor ID / Device ID");
	dump_aud_reg(AUD_RID,                  "Audio Revision ID");
	dump_aud_reg(AUD_TCA_M_CTS_ENABLE,     "Audio M & CTS Programming Enable - Transcoder A");
	dump_aud_reg(AUD_TCB_M_CTS_ENABLE,     "Audio M & CTS Programming Enable - Transcoder B");
	dump_aud_reg(AUD_TCC_M_CTS_ENABLE,     "Audio M & CTS Programming Enable - Transcoder C");
	dump_aud_reg(AUD_PWRST,                "Audio Power State (Function Group, Convertor, Pin Widget)");
	dump_aud_reg(AUD_TCA_EDID_DATA,        "Audio EDID Data Block - Transcoder A");
	dump_aud_reg(AUD_TCB_EDID_DATA,        "Audio EDID Data Block - Transcoder B");
	dump_aud_reg(AUD_TCC_EDID_DATA,        "Audio EDID Data Block - Transcoder C");
	dump_aud_reg(AUD_TCA_INFOFR,           "Audio Widget Data Island Packet - Transcoder A");
	dump_aud_reg(AUD_TCB_INFOFR,           "Audio Widget Data Island Packet - Transcoder B");
	dump_aud_reg(AUD_TCC_INFOFR,           "Audio Widget Data Island Packet - Transcoder C");
	dump_aud_reg(AUD_PIPE_CONV_CFG,        "Audio Pipe and Converter Configs");
	dump_aud_reg(AUD_C1_DIG_CNVT,          "Audio Digital Converter - Converter 1");
	dump_aud_reg(AUD_C2_DIG_CNVT,          "Audio Digital Converter - Converter 2");
	dump_aud_reg(AUD_C3_DIG_CNVT,          "Audio Digital Converter - Converter 3");
	dump_aud_reg(AUD_C1_STR_DESC,          "Audio Stream Descriptor Format - Converter 1");
	dump_aud_reg(AUD_C2_STR_DESC,          "Audio Stream Descriptor Format - Converter 2");
	dump_aud_reg(AUD_C3_STR_DESC,          "Audio Stream Descriptor Format - Converter 3");
	dump_aud_reg(AUD_OUT_CHAN_MAP,         "Audio Output Channel Mapping");
	dump_aud_reg(AUD_TCA_PIN_PIPE_CONN_ENTRY_LNGTH, "Audio Connection List entry and Length - Transcoder A");
	dump_aud_reg(AUD_TCB_PIN_PIPE_CONN_ENTRY_LNGTH, "Audio Connection List entry and Length - Transcoder B");
	dump_aud_reg(AUD_TCC_PIN_PIPE_CONN_ENTRY_LNGTH, "Audio Connection List entry and Length - Transcoder C");
	dump_aud_reg(AUD_PIPE_CONN_SEL_CTRL,   "Audio Pipe Connection Select Control");
	dump_aud_reg(AUD_TCA_DIP_ELD_CTRL_ST,  "Audio DIP and ELD control state - Transcoder A");
	dump_aud_reg(AUD_TCB_DIP_ELD_CTRL_ST,  "Audio DIP and ELD control state - Transcoder B");
	dump_aud_reg(AUD_TCC_DIP_ELD_CTRL_ST,  "Audio DIP and ELD control state - Transcoder C");
	dump_aud_reg(AUD_PIN_ELD_CP_VLD,       "Audio pin ELD valid and CP ready status");
	dump_aud_reg(AUD_HDMI_FIFO_STATUS,     "Audio HDMI FIFO Status");

	/* Audio debug registers */
	dump_aud_reg(AUD_ICOI,                 "Audio Immediate Command Output Interface");
	dump_aud_reg(AUD_IRII,                 "Audio Immediate Response Input Interface");
	dump_aud_reg(AUD_ICS,                  "Audio Immediate Command Status");
	dump_aud_reg(AUD_CHICKENBIT_REG,       "Audio Chicken Bit Register");
	dump_aud_reg(AUD_DP_DIP_STATUS,        "Audio DP and DIP FIFO Debug Status");
	dump_aud_reg(AUD_TCA_M_CTS,            "Audio M CTS Read Back Transcoder A");
	dump_aud_reg(AUD_TCB_M_CTS,            "Audio M CTS Read Back Transcoder B");
	dump_aud_reg(AUD_TCC_M_CTS,            "Audio M CTS Read Back Transcoder C");

	printf("\nDetails:\n\n");

	dump_ddi_buf_ctl(PORT_A);
	dump_ddi_buf_ctl(PORT_B);
	dump_ddi_buf_ctl(PORT_C);
	dump_ddi_buf_ctl(PORT_D);
	dump_ddi_buf_ctl(PORT_E);

	dump_ddi_func_ctl(PIPE_A);
	dump_ddi_func_ctl(PIPE_B);
	dump_ddi_func_ctl(PIPE_C);

	/* audio configuration - details */
	dump_aud_config(TRANSCODER_A);
	dump_aud_config(TRANSCODER_B);
	dump_aud_config(TRANSCODER_C);

	dump_aud_misc_control(CONVERTER_1);
	dump_aud_misc_control(CONVERTER_2);
	dump_aud_misc_control(CONVERTER_3);

	dump_aud_vendor_device_id();
	dump_aud_revision_id();

	dump_aud_m_cts_enable(TRANSCODER_A);
	dump_aud_m_cts_enable(TRANSCODER_B);
	dump_aud_m_cts_enable(TRANSCODER_C);

	dump_aud_power_state();

	dump_aud_edid_data(TRANSCODER_A);
	dump_aud_edid_data(TRANSCODER_B);
	dump_aud_edid_data(TRANSCODER_C);

	dump_aud_infoframe(TRANSCODER_A);
	dump_aud_infoframe(TRANSCODER_B);
	dump_aud_infoframe(TRANSCODER_C);

	dump_aud_pipe_conv_cfg();

	dump_aud_dig_cnvt(CONVERTER_1);
	dump_aud_dig_cnvt(CONVERTER_2);
	dump_aud_dig_cnvt(CONVERTER_3);

	dump_aud_str_desc(CONVERTER_1);
	dump_aud_str_desc(CONVERTER_2);
	dump_aud_str_desc(CONVERTER_3);

	dump_aud_out_chan_map();

	dump_aud_connect_list_entry_length(TRANSCODER_A);
	dump_aud_connect_list_entry_length(TRANSCODER_B);
	dump_aud_connect_list_entry_length(TRANSCODER_C);
	dump_aud_connect_select_ctrl();

	dump_aud_dip_eld_ctrl_st(TRANSCODER_A);
	dump_aud_dip_eld_ctrl_st(TRANSCODER_B);
	dump_aud_dip_eld_ctrl_st(TRANSCODER_C);

	dump_aud_eld_cp_vld();
	dump_aud_hdmi_fifo_status();

	dword = read_aud_reg(AUD_ICS);
	printf("IRV [%1lx] %s\t", BIT(dword, 1),
		OPNAME(immed_result_valid, BIT(dword, 1)));
	printf("ICB [%1lx] %s\n", BIT(dword, 1),
		OPNAME(immed_cmd_busy, BIT(dword, 0)));

	dword = read_aud_reg(AUD_CHICKENBIT_REG);
	printf("AUD_CHICKENBIT_REG Audio Chicken Bits: %08x\n", dword);
	if (IS_BROADWELL(devid))
		parse_bdw_audio_chicken_bit_reg(dword);

	dword = read_aud_reg(AUD_DP_DIP_STATUS);
	printf("AUD_DP_DIP_STATUS Audio DP & DIP FIFO Status: %08x\n\t", dword);
	for (i = 31; i >= 0; i--)
		if (BIT(dword, i))
			printf("%s\n\t", audio_dp_dip_status[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	struct pci_device *pci_dev;

	pci_dev = intel_get_pci_device();
	devid = pci_dev->device_id; /* XXX not true when mapping! */

	do_self_tests();

	if (argc == 2)
		intel_mmio_use_dump_file(argv[1]);
	else
		intel_mmio_use_pci_bar(pci_dev);

	if (IS_VALLEYVIEW(devid)) {
		printf("Valleyview audio registers:\n\n");
		dump_ironlake();
	}  else if (IS_BROADWELL(devid) || IS_HASWELL(devid)) {
		printf("%s audio registers:\n\n",
			IS_BROADWELL(devid) ? "Broadwell" : "Haswell");
		dump_hsw_plus();
	} else if (IS_GEN6(devid) || IS_GEN7(devid)
		|| getenv("HAS_PCH_SPLIT")) {
		printf("%s audio registers:\n\n",
				IS_GEN6(devid) ? "SandyBridge" : "IvyBridge");
		intel_check_pch();
		dump_cpt();
	} else if (IS_GEN5(devid)) {
		printf("Ironlake audio registers:\n\n");
		dump_ironlake();
	} else if (IS_G4X(devid)) {
		printf("G45 audio registers:\n\n");
		dump_eaglelake();
	}

	return 0;
}
