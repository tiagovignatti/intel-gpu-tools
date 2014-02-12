/*
 * Copyright © 2012 Intel Corporation
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
 *      Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "intel_io.h"
#include "intel_chipset.h"
#include "drmtest.h"

typedef enum {
	TRANSC_A = 0,
	TRANSC_B = 1,
	TRANSC_C = 2,
	TRANSC_INVALID
} Transcoder;

typedef enum {
	REG_HDMIB_GEN4    = 0x61140,
	REG_HDMIC_GEN4    = 0x61160,
	REG_HDMIB_VLV     = 0x1e1140,
	REG_HDMIC_VLV     = 0x1e1160,
	REG_HDMIB_PCH     = 0xe1140,
	REG_HDMIC_PCH     = 0xe1150,
	REG_HDMID_PCH     = 0xe1160,
	REG_DIP_CTL_GEN4  = 0x61170,
	REG_DIP_CTL_A_VLV   = 0x1e0200,
	REG_DIP_CTL_B_VLV   = 0x1e1170,
	REG_DIP_CTL_A     = 0xe0200,
	REG_DIP_CTL_B     = 0xe1200,
	REG_DIP_CTL_C     = 0xe2200,
	REG_DIP_DATA_GEN4 = 0x61178,
	REG_DIP_DATA_A_VLV  = 0x1e0208,
	REG_DIP_DATA_B_VLV  = 0x1e1174,
	REG_DIP_DATA_A    = 0xe0208,
	REG_DIP_DATA_B    = 0xe1208,
	REG_DIP_DATA_C    = 0xe2208,
} Register;

typedef enum {
	DIP_AVI    = 0,
	DIP_VENDOR = 1,
	DIP_GAMUT  = 2,
	DIP_SPD    = 3,
	DIP_INVALID,
} DipType;

typedef enum {
	DIP_FREQ_ONCE              = 0,
	DIP_FREQ_EVERY_VSYNC       = 1,
	DIP_FREQ_EVERY_OTHER_VSYNC = 2,
	DIP_FREQ_RESERVED          = 3,
} DipFrequency;

typedef enum {
	SOURCE_DEVICE_UNKNOWN           = 0x00,
	SOURCE_DEVICE_DIGITAL_STB       = 0x01,
	SOURCE_DEVICE_DVD_PLAYER        = 0x02,
	SOURCE_DEVICE_D_VHS             = 0x03,
	SOURCE_DEVICE_HDD_VIDEORECORDER = 0x04,
	SOURCE_DEVICE_DVC               = 0x05,
	SOURCE_DEVICE_DSC               = 0x06,
	SOURCE_DEVICE_VIDEO_CD          = 0x07,
	SOURCE_DEVICE_GAME              = 0x08,
	SOURCE_DEVICE_PC_GENERAL        = 0x09,
	SOURCE_DEVICE_BLU_RAY_DISK      = 0x0a,
	SOURCE_DEVICE_SUPER_AUDIO_CD    = 0x0b,
	SOURCE_DEVICE_RESERVED          = 0x0c
} SourceDevice;

#define HDMI_PORT_ENABLE          (1 << 31)
#define HDMI_PORT_TRANSCODER_GEN4 (1 << 30)
#define HDMI_PORT_TRANSCODER_IBX  (1 << 30)
#define HDMI_PORT_TRANSCODER_CPT  (3 << 29)
#define HDMI_PORT_ENCODING        (3 << 10)
#define HDMI_PORT_MODE            (1 << 9)
#define HDMI_PORT_AUDIO           (1 << 6)
#define HDMI_PORT_DETECTED        (1 << 2)

#define DIP_CTL_ENABLE           (1 << 31)
#define DIP_CTL_GCP_ENABLE       (1 << 25)
#define DIP_CTL_SPD_ENABLE       (1 << 24)
#define DIP_CTL_GAMUT_ENABLE     (1 << 23)
#define DIP_CTL_VENDOR_ENABLE    (1 << 22)
#define DIP_CTL_AVI_ENABLE       (1 << 21)
#define DIP_CTL_BUFFER_INDEX     (3 << 19)
#define DIP_CTL_BUFFER_AVI       (0 << 19)
#define DIP_CTL_BUFFER_VENDOR    (1 << 19)
#define DIP_CTL_BUFFER_GAMUT     (2 << 19)
#define DIP_CTL_BUFFER_SPD       (3 << 19)
#define DIP_CTL_FREQUENCY        (3 << 16)
#define DIP_CTL_FREQ_ONCE        (0 << 16)
#define DIP_CTL_FREQ_EVERY       (1 << 16)
#define DIP_CTL_FREQ_EVERY_OTHER (2 << 16)
#define DIP_CTL_BUFFER_SIZE      (15 << 8)
#define DIP_CTL_ACCESS_ADDR      (15 << 0)

#define DIP_CTL_PORT_SEL_MASK_GEN4       (3 << 29)
#define DIP_CTL_PORT_SEL_B_GEN4          (1 << 29)
#define DIP_CTL_PORT_SEL_C_GEN4          (2 << 29)
#define DIP_CTL_BUFFER_TRANS_ACTIVE_GEN4 (1 << 28)

#define AVI_INFOFRAME_TYPE    0x82
#define AVI_INFOFRAME_VERSION 0x02
#define AVI_INFOFRAME_LENGTH  0x0d
#define SPD_INFOFRAME_TYPE    0x83
#define SPD_INFOFRAME_VERSION 0x01
#define SPD_INFOFRAME_LENGTH  0x19

#define VENDOR_ID_HDMI	0x000c03

typedef struct {
	uint8_t type;
	uint8_t version;
	uint8_t length;
	uint8_t ecc;
} DipInfoFrameHeader;

typedef union {
	struct {
		DipInfoFrameHeader header;
		uint8_t checksum;

		uint8_t S     :2;
		uint8_t B     :2;
		uint8_t A     :1;
		uint8_t Y     :2;
		uint8_t Rsvd0 :1;

		uint8_t R :4;
		uint8_t M :2;
		uint8_t C :2;

		uint8_t SC  :2;
		uint8_t Q   :2;
		uint8_t EC  :3;
		uint8_t ITC :1;

		uint8_t VIC   :7;
		uint8_t Rsvd1 :1;

		uint8_t PR    :4;
		uint8_t Rsvd2 :4;

		uint16_t top;
		uint16_t bottom;
		uint16_t left;
		uint16_t right;

		uint16_t Rsvd3;
		uint32_t Rsvd4[3];
	} avi;
	struct {
		DipInfoFrameHeader header;
		uint8_t checksum;
		uint8_t vendor[8];
		uint8_t description[16];
		uint8_t source;
	} __attribute__((packed)) spd;
	struct {
		DipInfoFrameHeader header;
		uint8_t checksum;

		uint8_t id[3];

		uint8_t Rsvd0        :5;
		uint8_t video_format :3;

		union {
			uint8_t vic;
			struct {
				uint8_t Rsvd1         :4;
				uint8_t s3d_structure :4;
			} s3d;
		} pb5;

		uint8_t Rsvd2        :4;
		uint8_t s3d_ext_data :4;
	} __attribute__((packed)) vendor;
	struct {
		DipInfoFrameHeader header;
		uint8_t body[27];
	} generic;
	uint8_t data8[128];
	uint32_t data32[16];
} DipInfoFrame;

Register vlv_hdmi_ports[] = {
	REG_HDMIB_VLV,
	REG_HDMIC_VLV,
};

Register vlv_dip_ctl_regs[] = {
	REG_DIP_CTL_A_VLV,
	REG_DIP_CTL_B_VLV,
};

Register vlv_dip_data_regs[] = {
	REG_DIP_DATA_A_VLV,
	REG_DIP_DATA_B_VLV,
};

Register gen4_hdmi_ports[] = {
	REG_HDMIB_GEN4,
	REG_HDMIC_GEN4,
};
Register pch_hdmi_ports[] = {
	REG_HDMIB_PCH,
	REG_HDMIC_PCH,
	REG_HDMID_PCH
};
Register pch_dip_ctl_regs[] = {
	REG_DIP_CTL_A,
	REG_DIP_CTL_B,
	REG_DIP_CTL_C
};
Register pch_dip_data_regs[] = {
	REG_DIP_DATA_A,
	REG_DIP_DATA_B,
	REG_DIP_DATA_C
};
const char *hdmi_port_names[] = {
	"HDMIB",
	"HDMIC",
	"HDMID"
};
const char *transcoder_names[] = {
	"A",
	"B",
	"C"
};
const char *dip_frequency_names[] = {
	"once",
	"every vsync",
	"every other vsync",
	"reserved (invalid)"
};

struct pci_device *pci_dev;
int gen = 0;

static const char *spd_source_to_string(SourceDevice source)
{
	switch (source) {
	case SOURCE_DEVICE_UNKNOWN:
		return "unknown";
	case SOURCE_DEVICE_DIGITAL_STB:
		return "digital stb";
	case SOURCE_DEVICE_DVD_PLAYER:
		return "dvd player";
	case SOURCE_DEVICE_D_VHS:
		return "d vhs";
	case SOURCE_DEVICE_HDD_VIDEORECORDER:
		return "hdd videorecorder";
	case SOURCE_DEVICE_DVC:
		return "dvc";
	case SOURCE_DEVICE_DSC:
		return "dsc";
	case SOURCE_DEVICE_VIDEO_CD:
		return "video cd";
	case SOURCE_DEVICE_GAME:
		return "game";
	case SOURCE_DEVICE_PC_GENERAL:
		return "pc general";
	case SOURCE_DEVICE_BLU_RAY_DISK:
		return "blu-ray disk";
	case SOURCE_DEVICE_SUPER_AUDIO_CD:
		return "super audio cd";
	default:
		return "reserved";
	}
}

static Register get_dip_ctl_reg(Transcoder transcoder)
{
	if (IS_VALLEYVIEW(pci_dev->device_id))
		return vlv_dip_ctl_regs[transcoder];
	else if (gen == 4)
		return REG_DIP_CTL_GEN4;
	else
		return pch_dip_ctl_regs[transcoder];
}

static Register get_dip_data_reg(Transcoder transcoder)
{
	if (IS_VALLEYVIEW(pci_dev->device_id))
		return vlv_dip_data_regs[transcoder];
	else if (gen == 4)
		return REG_DIP_DATA_GEN4;
	else
		return pch_dip_data_regs[transcoder];
}

static Register get_hdmi_port(int hdmi_port_index)
{
	if (IS_VALLEYVIEW(pci_dev->device_id))
		return vlv_hdmi_ports[hdmi_port_index];
	else if (gen == 4) {
		assert(hdmi_port_index < 2);
		return gen4_hdmi_ports[hdmi_port_index];
	} else {
		return pch_hdmi_ports[hdmi_port_index];
	}
}

static void load_infoframe(Transcoder transcoder, DipInfoFrame *frame,
			   DipType type)
{
	Register ctl_reg = get_dip_ctl_reg(transcoder);
	Register data_reg = get_dip_data_reg(transcoder);
	uint32_t ctl_val;
	uint32_t i;

	ctl_val = INREG(ctl_reg);

	ctl_val &= ~DIP_CTL_BUFFER_INDEX;
	ctl_val |= type << 19;
	OUTREG(ctl_reg, ctl_val);
	ctl_val = INREG(ctl_reg);

	ctl_val &= ~DIP_CTL_ACCESS_ADDR;
	OUTREG(ctl_reg, ctl_val);

	for (i = 0; i < 16; i++) {
		ctl_val = INREG(ctl_reg);
		assert((ctl_val & DIP_CTL_ACCESS_ADDR) == i);
		frame->data32[i] = INREG(data_reg);
	}
}

static int infoframe_valid_checksum(DipInfoFrame *frame)
{
	int i;
	int length = frame->generic.header.length;
	uint8_t csum;

	csum = frame->generic.header.type + frame->generic.header.version +
	       frame->generic.header.length; /* no ecc */
	for (i = 0; i < length + 1; i++)
		csum += frame->generic.body[i];

	return (csum == 0);
}

