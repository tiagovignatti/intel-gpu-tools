/*
 * Copyright © 2006 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "intel_bios.h"
#include "intel_gpu_tools.h"

static uint32_t devid = -1;

/* no bother to include "edid.h" */
#define _H_ACTIVE(x) (x[2] + ((x[4] & 0xF0) << 4))
#define _H_SYNC_OFF(x) (x[8] + ((x[11] & 0xC0) << 2))
#define _H_SYNC_WIDTH(x) (x[9] + ((x[11] & 0x30) << 4))
#define _H_BLANK(x) (x[3] + ((x[4] & 0x0F) << 8))
#define _V_ACTIVE(x) (x[5] + ((x[7] & 0xF0) << 4))
#define _V_SYNC_OFF(x) ((x[10] >> 4) + ((x[11] & 0x0C) << 2))
#define _V_SYNC_WIDTH(x) ((x[10] & 0x0F) + ((x[11] & 0x03) << 4))
#define _V_BLANK(x) (x[6] + ((x[7] & 0x0F) << 8))
#define _PIXEL_CLOCK(x) (x[0] + (x[1] << 8)) * 10000

uint8_t *VBIOS;

#define INTEL_BIOS_8(_addr)	(VBIOS[_addr])
#define INTEL_BIOS_16(_addr)	(VBIOS[_addr] | \
				 (VBIOS[_addr + 1] << 8))
#define INTEL_BIOS_32(_addr)	(VBIOS[_addr] | \
				 (VBIOS[_addr + 1] << 8) | \
				 (VBIOS[_addr + 2] << 16) | \
				 (VBIOS[_addr + 3] << 24))

#define YESNO(val) ((val) ? "yes" : "no")

struct bdb_block {
	uint8_t id;
	uint16_t size;
	void *data;
};

struct bdb_header *bdb;
static int tv_present;
static int lvds_present;
static int panel_type;

static struct bdb_block *find_section(int section_id, int length)
{
	struct bdb_block *block;
	unsigned char *base = (unsigned char *)bdb;
	int idx = 0;
	uint16_t total, current_size;
	unsigned char current_id;

	/* skip to first section */
	idx += bdb->header_size;
	total = bdb->bdb_size;
	if (total > length)
		total = length;

	block = malloc(sizeof(*block));
	if (!block) {
		fprintf(stderr, "out of memory\n");
		exit(-1);
	}

	/* walk the sections looking for section_id */
	while (idx + 3 < total) {
		current_id = *(base + idx);
		current_size = *(uint16_t *)(base + idx + 1);
		if (idx + current_size > total)
			return NULL;

		if (current_id == section_id) {
			block->id = current_id;
			block->size = current_size;
			block->data = base + idx + 3;
			return block;
		}

		idx += current_size + 3;
	}

	free(block);
	return NULL;
}

static void dump_general_features(int length)
{
	struct bdb_general_features *features;
	struct bdb_block *block;

	block = find_section(BDB_GENERAL_FEATURES, length);

	if (!block)
		return;

	features = block->data;

	printf("General features block:\n");

	printf("\tPanel fitting: ");
	switch (features->panel_fitting) {
	case 0:
		printf("disabled\n");
		break;
	case 1:
		printf("text only\n");
		break;
	case 2:
		printf("graphics only\n");
		break;
	case 3:
		printf("text & graphics\n");
		break;
	}
	printf("\tFlexaim: %s\n", YESNO(features->flexaim));
	printf("\tMessage: %s\n", YESNO(features->msg_enable));
	printf("\tClear screen: %d\n", features->clear_screen);
	printf("\tDVO color flip required: %s\n", YESNO(features->color_flip));
	printf("\tExternal VBT: %s\n", YESNO(features->download_ext_vbt));
	printf("\tEnable SSC: %s\n", YESNO(features->enable_ssc));
	if (features->enable_ssc) {
		if (HAS_PCH_SPLIT(devid))
			printf("\tSSC frequency: %s\n", features->ssc_freq ?
			       "100 MHz" : "120 MHz");
		else
			printf("\tSSC frequency: %s\n", features->ssc_freq ?
			       "100 MHz (66 MHz on 855)" : "96 MHz (48 MHz on 855)");
	}
	printf("\tLFP on override: %s\n",
	       YESNO(features->enable_lfp_on_override));
	printf("\tDisable SSC on clone: %s\n",
	       YESNO(features->disable_ssc_ddt));
	printf("\tDisable smooth vision: %s\n",
	       YESNO(features->disable_smooth_vision));
	printf("\tSingle DVI for CRT/DVI: %s\n", YESNO(features->single_dvi));
	printf("\tLegacy monitor detect: %s\n",
	       YESNO(features->legacy_monitor_detect));
	printf("\tIntegrated CRT: %s\n", YESNO(features->int_crt_support));
	printf("\tIntegrated TV: %s\n", YESNO(features->int_tv_support));

	tv_present = 1;		/* should be based on whether TV DAC exists */
	lvds_present = 1;	/* should be based on IS_MOBILE() */

	free(block);
}

