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
#include "intel_gpu_tools.h"

static uint32_t devid;


#define BITSTO(n)		(n >= sizeof(long) * 8 ? ~0 : (1UL << (n)) - 1)
#define BITMASK(high, low)	(BITSTO(high+1) & ~BITSTO(low))
#define BITS(reg, high, low)	(((reg) & (BITMASK(high, low))) >> (low))
#define BIT(reg, n)		BITS(reg, n, n)

#define min_t(type, x, y) ({                    \
        type __min1 = (x);                      \
        type __min2 = (y);                      \
        __min1 < __min2 ? __min1: __min2; })

#define OPNAME(names, index)   \
               names[min_t(unsigned int, index, ARRAY_SIZE(names) - 1)]

#define dump_reg(reg, desc)					\
    do {							\
	    dword = INREG(reg);	  				\
	    printf("%-21s 0x%08x  %s\n", # reg, dword, desc);	\
    } while (0)


static const char *pixel_clock[] = {
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

static const char *power_state[] = {
	[0] = "D0",
	[1] = "D1",
	[2] = "D2",
	[3] = "D3",
};

static const char *stream_type[] = {
	[0] = "default samples",
	[1] = "one bit stream",
	[2] = "DST stream",
	[3] = "MLP stream",
	[4] = "Reserved",
};

static const char *dip_port[] = {
	[0] = "Reserved",
	[1] = "Digital Port B",
	[2] = "Digital Port C",
	[3] = "Digital Port D",
};

static const char *dip_type[] = {
        [0] = "Audio DIP Disabled",
        [1] = "Audio DIP Enabled",
};

static const char *dip_index[] = {
	[0] = "Audio DIP",
	[1] = "ACP DIP",
	[2] = "ISRC1 DIP",
	[3] = "ISRC2 DIP",
	[4] = "Reserved",
};

static const char *dip_trans[] = {
	[0] = "disabled",
	[1] = "reserved",
	[2] = "send once",
	[3] = "best effort",
};

static const char *video_dip_index[] = {
	[0] = "AVI DIP",
	[1] = "Vendor-specific DIP",
	[2] = "Gamut Metadata DIP",
	[3] = "Source Product Description DIP",
};

static const char *video_dip_trans[] = {
	[0] = "send once",
	[1] = "send every vsync",
	[2] = "send at least every other vsync",
	[3] = "reserved",
};

static const char *trans_to_port_sel[] = {
	[0] = "no port",
	[1] = "Digital Port B",
	[2] = "Digital Port C",
	[3] = "Digital Port D",
	[4] = "reserved",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char *ddi_mode[] = {
	[0] = "HDMI mode",
	[1] = "DVI mode",
	[2] = "DP SST mode",
	[3] = "DP MST mode",
	[4] = "DP FDI mode",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char *transcoder_select[] = {
	[0] = "Transcoder A",
	[1] = "Transcoder B",
	[2] = "Transcoder C",
	[3] = "reserved",
};

static const char *dp_port_width[] = {
	[0] = "x1 mode",
	[1] = "x2 mode",
	[2] = "reserved",
	[3] = "x4 mode",
	[4] = "reserved",
	[5] = "reserved",
	[6] = "reserved",
	[7] = "reserved",
};

static const char *bits_per_sample[] = {
	[0] = "reserved",
	[1] = "16 bits",
	[2] = "24 bits",
	[3] = "32 bits",
	[4] = "20 bits",
	[5] = "reserved",
};

static const char *sdvo_hdmi_encoding[] = {
	[0] = "SDVO",
	[1] = "reserved",
	[2] = "TMDS",
	[3] = "reserved",
};

static const char *n_index_value[] = {
	[0] = "HDMI",
	[1] = "DisplayPort",
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
    dump_reg(AUD_PINW_UNSOLRESP,"Audio Unsolicited Response Enable");
    dump_reg(AUD_CNTL_ST,	"Audio Control State Register");
    dump_reg(AUD_PINW_CONFIG,	"Audio Configuration Default");
    dump_reg(AUD_HDMIW_STATUS,	"Audio HDMI Status");
    dump_reg(AUD_HDMIW_HDMIEDID,"Audio HDMI Data EDID Block");
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
	    printf("\t\t\t\t\t[0x%x] %u => %lu \n", dword, i, BITS(dword, 7, 4));
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
 * IronLake registers
 */
#define AUD_CONFIG_A		0xE2000
#define AUD_CONFIG_B		0xE2100
#define AUD_CTS_ENABLE_A	0xE2028
#define AUD_CTS_ENABLE_B	0xE2128
#define AUD_MISC_CTRL_A		0xE2010
#define AUD_MISC_CTRL_B		0xE2110
#define AUD_VID_DID		0xE2020
#define AUD_RID			0xE2024
#define AUD_PWRST		0xE204C
#define AUD_PORT_EN_HD_CFG	0xE207C
#define AUD_OUT_DIG_CNVT_A	0xE2080
#define AUD_OUT_DIG_CNVT_B	0xE2180
#define AUD_OUT_CH_STR		0xE2088
#define AUD_OUT_STR_DESC_A	0xE2084
#define AUD_OUT_STR_DESC_B	0xE2184
#define AUD_PINW_CONNLNG_LIST	0xE20A8
#define AUD_PINW_CONNLNG_SEL	0xE20AC
#define AUD_CNTL_ST_A		0xE20B4
#define AUD_CNTL_ST_B		0xE21B4
#define AUD_CNTL_ST2		0xE20C0
#define AUD_HDMIW_STATUS	0xE20D4
#define AUD_HDMIW_HDMIEDID_A	0xE2050
#define AUD_HDMIW_HDMIEDID_B	0xE2150
#define AUD_HDMIW_INFOFR_A	0xE2054
#define AUD_HDMIW_INFOFR_B	0xE2154

static void dump_ironlake(void)
{
    uint32_t dword;
    int i;

    dump_reg(HDMIB,			"sDVO/HDMI Port B Control");
    dump_reg(HDMIC,			"HDMI Port C Control");
    dump_reg(HDMID,			"HDMI Port D Control");
    dump_reg(PCH_DP_B,			"DisplayPort B Control Register");
    dump_reg(PCH_DP_C,			"DisplayPort C Control Register");
    dump_reg(PCH_DP_D,			"DisplayPort D Control Register");
    dump_reg(AUD_CONFIG_A,		"Audio Configuration - Transcoder A");
    dump_reg(AUD_CONFIG_B,		"Audio Configuration - Transcoder B");
    dump_reg(AUD_CTS_ENABLE_A,		"Audio CTS Programming Enable - Transcoder A");
    dump_reg(AUD_CTS_ENABLE_B,		"Audio CTS Programming Enable - Transcoder B");
    dump_reg(AUD_MISC_CTRL_A,		"Audio MISC Control for Transcoder A");
    dump_reg(AUD_MISC_CTRL_B,		"Audio MISC Control for Transcoder B");
    dump_reg(AUD_VID_DID,		"Audio Vendor ID / Device ID");
    dump_reg(AUD_RID,			"Audio Revision ID");
    dump_reg(AUD_PWRST,			"Audio Power State (Function Group, Convertor, Pin Widget)");
    dump_reg(AUD_PORT_EN_HD_CFG,	"Audio Port Enable HDAudio Config");
    dump_reg(AUD_OUT_DIG_CNVT_A,	"Audio Digital Converter - Conv A");
    dump_reg(AUD_OUT_DIG_CNVT_B,	"Audio Digital Converter - Conv B");
    dump_reg(AUD_OUT_CH_STR,		"Audio Channel ID and Stream ID");
    dump_reg(AUD_OUT_STR_DESC_A,	"Audio Stream Descriptor Format - Conv A");
    dump_reg(AUD_OUT_STR_DESC_B,	"Audio Stream Descriptor Format - Conv B");
    dump_reg(AUD_PINW_CONNLNG_LIST,	"Audio Connection List");
    dump_reg(AUD_PINW_CONNLNG_SEL,	"Audio Connection Select");
    dump_reg(AUD_CNTL_ST_A,		"Audio Control State Register - Transcoder A");
    dump_reg(AUD_CNTL_ST_B,		"Audio Control State Register - Transcoder B");
    dump_reg(AUD_CNTL_ST2,		"Audio Control State 2");
    dump_reg(AUD_HDMIW_STATUS,		"Audio HDMI Status");
    dump_reg(AUD_HDMIW_HDMIEDID_A,	"HDMI Data EDID Block - Transcoder A");
    dump_reg(AUD_HDMIW_HDMIEDID_B,	"HDMI Data EDID Block - Transcoder B");
    dump_reg(AUD_HDMIW_INFOFR_A,	"Audio Widget Data Island Packet - Transcoder A");
    dump_reg(AUD_HDMIW_INFOFR_B,	"Audio Widget Data Island Packet - Transcoder B");

    printf("\nDetails:\n\n");

    dword = INREG(AUD_VID_DID);
    printf("AUD_VID_DID vendor id\t\t\t\t\t0x%x\n", dword >> 16);
    printf("AUD_VID_DID device id\t\t\t\t\t0x%x\n", dword & 0xffff);

    dword = INREG(AUD_RID);
    printf("AUD_RID Major_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 23, 20));
    printf("AUD_RID Minor_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 19, 16));
    printf("AUD_RID Revision_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 15, 8));
    printf("AUD_RID Stepping_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 7, 0));

    dword = INREG(HDMIB);
    printf("HDMIB HDMIB_Enable\t\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
    printf("HDMIB Transcoder_Select\t\t\t\t\t%s\n", BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
    printf("HDMIB HDCP_Port_Select\t\t\t\t\t%lu\n", BIT(dword, 5));
    printf("HDMIB SDVOB Hot Plug Interrupt Detect Enable\t\t%lu\n", BIT(dword, 23));
    printf("HDMIB Digital_Port_B_Detected\t\t\t\t%lu\n", BIT(dword, 2));
    printf("HDMIB Encoding\t\t\t\t\t\t[0x%lx] %s\n",
				BITS(dword, 11, 10), sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
    printf("HDMIB Null_packets_enabled_during_Vsync\t\t\t%u\n",  !!(dword & SDVO_NULL_PACKETS_DURING_VSYNC));
    printf("HDMIB Audio_Output_Enable\t\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

    dword = INREG(HDMIC);
    printf("HDMIC HDMIC_Enable\t\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
    printf("HDMIC Transcoder_Select\t\t\t\t\t%s\n", BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
    printf("HDMIC HDCP_Port_Select\t\t\t\t\t%lu\n", BIT(dword, 5));
    printf("HDMIC Digital_Port_C_Detected\t\t\t\t%lu\n", BIT(dword, 2));
    printf("HDMIC Encoding\t\t\t\t\t\t[0x%lx] %s\n",
				BITS(dword, 11, 10), sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
    printf("HDMIC Null_packets_enabled_during_Vsync\t\t\t%u\n",  !!(dword & SDVO_NULL_PACKETS_DURING_VSYNC));
    printf("HDMIC Audio_Output_Enable\t\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

    dword = INREG(HDMID);
    printf("HDMID HDMID_Enable\t\t\t\t\t%u\n",      !!(dword & SDVO_ENABLE));
    printf("HDMID Transcoder_Select\t\t\t\t\t%s\n", BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
    printf("HDMID HDCP_Port_Select\t\t\t\t\t%lu\n", BIT(dword, 5));
    printf("HDMID Digital_Port_D_Detected\t\t\t\t%lu\n", BIT(dword, 2));
    printf("HDMID Encoding\t\t\t\t\t\t[0x%lx] %s\n",
				BITS(dword, 11, 10), sdvo_hdmi_encoding[BITS(dword, 11, 10)]);
    printf("HDMID Null_packets_enabled_during_Vsync\t\t\t%u\n",  !!(dword & SDVO_NULL_PACKETS_DURING_VSYNC));
    printf("HDMID Audio_Output_Enable\t\t\t\t%u\n", !!(dword & SDVO_AUDIO_ENABLE));

    dword = INREG(PCH_DP_B);
    printf("PCH_DP_B DisplayPort_Enable\t\t\t\t%lu\n", BIT(dword, 31));
    printf("PCH_DP_B Transcoder_Select\t\t\t\t%s\n", BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
    printf("PCH_DP_B Port_Width_Selection\t\t\t\t[0x%lx] %s\n",
				BITS(dword, 21, 19), dp_port_width[BITS(dword, 21, 19)]);
    printf("PCH_DP_B Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
    printf("PCH_DP_B HDCP_Port_Select\t\t\t\t%lu\n", BIT(dword, 5));
    printf("PCH_DP_B Audio_Output_Enable\t\t\t\t%lu\n", BIT(dword, 6));

    dword = INREG(PCH_DP_C);
    printf("PCH_DP_C DisplayPort_Enable\t\t\t\t%lu\n", BIT(dword, 31));
    printf("PCH_DP_C Transcoder_Select\t\t\t\t%s\n", BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
    printf("PCH_DP_C Port_Width_Selection\t\t\t\t[0x%lx] %s\n",
				BITS(dword, 21, 19), dp_port_width[BITS(dword, 21, 19)]);
    printf("PCH_DP_C Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
    printf("PCH_DP_C HDCP_Port_Select\t\t\t\t%lu\n", BIT(dword, 5));
    printf("PCH_DP_C Audio_Output_Enable\t\t\t\t%lu\n", BIT(dword, 6));

    dword = INREG(PCH_DP_D);
    printf("PCH_DP_D DisplayPort_Enable\t\t\t\t%lu\n", BIT(dword, 31));
    printf("PCH_DP_D Transcoder_Select\t\t\t\t%s\n", BIT(dword, 30) ? "Transcoder B" : "Transcoder A");
    printf("PCH_DP_D Port_Width_Selection\t\t\t\t[0x%lx] %s\n",
				BITS(dword, 21, 19), dp_port_width[BITS(dword, 21, 19)]);
    printf("PCH_DP_D Port_Detected\t\t\t\t\t%lu\n", BIT(dword, 2));
    printf("PCH_DP_D HDCP_Port_Select\t\t\t\t%lu\n", BIT(dword, 5));
    printf("PCH_DP_D Audio_Output_Enable\t\t\t\t%lu\n", BIT(dword, 6));

    dword = INREG(AUD_CONFIG_A);
    printf("AUD_CONFIG_A  N_index_value\t\t\t\t[0x%lx] %s\n", BIT(dword, 29),
						n_index_value[BIT(dword, 29)]);
    printf("AUD_CONFIG_A  N_programming_enable\t\t\t%lu\n", BIT(dword, 28));
    printf("AUD_CONFIG_A  Upper_N_value\t\t\t\t0x%02lx\n", BITS(dword, 27, 20));
    printf("AUD_CONFIG_A  Lower_N_value\t\t\t\t0x%03lx\n", BITS(dword, 15, 4));
    printf("AUD_CONFIG_A  Pixel_Clock\t\t\t\t[0x%lx] %s\n", BITS(dword, 19, 16),
		    OPNAME(pixel_clock, BITS(dword, 19, 16)));
    printf("AUD_CONFIG_A  Disable_NCTS\t\t\t\t%lu\n", BIT(dword, 3));
    dword = INREG(AUD_CONFIG_B);
    printf("AUD_CONFIG_B  N_index_value\t\t\t\t[0x%lx] %s\n", BIT(dword, 29),
						n_index_value[BIT(dword, 29)]);
    printf("AUD_CONFIG_B  N_programming_enable\t\t\t%lu\n", BIT(dword, 28));
    printf("AUD_CONFIG_B  Upper_N_value\t\t\t\t0x%02lx\n", BITS(dword, 27, 20));
    printf("AUD_CONFIG_B  Lower_N_value\t\t\t\t0x%03lx\n", BITS(dword, 15, 4));
    printf("AUD_CONFIG_B  Pixel_Clock\t\t\t\t[0x%lx] %s\n", BITS(dword, 19, 16),
		    OPNAME(pixel_clock, BITS(dword, 19, 16)));
    printf("AUD_CONFIG_B  Disable_NCTS\t\t\t\t%lu\n", BIT(dword, 3));

    dword = INREG(AUD_CTS_ENABLE_A);
    printf("AUD_CTS_ENABLE_A  Enable_CTS_or_M_programming\t\t%lu\n", BIT(dword, 20));
    printf("AUD_CTS_ENABLE_A  CTS/M value Index\t\t\t%s\n", BIT(dword, 21) ? "CTS" : "M");
    printf("AUD_CTS_ENABLE_A  CTS_programming\t\t\t%#lx\n", BITS(dword, 19, 0));
    dword = INREG(AUD_CTS_ENABLE_B);
    printf("AUD_CTS_ENABLE_B  Enable_CTS_or_M_programming\t\t%lu\n", BIT(dword, 20));
    printf("AUD_CTS_ENABLE_B  CTS/M value Index\t\t\t%s\n", BIT(dword, 21) ? "CTS" : "M");
    printf("AUD_CTS_ENABLE_B  CTS_programming\t\t\t%#lx\n", BITS(dword, 19, 0));

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

    dword = INREG(AUD_PWRST);
    printf("AUD_PWRST  Function_Group_Device_Power_State_Current\t%s\n", power_state[BITS(dword, 23, 22)]);
    printf("AUD_PWRST  Function_Group_Device_Power_State_Set    \t%s\n", power_state[BITS(dword, 21, 20)]);
    printf("AUD_PWRST  ConvertorB_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 19, 18)]);
    printf("AUD_PWRST  ConvertorB_Widget_Power_State_Requested  \t%s\n", power_state[BITS(dword, 17, 16)]);
    printf("AUD_PWRST  ConvertorA_Widget_Power_State_Current    \t%s\n", power_state[BITS(dword, 15, 14)]);
    printf("AUD_PWRST  ConvertorA_Widget_Power_State_Requsted   \t%s\n", power_state[BITS(dword, 13, 12)]);
    printf("AUD_PWRST  PinD_Widget_Power_State_Current          \t%s\n", power_state[BITS(dword, 11, 10)]);
    printf("AUD_PWRST  PinD_Widget_Power_State_Set              \t%s\n", power_state[BITS(dword,  9,  8)]);
    printf("AUD_PWRST  PinC_Widget_Power_State_Current          \t%s\n", power_state[BITS(dword,  7,  6)]);
    printf("AUD_PWRST  PinC_Widget_Power_State_Set              \t%s\n", power_state[BITS(dword,  5,  4)]);
    printf("AUD_PWRST  PinB_Widget_Power_State_Current          \t%s\n", power_state[BITS(dword,  3,  2)]);
    printf("AUD_PWRST  PinB_Widget_Power_State_Set              \t%s\n", power_state[BITS(dword,  1,  0)]);

    dword = INREG(AUD_PORT_EN_HD_CFG);
    printf("AUD_PORT_EN_HD_CFG  Convertor_A_Digen\t\t\t%lu\n",	BIT(dword, 0));
    printf("AUD_PORT_EN_HD_CFG  Convertor_B_Digen\t\t\t%lu\n",	BIT(dword, 1));
    printf("AUD_PORT_EN_HD_CFG  ConvertorA_Stream_ID\t\t%lu\n",	BITS(dword,  7, 4));
    printf("AUD_PORT_EN_HD_CFG  ConvertorB_Stream_ID\t\t%lu\n",	BITS(dword, 11, 8));
    printf("AUD_PORT_EN_HD_CFG  Port_B_Out_Enable\t\t\t%lu\n",	BIT(dword, 12));
    printf("AUD_PORT_EN_HD_CFG  Port_C_Out_Enable\t\t\t%lu\n",	BIT(dword, 13));
    printf("AUD_PORT_EN_HD_CFG  Port_D_Out_Enable\t\t\t%lu\n",	BIT(dword, 14));
    printf("AUD_PORT_EN_HD_CFG  Port_B_Amp_Mute_Status\t\t%lu\n", BIT(dword, 16));
    printf("AUD_PORT_EN_HD_CFG  Port_C_Amp_Mute_Status\t\t%lu\n", BIT(dword, 17));
    printf("AUD_PORT_EN_HD_CFG  Port_D_Amp_Mute_Status\t\t%lu\n", BIT(dword, 18));

    dword = INREG(AUD_OUT_DIG_CNVT_A);
    printf("AUD_OUT_DIG_CNVT_A  V\t\t\t\t\t%lu\n",		BIT(dword, 1));
    printf("AUD_OUT_DIG_CNVT_A  VCFG\t\t\t\t%lu\n",		BIT(dword, 2));
    printf("AUD_OUT_DIG_CNVT_A  PRE\t\t\t\t\t%lu\n",		BIT(dword, 3));
    printf("AUD_OUT_DIG_CNVT_A  Copy\t\t\t\t%lu\n",		BIT(dword, 4));
    printf("AUD_OUT_DIG_CNVT_A  NonAudio\t\t\t\t%lu\n",		BIT(dword, 5));
    printf("AUD_OUT_DIG_CNVT_A  PRO\t\t\t\t\t%lu\n",		BIT(dword, 6));
    printf("AUD_OUT_DIG_CNVT_A  Level\t\t\t\t%lu\n",		BIT(dword, 7));
    printf("AUD_OUT_DIG_CNVT_A  Category_Code\t\t\t%lu\n",	BITS(dword, 14, 8));
    printf("AUD_OUT_DIG_CNVT_A  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
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
    printf("AUD_OUT_DIG_CNVT_B  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
    printf("AUD_OUT_DIG_CNVT_B  Stream_ID\t\t\t\t%lu\n",	BITS(dword, 23, 20));

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

    dword = INREG(AUD_PINW_CONNLNG_SEL);
    printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_B\t%lu\n", BITS(dword,  7,  0));
    printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_C\t%lu\n", BITS(dword, 15,  8));
    printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_D\t%lu\n", BITS(dword, 23, 16));

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
    printf("AUD_CNTL_ST_A  ELD_access_address\t\t\t%lu\n", BITS(dword, 9, 5));

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
    printf("AUD_CNTL_ST_B  ELD_access_address\t\t\t%lu\n", BITS(dword, 9, 5));

    dword = INREG(AUD_CNTL_ST2);
    printf("AUD_CNTL_ST2  CP_ReadyB\t\t\t\t\t%lu\n",	BIT(dword, 1));
    printf("AUD_CNTL_ST2  ELD_validB\t\t\t\t%lu\n",	BIT(dword, 0));
    printf("AUD_CNTL_ST2  CP_ReadyC\t\t\t\t\t%lu\n",	BIT(dword, 5));
    printf("AUD_CNTL_ST2  ELD_validC\t\t\t\t%lu\n",	BIT(dword, 4));
    printf("AUD_CNTL_ST2  CP_ReadyD\t\t\t\t\t%lu\n",	BIT(dword, 9));
    printf("AUD_CNTL_ST2  ELD_validD\t\t\t\t%lu\n",	BIT(dword, 8));

    dword = INREG(AUD_HDMIW_STATUS);
    printf("AUD_HDMIW_STATUS  Conv_B_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 31));
    printf("AUD_HDMIW_STATUS  Conv_B_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 30));
    printf("AUD_HDMIW_STATUS  Conv_A_CDCLK/DOTCLK_FIFO_Underrun\t%lu\n", BIT(dword, 29));
    printf("AUD_HDMIW_STATUS  Conv_A_CDCLK/DOTCLK_FIFO_Overrun\t%lu\n",  BIT(dword, 28));
    printf("AUD_HDMIW_STATUS  BCLK/CDCLK_FIFO_Overrun\t\t%lu\n",	 BIT(dword, 25));
    printf("AUD_HDMIW_STATUS  Function_Reset\t\t\t%lu\n",		 BIT(dword, 29));

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

}


#undef AUD_CONFIG_A
#undef AUD_MISC_CTRL_A
#undef AUD_VID_DID
#undef AUD_RID
#undef AUD_CTS_ENABLE_A
#undef AUD_PWRST
#undef AUD_HDMIW_HDMIEDID_A
#undef AUD_HDMIW_INFOFR_A
#undef AUD_PORT_EN_HD_CFG
#undef AUD_OUT_DIG_CNVT_A
#undef AUD_OUT_STR_DESC_A
#undef AUD_OUT_CH_STR
#undef AUD_PINW_CONNLNG_LIST
#undef AUD_CNTL_ST_A
#undef AUD_HDMIW_STATUS
#undef AUD_CONFIG_B
#undef AUD_MISC_CTRL_B
#undef AUD_CTS_ENABLE_B
#undef AUD_HDMIW_HDMIEDID_B
#undef AUD_HDMIW_INFOFR_B
#undef AUD_OUT_DIG_CNVT_B
#undef AUD_OUT_STR_DESC_B
#undef AUD_CNTL_ST_B

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
#define AUD_PINW_CONNLNG_SELA 0xE50AC
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
    printf("AUD_OUT_DIG_CNVT_A  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
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
    printf("AUD_OUT_DIG_CNVT_B  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
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
    printf("AUD_OUT_DIG_CNVT_C  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
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

#undef AUD_CONFIG_A
#undef AUD_MISC_CTRL_A
#undef AUD_VID_DID
#undef AUD_RID
#undef AUD_CTS_ENABLE_A
#undef AUD_PWRST
#undef AUD_HDMIW_HDMIEDID_A
#undef AUD_HDMIW_INFOFR_A
#undef AUD_PORT_EN_HD_CFG
#undef AUD_OUT_DIG_CNVT_A
#undef AUD_OUT_STR_DESC_A
#undef AUD_OUT_CH_STR
#undef AUD_PINW_CONNLNG_LIST
#undef AUD_CNTL_ST_A
#undef AUD_HDMIW_STATUS
#undef AUD_CONFIG_B
#undef AUD_MISC_CTRL_B
#undef AUD_CTS_ENABLE_B
#undef AUD_HDMIW_HDMIEDID_B
#undef AUD_HDMIW_INFOFR_B
#undef AUD_OUT_DIG_CNVT_B
#undef AUD_OUT_STR_DESC_B
#undef AUD_CNTL_ST_B
#undef AUD_CONFIG_C
#undef AUD_MISC_CTRL_C
#undef AUD_CTS_ENABLE_C
#undef AUD_HDMIW_HDMIEDID_C
#undef AUD_HDMIW_INFOFR_C
#undef AUD_OUT_DIG_CNVT_C
#undef AUD_OUT_STR_DESC_C

#undef VIDEO_DIP_CTL_A
#undef VIDEO_DIP_CTL_B
#undef VIDEO_DIP_CTL_C
#undef VIDEO_DIP_CTL_D
#undef VIDEO_DIP_DATA

/*
 * Haswell registers
 */

/* DisplayPort Transport Control */
#define DP_TP_CTL_A	    0x64040
#define DP_TP_CTL_B	    0x64140
#define DP_TP_CTL_C	    0x64240
#define DP_TP_CTL_D	    0x64340
#define DP_TP_CTL_E	    0x64440

/* DisplayPort Transport Status */
#define DP_TP_ST_A          0x64044
#define DP_TP_ST_B          0x64144
#define DP_TP_ST_C          0x64244
#define DP_TP_ST_D          0x64344
#define DP_TP_ST_E          0x64444

/* Transcoder configuration */
#define TRANS_CONF_A	    0xF0008
#define TRANS_CONF_B	    0xF1008
#define TRANS_CONF_C	    0xF2008

/* DDI Buffer Control */
#define DDI_BUF_CTL_A	      0x64000
#define DDI_BUF_CTL_B	      0x64100
#define DDI_BUF_CTL_C	      0x64200
#define DDI_BUF_CTL_D	      0x64300
#define DDI_BUF_CTL_E	      0x64400

/* DDI Buffer Translation */
#define DDI_BUF_TRANS_A		0x64e00
#define DDI_BUF_TRANS_B		0x64e60
#define DDI_BUF_TRANS_C		0x64ec0
#define DDI_BUF_TRANS_D		0x64f20
#define DDI_BUF_TRANS_E		0x64f80

/* DDI Aux Channel */
#define DDI_AUX_CHANNEL_CTRL	0x64010
#define DDI_AUX_DATA		0x64014
#define DDI_AUX_TST		0x64028

/* DDI CRC Control */
#define DDI_CRC_CTL_A       0x64050
#define DDI_CRC_CTL_B       0x64150
#define DDI_CRC_CTL_C       0x64250
#define DDI_CRC_CTL_D       0x64350
#define DDI_CRC_CTL_E       0x64450

/* Pipe DDI Function Control */
#define PIPE_DDI_FUNC_CTL_A   0x60400 
#define PIPE_DDI_FUNC_CTL_B   0x61400 
#define PIPE_DDI_FUNC_CTL_C   0x62400 
#define PIPE_DDI_FUNC_CTL_EDP 0x6F400 

/* Pipe Configuration */
#define PIPE_CONF_A		0x70008
#define PIPE_CONF_B		0x71008
#define PIPE_CONF_C		0x72008
#define PIPE_CONF_EDP		0x7F008

/* Audio registers */
#define AUD_CONFIG_A          0x65000
#define AUD_MISC_CTRL_A       0x65010
#define AUD_VID_DID           0x65020
#define AUD_RID               0x65024
#define AUD_CTS_ENABLE_A      0x65028
#define AUD_PWRST             0x6504C
#define AUD_HDMIW_HDMIEDID_A  0x65050
#define AUD_HDMIW_INFOFR_A    0x65054
#define AUD_PORT_EN_HD_CFG    0x6507C
#define AUD_OUT_DIG_CNVT_A    0x65080
#define AUD_OUT_STR_DESC_A    0x65084
#define AUD_OUT_CHAN_MAP      0x65088
#define AUD_PINW_CONNLNG_LIST_A 0x650A8
#define AUD_PINW_CONNLNG_LIST_B 0x651A8
#define AUD_PINW_CONNLNG_LIST_C 0x652A8
#define AUD_PIPE_CONN_SEL_CTRL	0x650AC
#define AUD_PIN_ELD_CP_VLD    0x650C0
#define AUD_HDMIW_STATUS      0x650D4
#define AUD_CONFIG_B          0x65100
#define AUD_MISC_CTRL_B       0x65110
#define AUD_CTS_ENABLE_B      0x65128
#define AUD_HDMIW_HDMIEDID_B  0x65150
#define AUD_HDMIW_INFOFR_B    0x65154
#define AUD_OUT_DIG_CNVT_B    0x65180
#define AUD_OUT_STR_DESC_B    0x65184
#define AUD_CONFIG_C          0x65200
#define AUD_MISC_CTRL_C       0x65210
#define AUD_CTS_ENABLE_C      0x65228
#define AUD_HDMIW_HDMIEDID_C  0x65250
#define AUD_HDMIW_INFOFR_C    0x65254
#define AUD_OUT_DIG_CNVT_C    0x65280
#define AUD_OUT_STR_DESC_C    0x65284
#define AUD_DIP_ELD_CTRL_ST_A	0x650b4
#define AUD_DIP_ELD_CTRL_ST_B	0x651b4
#define AUD_DIP_ELD_CTRL_ST_C	0x652b4

/* Video DIP Control */
#define VIDEO_DIP_CTL_A		0x60200
#define VIDEO_DIP_CTL_B		0x61200
#define VIDEO_DIP_CTL_C		0x62200
#define VIDEO_DIP_CTL_D		0x63200

#define VIDEO_DIP_DATA		0x60220
#define VIDEO_DIP_ECC		0x60240

#define AUD_DP_DIP_STATUS	0x65f20


static void dump_hsw(void)
{
    uint32_t dword;
    int i;

    /* HSW DDI Buffer */
    dump_reg(DDI_BUF_CTL_A,		"DDI Buffer Controler A");
    dump_reg(DDI_BUF_CTL_B,		"DDI Buffer Controler B");
    dump_reg(DDI_BUF_CTL_C,		"DDI Buffer Controler C");
    dump_reg(DDI_BUF_CTL_D,		"DDI Buffer Controler D");
    dump_reg(DDI_BUF_CTL_E,		"DDI Buffer Controler E");
    
    /* HSW Pipe Function */
    dump_reg(PIPE_CONF_A,		"PIPE Configuration A");
    dump_reg(PIPE_CONF_B,		"PIPE Configuration B");
    dump_reg(PIPE_CONF_C,		"PIPE Configuration C");
    dump_reg(PIPE_CONF_EDP,		"PIPE Configuration EDP");

    dump_reg(PIPE_DDI_FUNC_CTL_A,	"PIPE DDI Function Control A");
    dump_reg(PIPE_DDI_FUNC_CTL_B,	"PIPE DDI Function Control B");
    dump_reg(PIPE_DDI_FUNC_CTL_C,	"PIPE DDI Function Control C");
    dump_reg(PIPE_DDI_FUNC_CTL_EDP,	"PIPE DDI Function Control EDP");
   
    /* HSW Display port */
    dump_reg(DP_TP_CTL_A,		"DisplayPort Transport A Control");
    dump_reg(DP_TP_CTL_B,		"DisplayPort Transport B Control");
    dump_reg(DP_TP_CTL_C,		"DisplayPort Transport C Control");
    dump_reg(DP_TP_CTL_D,		"DisplayPort Transport D Control");
    dump_reg(DP_TP_CTL_E,		"DisplayPort Transport E Control");

    dump_reg(DP_TP_ST_A,		"DisplayPort Transport A Status");
    dump_reg(DP_TP_ST_B,		"DisplayPort Transport B Status");
    dump_reg(DP_TP_ST_C,		"DisplayPort Transport C Status");
    dump_reg(DP_TP_ST_D,		"DisplayPort Transport D Status");
    dump_reg(DP_TP_ST_E,		"DisplayPort Transport E Status");

    /* HSW Transcoder A configuration */
    dump_reg(TRANS_CONF_A,		"Transcoder A Configuration");
    dump_reg(TRANS_CONF_B,		"Transcoder B Configuration");
    dump_reg(TRANS_CONF_C,		"Transcoder C Configuration");

    /* HSW North Display Audio */
    dump_reg(AUD_CONFIG_A,		"Audio Configuration - Transcoder A");
    dump_reg(AUD_CONFIG_B,		"Audio Configuration - Transcoder B");
    dump_reg(AUD_CONFIG_C,		"Audio Configuration - Transcoder C");
    dump_reg(AUD_MISC_CTRL_A,		"Audio MISC Control for Transcoder A");
    dump_reg(AUD_MISC_CTRL_B,		"Audio MISC Control for Transcoder B");
    dump_reg(AUD_MISC_CTRL_C,		"Audio MISC Control for Transcoder C");
    dump_reg(AUD_VID_DID,		"Audio Vendor ID / Device ID");
    dump_reg(AUD_RID,			"Audio Revision ID");
    dump_reg(AUD_CTS_ENABLE_A,		"Audio CTS Programming Enable - Transcoder A");
    dump_reg(AUD_CTS_ENABLE_B,		"Audio CTS Programming Enable - Transcoder B");
    dump_reg(AUD_CTS_ENABLE_C,		"Audio CTS Programming Enable - Transcoder C");
    dump_reg(AUD_PWRST,			"Audio Power State (Function Group, Convertor, Pin Widget)");
    dump_reg(AUD_HDMIW_HDMIEDID_A,	"HDMI Data EDID Block - Transcoder A");
    dump_reg(AUD_HDMIW_HDMIEDID_B,	"HDMI Data EDID Block - Transcoder B");
    dump_reg(AUD_HDMIW_HDMIEDID_C,	"HDMI Data EDID Block - Transcoder C");
    dump_reg(AUD_HDMIW_INFOFR_A,	"Audio Widget Data Island Packet - Transcoder A");
    dump_reg(AUD_HDMIW_INFOFR_B,	"Audio Widget Data Island Packet - Transcoder B");
    dump_reg(AUD_HDMIW_INFOFR_C,	"Audio Widget Data Island Packet - Transcoder C");

    dump_reg(AUD_PORT_EN_HD_CFG,	"Audio Pipe and Convert Configs");
    dump_reg(AUD_OUT_DIG_CNVT_A,	"Audio Digital Converter - Conv A");
    dump_reg(AUD_OUT_DIG_CNVT_B,	"Audio Digital Converter - Conv B");
    dump_reg(AUD_OUT_DIG_CNVT_C,	"Audio Digital Converter - Conv C");
    dump_reg(AUD_OUT_CHAN_MAP,		"Audio Output Channel Mapping");
    dump_reg(AUD_OUT_STR_DESC_A,	"Audio Stream Descriptor Format - Conv A");
    dump_reg(AUD_OUT_STR_DESC_B,	"Audio Stream Descriptor Format - Conv B");
    dump_reg(AUD_OUT_STR_DESC_C,	"Audio Stream Descriptor Format - Conv C");
    dump_reg(AUD_PINW_CONNLNG_LIST_A,	"Audio Connection List entry and Length - Transcoder A");
    dump_reg(AUD_PINW_CONNLNG_LIST_B,	"Audio Connection List entry and Length - Transcoder B");
    dump_reg(AUD_PINW_CONNLNG_LIST_C,	"Audio Connection List entry and Length - Transcoder C");
    dump_reg(AUD_PIPE_CONN_SEL_CTRL,	"Audio Pipe Connection Select Control");
    dump_reg(AUD_DIP_ELD_CTRL_ST_A,	"Audio DIP and ELD control state - Transcoder A");
    dump_reg(AUD_DIP_ELD_CTRL_ST_B,	"Audio DIP and ELD control state - Transcoder B");
    dump_reg(AUD_DIP_ELD_CTRL_ST_C,	"Audio DIP and ELD control state - Transcoder C");
    dump_reg(AUD_PIN_ELD_CP_VLD,	"audio pin eld valid status");
    dump_reg(AUD_HDMIW_STATUS,		"Audio HDMI FIFO Status");

    printf("\nDetails:\n\n");

    dword = INREG(AUD_VID_DID);
    printf("AUD_VID_DID vendor id\t\t\t\t\t0x%x\n", dword >> 16);
    printf("AUD_VID_DID device id\t\t\t\t\t0x%x\n", dword & 0xffff);

    dword = INREG(AUD_RID);
    printf("AUD_RID Major_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 23, 20));
    printf("AUD_RID Minor_Revision\t\t\t\t\t0x%lx\n", BITS(dword, 19, 16));
    printf("AUD_RID Revision_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 15, 8));
    printf("AUD_RID Stepping_Id\t\t\t\t\t0x%lx\n",    BITS(dword, 7, 0));

    dword = INREG(AUD_DIP_ELD_CTRL_ST_A);
    printf("Audio DIP and ELD control state for TranscoderA\n");
    printf("Audio DIP port select\t\t\t\t\t[0x%lx] %s\n",
		BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);

    printf("Audio DIP type enable status\t\t\t\t[0x%lx] %s\n",
 		BITS(dword, 24, 21), dip_type[BIT(dword, 21)]);

    printf("DIP Buffer Index \t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 20, 18), dip_index[BITS(dword, 20, 18)]);
    printf("DIP_transmission_frequency\t\t\t\t[0x%lx] %s\n",
					BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
    printf("ELD_ACK\t\t\t\t\t\t\t%lu\n", BIT(dword, 4));
    printf("ELD_buffer_size\t\t\t\t\t\t%lu\n", BITS(dword, 14, 10));

    dword = INREG(AUD_DIP_ELD_CTRL_ST_B);
    printf("Audio DIP and ELD control state for TranscoderB\n");
    printf("Audio DIP port select\t\t\t\t\t[0x%lx] %s\n",
		BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);

    printf("Audio DIP type enable status\t\t\t\t[0x%lx] %s\n",
 		BITS(dword, 24, 21), dip_type[BIT(dword, 21)]);

    printf("DIP Buffer Index \t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 20, 18), dip_index[BITS(dword, 20, 18)]);
    printf("DIP_transmission_frequency\t\t\t\t[0x%lx] %s\n",
					BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
    printf("ELD_ACK\t\t\t\t\t\t\t%lu\n", BIT(dword, 4));
    printf("ELD_buffer_size\t\t\t\t\t\t%lu\n", BITS(dword, 14, 10));

    dword = INREG(AUD_DIP_ELD_CTRL_ST_C);
    printf("Audio DIP and ELD control state for TranscoderC\n");
    printf("Audio DIP port select\t\t\t\t\t[0x%lx] %s\n",
		BITS(dword, 30, 29), dip_port[BITS(dword, 30, 29)]);

    printf("Audio DIP type enable status\t\t\t\t[0x%lx] %s\n",
 		BITS(dword, 24, 21), dip_type[BIT(dword, 21)]);

    printf("DIP Buffer Index \t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 20, 18), dip_index[BITS(dword, 20, 18)]);
    printf("DIP_transmission_frequency\t\t\t\t[0x%lx] %s\n",
					BITS(dword, 17, 16), dip_trans[BITS(dword, 17, 16)]);
    printf("ELD_ACK\t\t\t\t\t\t\t%lu\n", BIT(dword, 4));
    printf("ELD_buffer_size\t\t\t\t\t\t%lu\n", BITS(dword, 14, 10));
 
    dword = INREG(DDI_BUF_CTL_A);
    printf("DDI A Buffer control\n");
    printf("DDI Buffer Enable\t\t\t\t\t%ld\n", BIT(dword, 31));
    printf("DP port width\t\t\t\t\t\t[0x%lx] %s\n",
		BITS(dword, 3, 1), dp_port_width[BITS(dword, 3, 1)]);
    dword = INREG(DDI_BUF_CTL_B);
    printf("DDI B Buffer control\n");
    printf("DDI Buffer Enable\t\t\t\t\t%ld\n", BIT(dword, 31));
    printf("DP port width\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 3, 1), dp_port_width[BITS(dword, 3, 1)]);
    dword = INREG(DDI_BUF_CTL_C);
    printf("DDI C Buffer control\n");
    printf("DDI Buffer Enable\t\t\t\t\t%ld\n", BIT(dword, 31));
    printf("DP port width\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 3, 1), dp_port_width[BITS(dword, 3, 1)]);
    dword = INREG(DDI_BUF_CTL_D);
    printf("DDI D Buffer control\n");
    printf("DDI Buffer Enable\t\t\t\t\t%ld\n", BIT(dword, 31));
    printf("DP port width\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 3, 1), dp_port_width[BITS(dword, 3, 1)]);
    dword = INREG(DDI_BUF_CTL_E);
    printf("DDI E Buffer control\n");
    printf("DDI Buffer Enable\t\t\t\t\t%ld\n", BIT(dword, 31));
    printf("DP port width\t\t\t\t\t\t[0x%lx] %s\n",
			BITS(dword, 3, 1), dp_port_width[BITS(dword, 3, 1)]);

    dword = INREG(PIPE_DDI_FUNC_CTL_A);
    printf("Pipe A DDI Function Control\n");
    printf("PIPE DDI Function Enable\t\t\t\t[0x%lx]\n", BIT(dword, 31));
    printf("PIPE DDI selection\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 30, 28), 
		trans_to_port_sel[BITS(dword, 30, 28)]);
    printf("PIPE DDI Mode\t\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 26, 24), ddi_mode[BITS(dword, 26, 24)]);
    printf("BITS per color\t\t\t\t\t\t[0x%lx]\n", BITS(dword, 22, 20));

    dword = INREG(PIPE_DDI_FUNC_CTL_B);
    printf("Pipe B DDI Function Control\n");
    printf("PIPE DDI Function Enable\t\t\t\t[0x%lx]\n", BIT(dword, 31));
    printf("PIPE DDI selection\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 30, 28), 
		trans_to_port_sel[BITS(dword, 30, 28)]);
    printf("PIPE DDI Mode \t\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 26, 24), ddi_mode[BITS(dword, 26, 24)]);
    printf("BITS per color\t\t\t\t\t\t[0x%lx]\n", BITS(dword, 22, 20));

    dword = INREG(PIPE_DDI_FUNC_CTL_C);
    printf("Pipe C DDI Function Control\n");
    printf("PIPE DDI Function Enable\t\t\t\t[0x%lx]\n", BIT(dword, 31));
    printf("PIPE DDI selection\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 30, 28), 
		trans_to_port_sel[BITS(dword, 30, 28)]);
    printf("PIPE DDI Mode \t\t\t\t\t\t[0x%lx] %s\n", BITS(dword, 26, 24), ddi_mode[BITS(dword, 26, 24)]);
    printf("BITS per color\t\t\t\t\t\t[0x%lx]\n", BITS(dword, 22, 20));

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
    printf("AUD_OUT_DIG_CNVT_A  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
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
    printf("AUD_OUT_DIG_CNVT_B  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
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
    printf("AUD_OUT_DIG_CNVT_C  Lowest_Channel_Number\t\t%lu\n",BITS(dword, 19, 16));
    printf("AUD_OUT_DIG_CNVT_C  Stream_ID\t\t\t\t%lu\n",	BITS(dword, 23, 20));

    printf("AUD_OUT_CHAN_MAP  Converter_Channel_MAP	PORTB	PORTC	PORTD\n");
    for (i = 0; i < 8; i++) {
	    OUTREG(AUD_OUT_CHAN_MAP, i | (i << 8) | (i << 16));
	    dword = INREG(AUD_OUT_CHAN_MAP);
	    printf("\t\t\t\t%lu\t%lu\t%lu\t%lu\n",
		   1 + BITS(dword,  3,  0),
		   1 + BITS(dword,  7,  4),
		   1 + BITS(dword, 15, 12),
		   1 + BITS(dword, 23, 20));
    }

    dword = INREG(AUD_OUT_STR_DESC_A);
    printf("AUD_OUT_STR_DESC_A  Convertor_Channel_Count\t\t%lu\n", BITS(dword, 20, 16) + 1);
    printf("AUD_OUT_STR_DESC_A  Bits_per_Sample\t\t\t[%#lx] %s\n",
				BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));
    printf("AUD_OUT_STR_DESC_A  Number_of_Channels_in_a_Stream\t%lu\n", 1 + BITS(dword, 3, 0));

    dword = INREG(AUD_OUT_STR_DESC_B);
    printf("AUD_OUT_STR_DESC_B  Convertor_Channel_Count\t\t%lu\n", BITS(dword, 20, 16) + 1);
    printf("AUD_OUT_STR_DESC_B  Bits_per_Sample\t\t\t[%#lx] %s\n",
				BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));
    printf("AUD_OUT_STR_DESC_B  Number_of_Channels_in_a_Stream\t%lu\n", 1 + BITS(dword, 3, 0));

    dword = INREG(AUD_OUT_STR_DESC_C);
    printf("AUD_OUT_STR_DESC_C  Convertor_Channel_Count\t\t%lu\n", BITS(dword, 20, 16) + 1);
    printf("AUD_OUT_STR_DESC_C  Bits_per_Sample\t\t\t[%#lx] %s\n",
				BITS(dword, 6, 4), OPNAME(bits_per_sample, BITS(dword, 6, 4)));
    printf("AUD_OUT_STR_DESC_C  Number_of_Channels_in_a_Stream\t%lu\n", 1 + BITS(dword, 3, 0));

    dword = INREG(AUD_PINW_CONNLNG_SEL);
    printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_B\t%#lx\n", BITS(dword,  7,  0));
    printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_C\t%#lx\n", BITS(dword, 15,  8));
    printf("AUD_PINW_CONNLNG_SEL  Connection_select_Control_D\t%#lx\n", BITS(dword, 23, 16));

    dword = INREG(AUD_PIN_ELD_CP_VLD);
    printf("AUD_CNTRL_ST2  CP_ReadyB\t\t\t\t%lu\n",	BIT(dword, 1));
    printf("AUD_CNTRL_ST2  ELD_validB\t\t\t\t%lu\n",	BIT(dword, 0));
    printf("AUD_CNTRL_ST2  OUT_enableB\t\t\t\t%lu\n",	BIT(dword, 2));
    printf("AUD_CNTRL_ST2  CP_ReadyC\t\t\t\t%lu\n",	BIT(dword, 5));
    printf("AUD_CNTRL_ST2  ELD_validC\t\t\t\t%lu\n",	BIT(dword, 4));
    printf("AUD_CNTRL_ST2  OUT_enableC\t\t\t\t%lu\n",	BIT(dword, 6));
    printf("AUD_CNTRL_ST2  CP_ReadyD\t\t\t\t%lu\n",	BIT(dword, 9));
    printf("AUD_CNTRL_ST2  ELD_validD\t\t\t\t%lu\n",	BIT(dword, 8));
    printf("AUD_CNTRL_ST2  OUT_enableD\t\t\t\t%lu\n",	BIT(dword, 10));

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
    dword = INREG(AUD_DIP_ELD_CTRL_ST_A);
    dword &= ~BITMASK(9, 5);
    OUTREG(AUD_DIP_ELD_CTRL_ST_A, dword);
    for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
	    printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID_A)));
    printf("\n");

    printf("AUD_HDMIW_HDMIEDID_B HDMI ELD:\n\t");
    dword = INREG(AUD_DIP_ELD_CTRL_ST_B);
    dword &= ~BITMASK(9, 5);
    OUTREG(AUD_DIP_ELD_CTRL_ST_B, dword);
    for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
	    printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID_B)));
    printf("\n");

    printf("AUD_HDMIW_HDMIEDID_C HDMI ELD:\n\t");
    dword = INREG(AUD_DIP_ELD_CTRL_ST_C);
    dword &= ~BITMASK(9, 5);
    OUTREG(AUD_DIP_ELD_CTRL_ST_C, dword);
    for (i = 0; i < BITS(dword, 14, 10) / 4; i++)
	    printf("%08x ", htonl(INREG(AUD_HDMIW_HDMIEDID_C)));
    printf("\n");

    printf("AUD_HDMIW_INFOFR_A HDMI audio Infoframe:\n\t");
    dword = INREG(AUD_DIP_ELD_CTRL_ST_A);
    dword &= ~BITMASK(20, 18);
    dword &= ~BITMASK(3, 0);
    OUTREG(AUD_DIP_ELD_CTRL_ST_A, dword);
    for (i = 0; i < 8; i++)
	    printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR_A)));
    printf("\n");

    printf("AUD_HDMIW_INFOFR_B HDMI audio Infoframe:\n\t");
    dword = INREG(AUD_DIP_ELD_CTRL_ST_B);
    dword &= ~BITMASK(20, 18);
    dword &= ~BITMASK(3, 0);
    OUTREG(AUD_DIP_ELD_CTRL_ST_B, dword);
    for (i = 0; i < 8; i++)
	    printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR_B)));
    printf("\n");

    printf("AUD_HDMIW_INFOFR_C HDMI audio Infoframe:\n\t");
    dword = INREG(AUD_DIP_ELD_CTRL_ST_C);
    dword &= ~BITMASK(20, 18);
    dword &= ~BITMASK(3, 0);
    OUTREG(AUD_DIP_ELD_CTRL_ST_C, dword);
    for (i = 0; i < 8; i++)
	    printf("%08x ", htonl(INREG(AUD_HDMIW_INFOFR_C)));
    printf("\n");
}

int main(int argc, char **argv)
{
	struct pci_device *pci_dev;

	pci_dev = intel_get_pci_device();
	devid = pci_dev->device_id; /* XXX not true when mapping! */

	do_self_tests();

	if (argc == 2)
		intel_map_file(argv[1]);
	else
		intel_get_mmio(pci_dev);

	if (IS_GEN6(devid) || IS_GEN7(devid) || getenv("HAS_PCH_SPLIT")) {
		if (IS_HASWELL(devid)) {
			printf("Haswell audio registers:\n\n");
			dump_hsw();
			return 0;
		}
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