static void infoframe_fix_checksum(DipInfoFrame *frame)
{
	int i;
	int length = frame->generic.header.length;
	uint8_t csum;

	csum = frame->generic.header.type + frame->generic.header.version +
	       frame->generic.header.length; /* no ecc */
	/* Length does not include the header field nor the checksum */
	for (i = 1; i < length + 1; i++)
		csum += frame->generic.body[i];
	frame->generic.body[0] = 0x100 - csum;
}

static void dump_port_info(int hdmi_port_index)
{
	Register port = get_hdmi_port(hdmi_port_index);
	uint32_t val = INREG(port);
	Transcoder transcoder;

	printf("\nPort %s:\n", hdmi_port_names[hdmi_port_index]);
	printf("- %sdetected\n", val & HDMI_PORT_DETECTED ? "" : "not ");
	printf("- %s\n", val & HDMI_PORT_ENABLE ? "enabled" : "disabled");

	if (!(val & HDMI_PORT_ENABLE))
		return;

	if (gen == 4 || IS_VALLEYVIEW(pci_dev->device_id))
		transcoder = (val & HDMI_PORT_TRANSCODER_GEN4) >> 30;
	else if (intel_pch >= PCH_CPT)
		transcoder = (val & HDMI_PORT_TRANSCODER_CPT) >> 29;
	else
		transcoder = (val & HDMI_PORT_TRANSCODER_IBX) >> 30;
	printf("- transcoder: %s\n", transcoder_names[transcoder]);

	switch ((val & HDMI_PORT_ENCODING) >> 10) {
	case 0:
		printf("- mode: SDVO\n");
		break;
	case 2:
		printf("- mode: TMDS\n");
		break;
	default:
		printf("- mode: INVALID!\n");
	}

	printf("- mode: %s\n", val & HDMI_PORT_MODE ? "HDMI" : "DVI");
	printf("- audio: %s\n", val & HDMI_PORT_AUDIO ? "enabled" : "disabled");
}