static void dump_backlight_info(int length)
{
	struct bdb_block *block;
	struct bdb_lvds_backlight *backlight;
	struct blc_struct *blc;

	block = find_section(BDB_LVDS_BACKLIGHT, length);

	if (!block)
		return;

	backlight = block->data;

	printf("Backlight info block (len %d):\n", block->size);

	if (sizeof(struct blc_struct) != backlight->blcstruct_size) {
		printf("\tBacklight struct sizes don't match (expected %zu, got %u), skipping\n",
		     sizeof(struct blc_struct), backlight->blcstruct_size);
		return;
	}

	blc = &backlight->panels[panel_type];

	printf("\tInverter type: %d\n", blc->inverter_type);
	printf("\t     polarity: %d\n", blc->inverter_polarity);
	printf("\t    GPIO pins: %d\n", blc->gpio_pins);
	printf("\t  GMBUS speed: %d\n", blc->gmbus_speed);
	printf("\t     PWM freq: %d\n", blc->pwm_freq);
	printf("\tMinimum brightness: %d\n", blc->min_brightness);
	printf("\tI2C slave addr: 0x%02x\n", blc->i2c_slave_addr);
	printf("\tI2C command: 0x%02x\n", blc->i2c_cmd);
}

static const struct {
	unsigned short type;
	const char *name;
} child_device_types[] = {
	{ DEVICE_TYPE_NONE, "none" },
	{ DEVICE_TYPE_CRT, "CRT" },
	{ DEVICE_TYPE_TV, "TV" },
	{ DEVICE_TYPE_EFP, "EFP" },
	{ DEVICE_TYPE_LFP, "LFP" },
	{ DEVICE_TYPE_CRT_DPMS, "CRT" },
	{ DEVICE_TYPE_CRT_DPMS_HOTPLUG, "CRT" },
	{ DEVICE_TYPE_TV_COMPOSITE, "TV composite" },
	{ DEVICE_TYPE_TV_MACROVISION, "TV" },
	{ DEVICE_TYPE_TV_RF_COMPOSITE, "TV" },
	{ DEVICE_TYPE_TV_SVIDEO_COMPOSITE, "TV S-Video" },
	{ DEVICE_TYPE_TV_SCART, "TV SCART" },
	{ DEVICE_TYPE_TV_CODEC_HOTPLUG_PWR, "TV" },
	{ DEVICE_TYPE_EFP_HOTPLUG_PWR, "EFP" },
	{ DEVICE_TYPE_EFP_DVI_HOTPLUG_PWR, "DVI" },
	{ DEVICE_TYPE_EFP_DVI_I, "DVI-I" },
	{ DEVICE_TYPE_EFP_DVI_D_DUAL, "DL-DVI-D" },
	{ DEVICE_TYPE_EFP_DVI_D_HDCP, "DVI-D" },
	{ DEVICE_TYPE_OPENLDI_HOTPLUG_PWR, "OpenLDI" },
	{ DEVICE_TYPE_OPENLDI_DUALPIX, "OpenLDI" },
	{ DEVICE_TYPE_LFP_PANELLINK, "PanelLink" },
	{ DEVICE_TYPE_LFP_CMOS_PWR, "CMOS LFP" },
	{ DEVICE_TYPE_LFP_LVDS_PWR, "LVDS" },
	{ DEVICE_TYPE_LFP_LVDS_DUAL, "LVDS" },
	{ DEVICE_TYPE_LFP_LVDS_DUAL_HDCP, "LVDS" },
	{ DEVICE_TYPE_INT_LFP, "LFP" },
	{ DEVICE_TYPE_INT_TV, "TV" },
	{ DEVICE_TYPE_DP, "DisplayPort" },
	{ DEVICE_TYPE_DP_HDMI_DVI, "DisplayPort/HDMI/DVI" },
	{ DEVICE_TYPE_DP_DVI, "DisplayPort/DVI" },
	{ DEVICE_TYPE_HDMI_DVI, "HDMI/DVI" },
	{ DEVICE_TYPE_DVI, "DVI" },
	{ DEVICE_TYPE_eDP, "eDP" },
};
static const int num_child_device_types =
	sizeof(child_device_types) / sizeof(child_device_types[0]);