static void dump_raw_infoframe(DipInfoFrame *frame)
{
	unsigned int i;
	printf("- raw:");
	for (i = 0; i < 16; i++) {
		if (i % 4 == 0)
			printf("\n ");
		printf(" %08x", frame->data32[i]);
	}
	printf("\n");
}

static void dump_avi_info(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val;
	DipFrequency freq;
	DipInfoFrame frame;

	load_infoframe(transcoder, &frame, DIP_AVI);
	val = INREG(reg);

	printf("AVI InfoFrame:\n");

	if (gen == 4) {
		printf("- %sbeing transmitted\n",
		       val & DIP_CTL_BUFFER_TRANS_ACTIVE_GEN4 ? "" : "not ");
	}

	freq = (val & DIP_CTL_FREQUENCY) >> 16;
	printf("- frequency: %s\n", dip_frequency_names[freq]);

	dump_raw_infoframe(&frame);

	printf("- type: %x, version: %x, length: %x, ecc: %x, checksum: %x\n",
	       frame.avi.header.type, frame.avi.header.version,
	       frame.avi.header.length, frame.avi.header.ecc,
	       frame.avi.checksum);
	printf("- S: %x, B: %x, A: %x, Y: %x, Rsvd0: %x\n",
	       frame.avi.S, frame.avi.B, frame.avi.A, frame.avi.Y,
	       frame.avi.Rsvd0);
	printf("- R: %x, M: %x, C: %x\n",
	       frame.avi.R, frame.avi.M, frame.avi.C);
	printf("- SC: %x, Q: %x, EC: %x, ITC: %x\n",
	       frame.avi.SC, frame.avi.Q, frame.avi.EC, frame.avi.ITC);
	printf("- VIC: %d, Rsvd1: %x\n", frame.avi.VIC, frame.avi.Rsvd1);
	printf("- PR: %x, Rsvd2: %x\n", frame.avi.PR, frame.avi.Rsvd2);
	printf("- top: %x, bottom: %x, left: %x, right: %x\n",
	       frame.avi.top, frame.avi.bottom, frame.avi.left,
	       frame.avi.right);
	printf("- Rsvd3: %x, Rsvd4[0]: %x, Rsvd4[1]: %x, Rsvd4[2]: %x\n",
	       frame.avi.Rsvd3, frame.avi.Rsvd4[0], frame.avi.Rsvd4[1],
	       frame.avi.Rsvd4[2]);

	if (!infoframe_valid_checksum(&frame))
		printf("Invalid InfoFrame checksum!\n");
}

static const char *vendor_id_to_string(uint32_t id)
{
	switch (id) {
	case VENDOR_ID_HDMI:
		return "HDMI";
	default:
		return "Unknown";
	}
}

static const char *s3d_structure_to_string(int format)
{
	switch (format) {
	case 0:
		return "Frame Packing";
	case 6:
		return "Top Bottom";
	case 8:
		return "Side By Side (half)";
	default:
		return "Reserved";
	}
}

static void dump_vendor_hdmi(DipInfoFrame *frame)
{
	int vic_present = frame->vendor.video_format & 0x1;
	int s3d_present = frame->vendor.video_format & 0x2;

	printf("- video format: 0x%03x %s\n", frame->vendor.video_format,
	       s3d_present ? "(3D)" : "");

	if (vic_present && s3d_present) {
		printf("Error: HDMI VIC and S3D bits set. Only one of those "
		       " at a time is valid\n");
		return;
	}

	if (vic_present)
		printf("- HDMI VIC: %d\n", frame->vendor.pb5.vic);
	else if (s3d_present) {
		int s3d_structure = frame->vendor.pb5.s3d.s3d_structure;

		printf("- 3D Format: %s\n",
		       s3d_structure_to_string(s3d_structure));

		/* Side-by-side (half) */
		if (s3d_structure >= 8)
			printf("- 3D Ext Data 0x%x\n",
			       frame->vendor.s3d_ext_data);
	}
}

static void dump_vendor_info(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val, vendor_id;
	DipFrequency freq;
	DipInfoFrame frame;

	load_infoframe(transcoder, &frame, DIP_VENDOR);
	val = INREG(reg);

	printf("Vendor InfoFrame:\n");

	if (gen == 4) {
		printf("- %sbeing transmitted\n",
		       val & DIP_CTL_BUFFER_TRANS_ACTIVE_GEN4 ? "" : "not ");
	}

	freq = (val & DIP_CTL_FREQUENCY) >> 16;
	printf("- frequency: %s\n", dip_frequency_names[freq]);

	dump_raw_infoframe(&frame);

	vendor_id = frame.vendor.id[2] << 16 | frame.vendor.id[1] << 8 |
		    frame.vendor.id[0];

	printf("- vendor Id: 0x%06x (%s)\n", vendor_id,
	       vendor_id_to_string(vendor_id));

	if (vendor_id == VENDOR_ID_HDMI)
		dump_vendor_hdmi(&frame);

	if (!infoframe_valid_checksum(&frame))
		printf("Invalid InfoFrame checksum!\n");
}

static void dump_gamut_info(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val;
	DipFrequency freq;
	DipInfoFrame frame;

	load_infoframe(transcoder, &frame, DIP_GAMUT);
	val = INREG(reg);

	printf("Gamut InfoFrame:\n");

	if (gen == 4) {
		printf("- %sbeing transmitted\n",
		       val & DIP_CTL_BUFFER_TRANS_ACTIVE_GEN4 ? "" : "not ");
	}

	freq = (val & DIP_CTL_FREQUENCY) >> 16;
	printf("- frequency: %s\n", dip_frequency_names[freq]);

	dump_raw_infoframe(&frame);

	if (!infoframe_valid_checksum(&frame))
		printf("Invalid InfoFrame checksum!\n");
}

static void dump_spd_info(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val;
	DipFrequency freq;
	DipInfoFrame frame;
	char vendor[9];
	char description[17];

	load_infoframe(transcoder, &frame, DIP_SPD);
	val = INREG(reg);

	printf("SPD InfoFrame:\n");

	if (gen == 4) {
		printf("- %sbeing transmitted\n",
		       val & DIP_CTL_BUFFER_TRANS_ACTIVE_GEN4 ? "" : "not ");
	}

	freq = (val & DIP_CTL_FREQUENCY) >> 16;
	printf("- frequency: %s\n", dip_frequency_names[freq]);

	dump_raw_infoframe(&frame);

	printf("- type: %x, version: %x, length: %x, ecc: %x, checksum: %x\n",
	       frame.spd.header.type, frame.spd.header.version,
	       frame.spd.header.length, frame.spd.header.ecc,
	       frame.spd.checksum);

	memcpy(vendor, frame.spd.vendor, 8);
	vendor[8] = '\0';
	memcpy(description, frame.spd.description, 16);
	description[16] = '\0';

	printf("- vendor: %s\n", vendor);
	printf("- description: %s\n", description);
	printf("- source: %s\n", spd_source_to_string(frame.spd.source));

	if (!infoframe_valid_checksum(&frame))
		printf("Invalid InfoFrame checksum!\n");
}