static const char *child_device_type(unsigned short type)
{
	int i;

	for (i = 0; i < num_child_device_types; i++)
		if (child_device_types[i].type == type)
			return child_device_types[i].name;

	return "unknown";
}

static const struct {
	unsigned short type;
	const char *name;
} efp_ports[] = {
	{ DEVICE_PORT_NONE, "N/A" },
	{ DEVICE_PORT_HDMIB, "HDMI-B" },
	{ DEVICE_PORT_HDMIC, "HDMI-C" },
	{ DEVICE_PORT_HDMID, "HDMI-D" },
	{ DEVICE_PORT_DPB, "DP-B" },
	{ DEVICE_PORT_DPC, "DP-C" },
	{ DEVICE_PORT_DPD, "DP-D" },
};
static const int num_efp_ports = sizeof(efp_ports) / sizeof(efp_ports[0]);

static const char *efp_port(uint8_t type)
{
	int i;

	for (i = 0; i < num_efp_ports; i++)
		if (efp_ports[i].type == type)
			return efp_ports[i].name;

	return "unknown";
}

static const struct {
	unsigned short type;
	const char *name;
} efp_conn_info[] = {
	{ DEVICE_INFO_NONE, "N/A" },
	{ DEVICE_INFO_HDMI_CERT, "HDMI certified" },
	{ DEVICE_INFO_DP, "DisplayPort" },
	{ DEVICE_INFO_DVI, "DVI" },
};
static const int num_efp_conn_info = sizeof(efp_conn_info) / sizeof(efp_conn_info[0]);

static const char *efp_conn(uint8_t type)
{
	int i;

	for (i = 0; i < num_efp_conn_info; i++)
		if (efp_conn_info[i].type == type)
			return efp_conn_info[i].name;

	return "unknown";
}



static void dump_child_device(struct child_device_config *child)
{
	char child_id[11];

	if (!child->device_type)
		return;

	if (bdb->version < 152) {
		strncpy(child_id, (char *)child->device_id, 10);
		child_id[10] = 0;

		printf("\tChild device info:\n");
		printf("\t\tDevice type: %04x (%s)\n", child->device_type,
		       child_device_type(child->device_type));
		printf("\t\tSignature: %s\n", child_id);
		printf("\t\tAIM offset: %d\n", child->addin_offset);
		printf("\t\tDVO port: 0x%02x\n", child->dvo_port);
	} else { /* 152+ have EFP blocks here */
		struct efp_child_device_config *efp =
			(struct efp_child_device_config *)child;
		printf("\tEFP device info:\n");
		printf("\t\tDevice type: 0x%04x (%s)\n", efp->device_type,
		       child_device_type(efp->device_type));
		printf("\t\tPort: 0x%02x (%s)\n", efp->port,
		       efp_port(efp->port));
		printf("\t\tDDC pin: 0x%02x\n", efp->ddc_pin);
		printf("\t\tDock port: 0x%02x (%s)\n", efp->docked_port,
		       efp_port(efp->docked_port));
		printf("\t\tHDMI compatible? %s\n", efp->hdmi_compat ? "Yes" : "No");
		printf("\t\tInfo: %s\n", efp_conn(efp->conn_info));
		printf("\t\tAux channel: 0x%02x\n", efp->aux_chan);
		printf("\t\tDongle detect: 0x%02x\n", efp->dongle_detect);
	}
}