static void dump_transcoder_info(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val = INREG(reg);

	if (gen == 4) {
		printf("\nDIP information:\n");
		switch (val & DIP_CTL_PORT_SEL_MASK_GEN4) {
		case DIP_CTL_PORT_SEL_B_GEN4:
			printf("- port B\n");
			break;
		case DIP_CTL_PORT_SEL_C_GEN4:
			printf("- port C\n");
			break;
		default:
			printf("- INVALID port!\n");
		}
	} else {
		printf("\nTranscoder %s:\n", transcoder_names[transcoder]);
	}
	printf("- %s\n", val & DIP_CTL_ENABLE ? "enabled" : "disabled");
	if (!(val & DIP_CTL_ENABLE))
		return;

	printf("- GCP: %s\n", val & DIP_CTL_GCP_ENABLE ?
	       "enabled" : "disabled");

	if (val & DIP_CTL_AVI_ENABLE)
		dump_avi_info(transcoder);
	if (val & DIP_CTL_VENDOR_ENABLE)
		dump_vendor_info(transcoder);
	if (val & DIP_CTL_GAMUT_ENABLE)
		dump_gamut_info(transcoder);
	if (val & DIP_CTL_SPD_ENABLE)
		dump_spd_info(transcoder);
}

static void dump_all_info(void)
{
	unsigned int i;

	if (IS_VALLEYVIEW(pci_dev->device_id)) {
		for (i = 0; i < ARRAY_SIZE(vlv_hdmi_ports); i++)
			dump_port_info(i);
		for (i = 0; i < ARRAY_SIZE(vlv_dip_ctl_regs); i++)
			dump_transcoder_info(i);
	} else if (gen == 4) {
		for (i = 0; i < ARRAY_SIZE(gen4_hdmi_ports); i++)
			dump_port_info(i);
		dump_transcoder_info(0);
	} else {
		for (i = 0; i < ARRAY_SIZE(pch_hdmi_ports); i++)
			dump_port_info(i);
		for (i = 0; i < ARRAY_SIZE(pch_dip_ctl_regs); i++)
			dump_transcoder_info(i);
	}
}

static void write_infoframe(Transcoder transcoder, DipType type,
			    DipInfoFrame *frame)
{
	Register ctl_reg = get_dip_ctl_reg(transcoder);
	Register data_reg = get_dip_data_reg(transcoder);
	uint32_t ctl_val;
	unsigned int i;

	ctl_val = INREG(ctl_reg);
	ctl_val &= ~DIP_CTL_BUFFER_INDEX;
	ctl_val |= (type << 19);
	ctl_val &= ~DIP_CTL_ACCESS_ADDR;
	OUTREG(ctl_reg, ctl_val);

	for (i = 0; i < 8; i++) {
		ctl_val = INREG(ctl_reg);
		assert((ctl_val & DIP_CTL_ACCESS_ADDR) == i);
		OUTREG(data_reg, frame->data32[i]);
	}
}

static void disable_infoframe(Transcoder transcoder, DipType type)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val = INREG(reg);
	if (gen != 4 && type == DIP_AVI)
		val &= ~DIP_CTL_ENABLE;
	val &= ~(1 << (21 + type));
	OUTREG(reg, val);
}

static void enable_infoframe(Transcoder transcoder, DipType type)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val = INREG(reg);
	if (gen != 4 && type == DIP_AVI)
		val |= DIP_CTL_ENABLE;
	val |= (1 << (21 + type));
	OUTREG(reg, val);
}

static int parse_infoframe_option_u(const char *name, const char *s,
				    uint32_t min, uint32_t max,
				    uint32_t *value, char **commands)
{
	int read, rc;
	if (!strcmp(name, s)) {
		rc = sscanf(*commands, "%x%n", value, &read);
		*commands = &(*commands)[read];
		if (rc != 1) {
			printf("Invalid value.\n");
			return 0;
		}

		if (*value < min || *value > max) {
			printf("Value outside allowed range.\n");
			return 0;
		}
		return 1;
	}
	return 0;
}

static int parse_infoframe_option_s(const char *name, const char *s,
				    int min_size, int max_size,
				    char *value, char **commands)
{
	int size, read, rc;
	if (!strcmp(name, s)) {
		rc = sscanf(*commands, "%31s%n", value, &read);
		*commands = &(*commands)[read];
		if (rc != 1) {
			printf("Invalid value.\n");
			return 0;
		}

		size = strlen(value);
		if (size < min_size || size > max_size) {
			printf("String either too big or too small.\n");
			return 0;
		}
		return 1;
	}
	return 0;
}

static void change_avi_infoframe(Transcoder transcoder, char *commands)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val;
	DipInfoFrame frame;
	char option[32];
	uint32_t option_val;
	int rc, read;
	char *current = commands;

	load_infoframe(transcoder, &frame, DIP_AVI);
	val = INREG(reg);

	while (1) {
		rc = sscanf(current, "%31s%n", option, &read);
		current = &current[read];
		if (rc == EOF) {
			break;
		} else if (rc != 1) {
			printf("Invalid option: %s\n", option);
			continue;
		}

		if (parse_infoframe_option_u("S", option, 0, 2, &option_val,
					     &current))
			frame.avi.S = option_val;
		else if (parse_infoframe_option_u("B", option, 0, 3,
						  &option_val, &current))
			frame.avi.B = option_val;
		else if (parse_infoframe_option_u("A", option, 0, 1,
						  &option_val, &current))
			frame.avi.A = option_val;
		else if (parse_infoframe_option_u("Y", option, 0, 2,
						  &option_val, &current))
			frame.avi.Y = option_val;
		else if (parse_infoframe_option_u("R", option, 0, 15,
						  &option_val, &current))
			frame.avi.R = option_val;
		else if (parse_infoframe_option_u("M", option, 0, 2,
						  &option_val, &current))
			frame.avi.M = option_val;
		else if (parse_infoframe_option_u("C", option, 0, 3,
						  &option_val, &current))
			frame.avi.C = option_val;
		else if (parse_infoframe_option_u("SC", option, 0, 3,
						  &option_val, &current))
			frame.avi.SC = option_val;
		else if (parse_infoframe_option_u("Q", option, 0, 2,
						  &option_val, &current))
			frame.avi.Q = option_val;
		else if (parse_infoframe_option_u("EC", option, 0, 1,
						  &option_val,&current))
			frame.avi.EC = option_val;
		else if (parse_infoframe_option_u("ITC", option, 0, 1,
						  &option_val, &current))
			frame.avi.ITC = option_val;
		else if (parse_infoframe_option_u("VIC", option, 0, 127,
						  &option_val, &current))
			frame.avi.VIC = option_val;
		else if (parse_infoframe_option_u("PR", option, 0, 15,
						  &option_val, &current))
			frame.avi.PR = option_val;
		else if (parse_infoframe_option_u("top", option, 0, 65535,
						  &option_val, &current))
			frame.avi.top = option_val;
		else if (parse_infoframe_option_u("bottom", option, 0, 65535,
						  &option_val, &current))
			frame.avi.bottom = option_val;
		else if (parse_infoframe_option_u("left", option, 0, 65535,
						  &option_val, &current))
			frame.avi.left = option_val;
		else if (parse_infoframe_option_u("right", option, 0, 65535,
						  &option_val, &current))
			frame.avi.right = option_val;
		else
			printf("Unrecognized option: %s\n", option);
	}

	val &= ~DIP_CTL_FREQUENCY;
	val |= DIP_CTL_FREQ_EVERY;
	OUTREG(reg, val);

	frame.avi.header.type = AVI_INFOFRAME_TYPE;
	frame.avi.header.version = AVI_INFOFRAME_VERSION;
	frame.avi.header.length = AVI_INFOFRAME_LENGTH;
	frame.avi.Rsvd0 = 0;
	frame.avi.Rsvd1 = 0;
	frame.avi.Rsvd2 = 0;
	frame.avi.Rsvd3 = 0;
	frame.avi.Rsvd4[0] = 0;
	frame.avi.Rsvd4[1] = 0;
	frame.avi.Rsvd4[2] = 0;

	infoframe_fix_checksum(&frame);

	disable_infoframe(transcoder, DIP_AVI);
	write_infoframe(transcoder, DIP_AVI, &frame);
	enable_infoframe(transcoder, DIP_AVI);
}

static void change_spd_infoframe(Transcoder transcoder, char *commands)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val;
	DipInfoFrame frame;
	char option[16];
	char option_val_s[32];
	uint32_t option_val_i;
	int rc, read;
	char *current = commands;

	load_infoframe(transcoder, &frame, DIP_SPD);
	val = INREG(reg);

	while (1) {
		rc = sscanf(current, "%15s%n", option, &read);
		current = &current[read];
		if (rc == EOF) {
			break;
		} else if (rc != 1) {
			printf("Invalid option: %s\n", option);
			continue;
		}

		memset(option_val_s, 0, 32);

		if (parse_infoframe_option_s("vendor", option, 0, 8,
					     option_val_s, &current))
			memcpy(frame.spd.vendor, option_val_s, 8);
		else if (parse_infoframe_option_s("description", option, 0, 16,
						  option_val_s, &current))
			memcpy(frame.spd.description, option_val_s, 16);
		else if (parse_infoframe_option_u("source", option, 0, 0x0c,
						  &option_val_i, &current))
			frame.spd.source = option_val_i;
		else
			printf("Unrecognized option: %s\n", option);
	}

	val &= ~DIP_CTL_FREQUENCY;
	val |= DIP_CTL_FREQ_EVERY_OTHER;
	OUTREG(reg, val);

	frame.spd.header.type = SPD_INFOFRAME_TYPE;
	frame.spd.header.version = SPD_INFOFRAME_VERSION;
	frame.spd.header.length = SPD_INFOFRAME_LENGTH;

	infoframe_fix_checksum(&frame);

	disable_infoframe(transcoder, DIP_SPD);
	write_infoframe(transcoder, DIP_SPD, &frame);
	enable_infoframe(transcoder, DIP_SPD);
}

static void change_infoframe_checksum(Transcoder transcoder, DipType type,
				      uint32_t selected_csum)
{
	DipInfoFrame frame;

	load_infoframe(transcoder, &frame, type);
	frame.generic.body[0] = selected_csum;
	disable_infoframe(transcoder, type);
	write_infoframe(transcoder, type, &frame);
	enable_infoframe(transcoder, type);
}