static void dump_general_definitions(int length)
{
	struct bdb_block *block;
	struct bdb_general_definitions *defs;
	struct child_device_config *child;
	int i;
	int child_device_num;

	block = find_section(BDB_GENERAL_DEFINITIONS, length);

	if (!block)
		return;

	defs = block->data;

	printf("General definitions block:\n");

	printf("\tCRT DDC GMBUS addr: 0x%02x\n", defs->crt_ddc_gmbus_pin);
	printf("\tUse ACPI DPMS CRT power states: %s\n",
	       YESNO(defs->dpms_acpi));
	printf("\tSkip CRT detect at boot: %s\n",
	       YESNO(defs->skip_boot_crt_detect));
	printf("\tUse DPMS on AIM devices: %s\n", YESNO(defs->dpms_aim));
	printf("\tBoot display type: 0x%02x%02x\n", defs->boot_display[1],
	       defs->boot_display[0]);
	printf("\tTV data block present: %s\n", YESNO(tv_present));
	child_device_num = (block->size - sizeof(*defs)) / sizeof(*child);
	for (i = 0; i < child_device_num; i++)
		dump_child_device(&defs->devices[i]);
	free(block);
}

static void dump_child_devices(int length)
{
	struct bdb_block *block;
	struct bdb_child_devices *child_devs;
	struct child_device_config *child;
	int i;

	block = find_section(BDB_CHILD_DEVICE_TABLE, length);
	if (!block) {
		printf("No child device table found\n");
		return;
	}

	child_devs = block->data;

	printf("Child devices block:\n");
	for (i = 0; i < DEVICE_CHILD_SIZE; i++) {
		child = &child_devs->children[i];
		/* Skip nonexistent children */
		if (!child->device_type)
			continue;
		printf("\tChild device %d\n", i);
		printf("\t\tType: 0x%04x (%s)\n", child->device_type,
		       child_device_type(child->device_type));
		printf("\t\tDVO port: 0x%02x\n", child->dvo_port);
		printf("\t\tI2C pin: 0x%02x\n", child->i2c_pin);
		printf("\t\tSlave addr: 0x%02x\n", child->slave_addr);
		printf("\t\tDDC pin: 0x%02x\n", child->ddc_pin);
		printf("\t\tDVO config: 0x%02x\n", child->dvo_cfg);
		printf("\t\tDVO wiring: 0x%02x\n", child->dvo_wiring);
	}

	free(block);
}

static void dump_lvds_options(int length)
{
	struct bdb_block *block;
	struct bdb_lvds_options *options;

	block = find_section(BDB_LVDS_OPTIONS, length);
	if (!block) {
		printf("No LVDS options block\n");
		return;
	}

	options = block->data;

	printf("LVDS options block:\n");

	panel_type = options->panel_type;
	printf("\tPanel type: %d\n", panel_type);
	printf("\tLVDS EDID available: %s\n", YESNO(options->lvds_edid));
	printf("\tPixel dither: %s\n", YESNO(options->pixel_dither));
	printf("\tPFIT auto ratio: %s\n", YESNO(options->pfit_ratio_auto));
	printf("\tPFIT enhanced graphics mode: %s\n",
	       YESNO(options->pfit_gfx_mode_enhanced));
	printf("\tPFIT enhanced text mode: %s\n",
	       YESNO(options->pfit_text_mode_enhanced));
	printf("\tPFIT mode: %d\n", options->pfit_mode);

	free(block);
}

static void dump_lvds_ptr_data(int length)
{
	struct bdb_block *block;
	struct bdb_lvds_lfp_data *lvds_data;
	struct bdb_lvds_lfp_data_ptrs *ptrs;
	struct lvds_fp_timing *fp_timing;
	struct bdb_lvds_lfp_data_entry *entry;
	int lfp_data_size;

	block = find_section(BDB_LVDS_LFP_DATA_PTRS, length);
	if (!block) {
		printf("No LFP data pointers block\n");
		return;
	}
	ptrs = block->data;

	block = find_section(BDB_LVDS_LFP_DATA, length);
	if (!block) {
		printf("No LVDS data block\n");
		return;
	}
	lvds_data = block->data;

	lfp_data_size =
	    ptrs->ptr[1].fp_timing_offset - ptrs->ptr[0].fp_timing_offset;
	entry =
	    (struct bdb_lvds_lfp_data_entry *)((uint8_t *) lvds_data->data +
					       (lfp_data_size * panel_type));
	fp_timing = &entry->fp_timing;

	printf("LVDS timing pointer data:\n");
	printf("  Number of entries: %d\n", ptrs->lvds_entries);

	printf("\tpanel type %02i: %dx%d\n", panel_type, fp_timing->x_res,
	       fp_timing->y_res);

	free(block);
}

static void dump_lvds_data(int length)
{
	struct bdb_block *block;
	struct bdb_lvds_lfp_data *lvds_data;
	struct bdb_lvds_lfp_data_ptrs *ptrs;
	int num_entries;
	int i;
	int hdisplay, hsyncstart, hsyncend, htotal;
	int vdisplay, vsyncstart, vsyncend, vtotal;
	float clock;
	int lfp_data_size, dvo_offset;

	block = find_section(BDB_LVDS_LFP_DATA_PTRS, length);
	if (!block) {
		printf("No LVDS ptr block\n");
		return;
	}
	ptrs = block->data;
	lfp_data_size =
	    ptrs->ptr[1].fp_timing_offset - ptrs->ptr[0].fp_timing_offset;
	dvo_offset =
	    ptrs->ptr[0].dvo_timing_offset - ptrs->ptr[0].fp_timing_offset;
	free(block);

	block = find_section(BDB_LVDS_LFP_DATA, length);
	if (!block) {
		printf("No LVDS data block\n");
		return;
	}

	lvds_data = block->data;
	num_entries = block->size / lfp_data_size;

	printf("LVDS panel data block (preferred block marked with '*'):\n");
	printf("  Number of entries: %d\n", num_entries);

	for (i = 0; i < num_entries; i++) {
		uint8_t *lfp_data_ptr =
		    (uint8_t *) lvds_data->data + lfp_data_size * i;
		uint8_t *timing_data = lfp_data_ptr + dvo_offset;
		struct bdb_lvds_lfp_data_entry *lfp_data =
		    (struct bdb_lvds_lfp_data_entry *)lfp_data_ptr;
		char marker;

		if (i == panel_type)
			marker = '*';
		else
			marker = ' ';

		hdisplay = _H_ACTIVE(timing_data);
		hsyncstart = hdisplay + _H_SYNC_OFF(timing_data);
		hsyncend = hsyncstart + _H_SYNC_WIDTH(timing_data);
		htotal = hdisplay + _H_BLANK(timing_data);

		vdisplay = _V_ACTIVE(timing_data);
		vsyncstart = vdisplay + _V_SYNC_OFF(timing_data);
		vsyncend = vsyncstart + _V_SYNC_WIDTH(timing_data);
		vtotal = vdisplay + _V_BLANK(timing_data);
		clock = _PIXEL_CLOCK(timing_data) / 1000;

		printf("%c\tpanel type %02i: %dx%d clock %d\n", marker,
		       i, lfp_data->fp_timing.x_res, lfp_data->fp_timing.y_res,
		       _PIXEL_CLOCK(timing_data));
		printf("\t\tinfo:\n");
		printf("\t\t  LVDS: 0x%08lx\n",
		       (unsigned long)lfp_data->fp_timing.lvds_reg_val);
		printf("\t\t  PP_ON_DELAYS: 0x%08lx\n",
		       (unsigned long)lfp_data->fp_timing.pp_on_reg_val);
		printf("\t\t  PP_OFF_DELAYS: 0x%08lx\n",
		       (unsigned long)lfp_data->fp_timing.pp_off_reg_val);
		printf("\t\t  PP_DIVISOR: 0x%08lx\n",
		       (unsigned long)lfp_data->fp_timing.pp_cycle_reg_val);
		printf("\t\t  PFIT: 0x%08lx\n",
		       (unsigned long)lfp_data->fp_timing.pfit_reg_val);
		printf("\t\ttimings: %d %d %d %d %d %d %d %d %.2f (%s)\n",
		       hdisplay, hsyncstart, hsyncend, htotal,
		       vdisplay, vsyncstart, vsyncend, vtotal, clock,
		       (hsyncend > htotal || vsyncend > vtotal) ?
		       "BAD!" : "good");
	}
	free(block);
}