static void change_infoframe_frequency(Transcoder transcoder, DipType type,
				       DipFrequency frequency)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val = INREG(reg);

	if (type == DIP_AVI && frequency != DIP_FREQ_EVERY_VSYNC) {
		printf("Error: AVI infoframe must be sent every VSync!\n");
		frequency = DIP_FREQ_EVERY_VSYNC;
	}

	val &= ~DIP_CTL_FREQUENCY;
	val |= (frequency << 16);
	OUTREG(reg, val);
}

static void disable_dip(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val = INREG(reg);
	val &= ~DIP_CTL_ENABLE;
	OUTREG(reg, val);
}

static void enable_dip(Transcoder transcoder)
{
	Register reg = get_dip_ctl_reg(transcoder);
	uint32_t val = INREG(reg);
	val |= DIP_CTL_ENABLE;
	OUTREG(reg, val);
}

static void disable_hdmi_port(Register reg)
{
	uint32_t val = INREG(reg);
	val &= ~HDMI_PORT_ENABLE;
	OUTREG(reg, val);
}

static void enable_hdmi_port(Register reg)
{
	uint32_t val = INREG(reg);
	val |= HDMI_PORT_ENABLE;
	OUTREG(reg, val);
}

static void print_usage(void)
{
printf("Options:\n"
"  -d, --dump\n"
"          dump information about all transcoders\n"
"  -c, --change-fields [fields]\n"
"          change infoframe fields from selected transcoder\n"
"  -k, --change-checksum [checksum]\n"
"          change infoframe checksum (value in hex)\n"
"  -q, --change-frequency [frequency]\n"
"          change infoframe frequency (once, everyvsync or everyothervsync)\n"
"  -n, --disable\n"
"          disable the selected infoframe from the selected transcoder\n"
"  -N, --enable\n"
"          enable the selected infoframe from the selected transcoder\n"
"  -x, --disable-infoframes\n"
"          disable all infoframes from selected transcoder\n"
"  -X, --enable-infoframes\n"
"          enable sending infoframes on the selected transcoder\n"
"  -p, --disable-hdmi-port [port]\n"
"          disable hdmi port on the selected transcoder (B, C or D)\n"
"  -P, --enable-hdmi-port [port]\n"
"          enable hdmi port on the selected transcoder (B, C or D)\n"
"  -t, --transcoder\n"
"          select transcoder (A, B or C)\n"
"  -f, --infoframe\n"
"          select infoframe (AVI, Vendor, Gamut or SPD)\n"
"  -h, --help\n"
"          prints this message\n"
"\n"
"Examples:\n"
"\n"
"  Dump information:\n"
"          intel_infoframes\n"
"\n"
"  Disable overscan and set ITC on transcoder B:\n"
"          intel_infoframes -t B -f AVI -c 'S 2 ITC 1'\n"
"\n"
"  Many actions on the same command:\n"
"  - enable overscan on transcoder A\n"
"  - enable overscan and change description on transcoder B\n"
"  - disable all infoframes on transcoder C\n"
"  - dump the resulting state:\n"
"          intel_infoframes -t A -f AVI -c 'S 1' \\\n"
"                           -t B -f AVI -c 'S 2' \\\n"
"                                -f SPD -c 'description Linux' \\\n"
"                           -t C --disable-infoframes \\\n"
"                           -d\n"
"\n"
"  Even more:\n"
"  - print the help message\n"
"  - completely disable all infoframes on all transcoders\n"
"  - dump the state"
"  - enable sending infoframes on transcoder B, but disable all infoframes\n"
"  - enable AVI infoframes transcoder B, use underscan and declare ITC\n"
"  - also enable SPD infoframes on the same transcoder, change frequency to\n"
"    every vsync and change vendor, description and source\n"
"  - dump the state again\n"
"          intel_infoframes -h \\\n"
"                           -t A -x -t B -x -t C -x \\\n"
"                           -d \\\n"
"                           -t A -X -f AVI -n -f Vendor -n \\\n"
"                           -f Gamut -n -f SPD -n \\\n"
"                           -f AVI -N -c 'S 2 ITC 1'\\\n"
"                           -f SPD -q everyvsync \\\n"
"                           -c 'vendor me description mine source 0x09' \\\n"
"                           -d\n"
"\n"
"Infoframe fields used by the --change-fields option:\n"
"  - AVI infoframe fields:\n"
"          S B A Y R M C SC Q EC ITC VIC PR top bottom left right\n"
"  - SPD infoframe fields:\n"
"          vendor description source\n"
"  - Other infoframe fields are not implemented yet.\n");
}

#define CHECK_TRANSCODER(transcoder)                  \
	if (transcoder == TRANSC_INVALID) {           \
		printf("Transcoder not selected.\n"); \
		ret = 1;                              \
		goto out;                             \
	}

#define CHECK_DIP(dip)                                \
	if (dip == DIP_INVALID) {                     \
		printf("Infoframe not selected.\n");  \
		ret = 1;                              \
		goto out;                             \
	}