static void dump_driver_feature(int length)
{
	struct bdb_block *block;
	struct bdb_driver_feature *feature;

	block = find_section(BDB_DRIVER_FEATURES, length);
	if (!block) {
		printf("No Driver feature data block\n");
		return;
	}
	feature = block->data;

	printf("Driver feature Data Block:\n");
	printf("\tBoot Device Algorithm: %s\n", feature->boot_dev_algorithm ?
	       "driver default" : "os default");
	printf("\tBlock display switching when DVD active: %s\n",
	       YESNO(feature->block_display_switch));
	printf("\tAllow display switching when in Full Screen DOS: %s\n",
	       YESNO(feature->allow_display_switch));
	printf("\tHot Plug DVO: %s\n", YESNO(feature->hotplug_dvo));
	printf("\tDual View Zoom: %s\n", YESNO(feature->dual_view_zoom));
	printf("\tDriver INT 15h hook: %s\n", YESNO(feature->int15h_hook));
	printf("\tEnable Sprite in Clone Mode: %s\n",
	       YESNO(feature->sprite_in_clone));
	printf("\tUse 00000110h ID for Primary LFP: %s\n",
	       YESNO(feature->primary_lfp_id));
	printf("\tBoot Mode X: %u\n", feature->boot_mode_x);
	printf("\tBoot Mode Y: %u\n", feature->boot_mode_y);
	printf("\tBoot Mode Bpp: %u\n", feature->boot_mode_bpp);
	printf("\tBoot Mode Refresh: %u\n", feature->boot_mode_refresh);
	printf("\tEnable LFP as primary: %s\n",
	       YESNO(feature->enable_lfp_primary));
	printf("\tSelective Mode Pruning: %s\n",
	       YESNO(feature->selective_mode_pruning));
	printf("\tDual-Frequency Graphics Technology: %s\n",
	       YESNO(feature->dual_frequency));
	printf("\tDefault Render Clock Frequency: %s\n",
	       feature->render_clock_freq ? "low" : "high");
	printf("\tNT 4.0 Dual Display Clone Support: %s\n",
	       YESNO(feature->nt_clone_support));
	printf("\tDefault Power Scheme user interface: %s\n",
	       feature->power_scheme_ui ? "3rd party" : "CUI");
	printf
	    ("\tSprite Display Assignment when Overlay is Active in Clone Mode: %s\n",
	     feature->sprite_display_assign ? "primary" : "secondary");
	printf("\tDisplay Maintain Aspect Scaling via CUI: %s\n",
	       YESNO(feature->cui_aspect_scaling));
	printf("\tPreserve Aspect Ratio: %s\n",
	       YESNO(feature->preserve_aspect_ratio));
	printf("\tEnable SDVO device power down: %s\n",
	       YESNO(feature->sdvo_device_power_down));
	printf("\tCRT hotplug: %s\n", YESNO(feature->crt_hotplug));
	printf("\tLVDS config: ");
	switch (feature->lvds_config) {
	case BDB_DRIVER_NO_LVDS:
		printf("No LVDS\n");
		break;
	case BDB_DRIVER_INT_LVDS:
		printf("Integrated LVDS\n");
		break;
	case BDB_DRIVER_SDVO_LVDS:
		printf("SDVO LVDS\n");
		break;
	case BDB_DRIVER_EDP:
		printf("Embedded DisplayPort\n");
		break;
	}
	printf("\tDefine Display statically: %s\n",
	       YESNO(feature->static_display));
	printf("\tLegacy CRT max X: %d\n", feature->legacy_crt_max_x);
	printf("\tLegacy CRT max Y: %d\n", feature->legacy_crt_max_y);
	printf("\tLegacy CRT max refresh: %d\n",
	       feature->legacy_crt_max_refresh);
	free(block);
}

static void dump_edp(int length)
{
	struct bdb_block *block;
	struct bdb_edp *edp;
	int bpp;

	block = find_section(BDB_EDP, length);
	if (!block) {
		printf("No EDP data block\n");
		return;
	}
	edp = block->data;

	printf("eDP block: type %d\n", panel_type);
	printf("\tPower Sequence: T3 %d T7 %d T9 %d T10 %d T12 %d\n",
		edp->power_seqs[panel_type].t3,
		edp->power_seqs[panel_type].t7,
		edp->power_seqs[panel_type].t9,
		edp->power_seqs[panel_type].t10,
		edp->power_seqs[panel_type].t12);

	bpp = (edp->color_depth >> (panel_type * 2)) & 3;

	printf("\tPanel color depth: ");
	switch (bpp) {
	case EDP_18BPP:
		printf("18bpp\n");
		break;
	case EDP_24BPP:
		printf("24bpp\n");
		break;
	case EDP_30BPP:
		printf("30bpp\n");
		break;
	}
	printf("\teDP sDRRs MSA timing delay: %d\n", edp->sdrrs_msa_timing_delay);
	printf("\tLink params:\n");
	printf("\t\trate: ");
	if (edp->link_params[panel_type].rate == EDP_RATE_1_62)
		printf("1.62G\n");
	else if (edp->link_params[panel_type].rate == EDP_RATE_2_7)
		printf("2.7G\n");
	printf("\t\tlanes: ");
	switch (edp->link_params[panel_type].lanes) {
	case EDP_LANE_1:
		printf("x1 mode\n");
		break;
	case EDP_LANE_2:
		printf("x2 mode\n");
		break;
	case EDP_LANE_4:
		printf("x4 mode\n");
		break;
	}
	printf("\t\tpre-emphasis: ");
	switch (edp->link_params[panel_type].preemphasis) {
	case EDP_PREEMPHASIS_NONE:
		printf("none\n");
		break;
	case EDP_PREEMPHASIS_3_5dB:
		printf("3.5dB\n");
		break;
	case EDP_PREEMPHASIS_6dB:
		printf("6dB\n");
		break;
	case EDP_PREEMPHASIS_9_5dB:
		printf("9.5dB\n");
		break;
	}
	printf("\t\tvswing: ");
	switch (edp->link_params[panel_type].vswing) {
	case EDP_VSWING_0_4V:
		printf("0.4V\n");
		break;
	case EDP_VSWING_0_6V:
		printf("0.6V\n");
		break;
	case EDP_VSWING_0_8V:
		printf("0.8V\n");
		break;
	case EDP_VSWING_1_2V:
		printf("1.2V\n");
		break;
	}
	free(block);
}

static void
print_detail_timing_data(struct lvds_dvo_timing2 *dvo_timing)
{
	int display, sync_start, sync_end, total;

	display = (dvo_timing->hactive_hi << 8) | dvo_timing->hactive_lo;
	sync_start = display +
		((dvo_timing->hsync_off_hi << 8) | dvo_timing->hsync_off_lo);
	sync_end = sync_start + dvo_timing->hsync_pulse_width;
	total = display +
		((dvo_timing->hblank_hi << 8) | dvo_timing->hblank_lo);
	printf("\thdisplay: %d\n", display);
	printf("\thsync [%d, %d] %s\n", sync_start, sync_end,
	       dvo_timing->hsync_positive ? "+sync" : "-sync");
	printf("\thtotal: %d\n", total);

	display = (dvo_timing->vactive_hi << 8) | dvo_timing->vactive_lo;
	sync_start = display + dvo_timing->vsync_off;
	sync_end = sync_start + dvo_timing->vsync_pulse_width;
	total = display +
		((dvo_timing->vblank_hi << 8) | dvo_timing->vblank_lo);
	printf("\tvdisplay: %d\n", display);
	printf("\tvsync [%d, %d] %s\n", sync_start, sync_end,
	       dvo_timing->vsync_positive ? "+sync" : "-sync");
	printf("\tvtotal: %d\n", total);

	printf("\tclock: %d\n", dvo_timing->clock * 10);
}

static void dump_sdvo_panel_dtds(int length)
{
	struct bdb_block *block;
	struct lvds_dvo_timing2 *dvo_timing;
	int n, count;

	block = find_section(BDB_SDVO_PANEL_DTDS, length);
	if (!block) {
		printf("No SDVO panel dtds block\n");
		return;
	}

	printf("SDVO panel dtds:\n");
	count = block->size / sizeof(struct lvds_dvo_timing2);
	dvo_timing = block->data;
	for (n = 0; n < count; n++) {
		printf("%d:\n", n);
		print_detail_timing_data(dvo_timing++);
	}

	free(block);
}

static void dump_sdvo_lvds_options(int length)
{
	struct bdb_block *block;
	struct bdb_sdvo_lvds_options *options;

	block = find_section(BDB_SDVO_LVDS_OPTIONS, length);
	if (!block) {
		printf("No SDVO LVDS options block\n");
		return;
	}

	options = block->data;

	printf("SDVO LVDS options block:\n");
	printf("\tbacklight: %d\n", options->panel_backlight);
	printf("\th40 type: %d\n", options->h40_set_panel_type);
	printf("\ttype: %d\n", options->panel_type);
	printf("\tssc_clk_freq: %d\n", options->ssc_clk_freq);
	printf("\tals_low_trip: %d\n", options->als_low_trip);
	printf("\tals_high_trip: %d\n", options->als_high_trip);
	/*
	u8 sclalarcoeff_tab_row_num;
	u8 sclalarcoeff_tab_row_size;
	u8 coefficient[8];
	*/
	printf("\tmisc[0]: %x\n", options->panel_misc_bits_1);
	printf("\tmisc[1]: %x\n", options->panel_misc_bits_2);
	printf("\tmisc[2]: %x\n", options->panel_misc_bits_3);
	printf("\tmisc[3]: %x\n", options->panel_misc_bits_4);

	free(block);
}