int main(int argc, char *argv[])
{
	int opt;
	int ret = 0;
	Transcoder transcoder = TRANSC_INVALID;
	DipType dip = DIP_INVALID;
	Register hdmi_port;

	char short_opts[] = "dc:k:q:nNxXp:P:t:f:h";
	struct option long_opts[] = {
		{ "dump",               no_argument,       NULL, 'd' },
		{ "change-fields",      required_argument, NULL, 'c' },
		{ "change-checksum",    required_argument, NULL, 'k' },
		{ "change-frequency",   required_argument, NULL, 'q' },
		{ "disable",            no_argument,       NULL, 'n' },
		{ "enable",             no_argument,       NULL, 'N' },
		{ "disable-infoframes", no_argument,       NULL, 'x' },
		{ "enable-infoframes",  no_argument,       NULL, 'X' },
		{ "disable-hdmi-port",  required_argument, NULL, 'p' },
		{ "enable-hdmi-port",   required_argument, NULL, 'P' },
		{ "transcoder" ,        required_argument, NULL, 't' },
		{ "infoframe",          required_argument, NULL, 'f' },
		{ "help",               no_argument,       NULL, 'h' },
		{ 0 }
	};

	printf("WARNING: This is just a debugging tool! Don't expect it to work"
	       " perfectly: the Kernel might undo our changes.\n");

	pci_dev = intel_get_pci_device();
	intel_register_access_init(pci_dev, 0);
	intel_check_pch();

	if (IS_GEN4(pci_dev->device_id))
		gen = 4;
	else if (IS_GEN5(pci_dev->device_id))
		gen = 5;
	else if (IS_GEN6(pci_dev->device_id))
		gen = 6;
	else if (IS_GEN7(pci_dev->device_id))
		gen = 7;
	else {
		printf("This program does not support your hardware yet.\n");
		ret = 1;
		goto out;
	}

	while (1) {
		opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'd':
			dump_all_info();
			break;
		case 'c':
			if (transcoder == TRANSC_INVALID) {
				printf("Transcoder not selected.\n");
				ret = 1;
				goto out;
			}
			switch (dip) {
			case DIP_AVI:
				change_avi_infoframe(transcoder, optarg);
				break;
			case DIP_VENDOR:
			case DIP_GAMUT:
				printf("Option not implemented yet.\n");
				ret = 1;
				goto out;
			case DIP_SPD:
				change_spd_infoframe(transcoder, optarg);
				break;
			case DIP_INVALID:
				printf("Infoframe not selected.\n");
				ret = 1;
				goto out;
			}
			break;
		case 'k':
			CHECK_TRANSCODER(transcoder);
			CHECK_DIP(dip);
			change_infoframe_checksum(transcoder, dip, atoi(optarg));
			break;
		case 'q':
			CHECK_TRANSCODER(transcoder);
			CHECK_DIP(dip);
			if (!strcmp(optarg, "once"))
				change_infoframe_frequency(transcoder, dip,
						DIP_FREQ_ONCE);
			else if (!strcmp(optarg, "everyvsync"))
				change_infoframe_frequency(transcoder, dip,
						DIP_FREQ_EVERY_VSYNC);
			else if (!strcmp(optarg, "everyothervsync"))
				change_infoframe_frequency(transcoder, dip,
						DIP_FREQ_EVERY_OTHER_VSYNC);
			else {
				printf("Invalid frequency.\n");
				ret = 1;
				goto out;
			}
			break;
		case 'n':
			CHECK_TRANSCODER(transcoder);
			CHECK_DIP(dip);
			disable_infoframe(transcoder, dip);
			break;
		case 'N':
			CHECK_TRANSCODER(transcoder);
			CHECK_DIP(dip);
			enable_infoframe(transcoder, dip);
			break;
		case 'x':
			CHECK_TRANSCODER(transcoder);
			disable_dip(transcoder);
			break;
		case 'X':
			CHECK_TRANSCODER(transcoder);
			enable_dip(transcoder);
			break;
		case 'p':
		case 'P':
			if (!strcmp(optarg, "B"))
				hdmi_port = get_hdmi_port(0);
			else if (!strcmp(optarg, "C"))
				hdmi_port = get_hdmi_port(1);
			else if (!strcmp(optarg, "D"))
				hdmi_port = get_hdmi_port(2);
			else {
				printf("Invalid HDMI port.\n");
				ret = 1;
				goto out;
			}
			if (opt == 'p')
				disable_hdmi_port(hdmi_port);
			else
				enable_hdmi_port(hdmi_port);
			break;
		case 't':
			if (!strcmp(optarg, "A"))
				transcoder = TRANSC_A;
			else if (!strcmp(optarg, "B"))
				transcoder = TRANSC_B;
			else if (intel_pch >= PCH_CPT && !strcmp(optarg, "C")) {
				transcoder = TRANSC_C;
			} else {
				printf("Invalid transcoder.\n");
				ret = 1;
				goto out;
			}
			break;
		case 'f':
			if (!strcmp(optarg, "AVI"))
				dip = DIP_AVI;
			else if (!strcmp(optarg, "Vendor"))
				dip = DIP_VENDOR;
			else if (!strcmp(optarg, "Gamut"))
				dip = DIP_GAMUT;
			else if (!strcmp(optarg, "SPD"))
				dip = DIP_SPD;
			else {
				printf("Invalid infoframe.\n");
				ret = 1;
				goto out;
			}
			break;
		case 'h':
			print_usage();
			break;
		default:
			print_usage();
			ret = 1;
			goto out;
		}
	}

out:
	intel_register_access_fini();
	return ret;
}