static int
get_device_id(unsigned char *bios)
{
    int device;
    int offset = (bios[0x19] << 8) + bios[0x18];

    if (bios[offset] != 'P' ||
	bios[offset+1] != 'C' ||
	bios[offset+2] != 'I' ||
	bios[offset+3] != 'R')
	return -1;

    device = (bios[offset+7] << 8) + bios[offset+6];

    return device;
}

int main(int argc, char **argv)
{
	int fd;
	struct vbt_header *vbt = NULL;
	int vbt_off, bdb_off, i;
	const char *filename = "bios";
	struct stat finfo;
	struct bdb_block *block;
	char signature[17];
	char *devid_string;

	if (argc != 2) {
		printf("usage: %s <rom file>\n", argv[0]);
		return 1;
	}

	if ((devid_string = getenv("DEVICE")))
	    devid = strtoul(devid_string, NULL, 0);

	filename = argv[1];

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		printf("Couldn't open \"%s\": %s\n", filename, strerror(errno));
		return 1;
	}

	if (stat(filename, &finfo)) {
		printf("failed to stat \"%s\": %s\n", filename,
		       strerror(errno));
		return 1;
	}

	if (finfo.st_size == 0) {
		int len = 0, ret;
		finfo.st_size = 8192;
		VBIOS = malloc (finfo.st_size);
		while ((ret = read(fd, VBIOS + len, finfo.st_size - len))) {
			if (ret < 0) {
				printf("failed to read \"%s\": %s\n", filename,
				       strerror(errno));
				return 1;
			}

			len += ret;
			if (len == finfo.st_size) {
				finfo.st_size *= 2;
				VBIOS = realloc(VBIOS, finfo.st_size);
			}
		}
	} else {
		VBIOS = mmap(NULL, finfo.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (VBIOS == MAP_FAILED) {
			printf("failed to map \"%s\": %s\n", filename, strerror(errno));
			return 1;
		}
	}

	/* Scour memory looking for the VBT signature */
	for (i = 0; i + 4 < finfo.st_size; i++) {
		if (!memcmp(VBIOS + i, "$VBT", 4)) {
			vbt_off = i;
			vbt = (struct vbt_header *)(VBIOS + i);
			break;
		}
	}

	if (!vbt) {
		printf("VBT signature missing\n");
		return 1;
	}

	printf("VBT vers: %d.%d\n", vbt->version / 100, vbt->version % 100);

	bdb_off = vbt_off + vbt->bdb_offset;
	if (bdb_off >= finfo.st_size - sizeof(struct bdb_header)) {
		printf("Invalid VBT found, BDB points beyond end of data block\n");
		return 1;
	}

	bdb = (struct bdb_header *)(VBIOS + bdb_off);
	strncpy(signature, (char *)bdb->signature, 16);
	signature[16] = 0;
	printf("BDB sig: %s\n", signature);
	printf("BDB vers: %d\n", bdb->version);

	printf("Available sections: ");
	for (i = 0; i < 256; i++) {
		block = find_section(i, finfo.st_size);
		if (!block)
			continue;
		printf("%d ", i);
		free(block);
	}
	printf("\n");

	if (devid == -1)
	    devid = get_device_id(VBIOS);
	if (devid == -1)
	    printf("Warning: could not find PCI device ID!\n");

	dump_general_features(finfo.st_size);
	dump_general_definitions(finfo.st_size);
	dump_child_devices(finfo.st_size);
	dump_lvds_options(finfo.st_size);
	dump_lvds_data(finfo.st_size);
	dump_lvds_ptr_data(finfo.st_size);
	dump_backlight_info(finfo.st_size);

	dump_sdvo_lvds_options(finfo.st_size);
	dump_sdvo_panel_dtds(finfo.st_size);

	dump_driver_feature(finfo.st_size);
	dump_edp(finfo.st_size);

	return 0;
}
