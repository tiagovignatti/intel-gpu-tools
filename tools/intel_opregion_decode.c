/*
 * Copyright © 2013 Intel Corporation
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
 *    Jani Nikula <jani.nikula@intel.com>
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "intel_io.h"
#include "drmtest.h"

#define OPREGION_HEADER_OFFSET		0
#define OPREGION_ACPI_OFFSET		0x100
#define OPREGION_SWSCI_OFFSET		0x200
#define OPREGION_ASLE_OFFSET		0x300
#define OPREGION_VBT_OFFSET		0x400
#define OPREGION_ASLE_EXT_OFFSET	0x1C00

#define MBOX_ACPI	(1 << 0)
#define MBOX_SWSCI	(1 << 1)
#define MBOX_ASLE	(1 << 2)
#define MBOX_VBT	(1 << 3)
#define MBOX_ASLE_EXT	(1 << 4)

struct opregion_header {
	char sign[16];
	uint32_t size;
	uint32_t over;
	char sver[32];
	char vver[16];
	char gver[16];
	uint32_t mbox;
	uint32_t dmod;
	uint32_t pcon;
	char dver[32];
	uint8_t rsv1[124];
} __attribute__((packed));

/* OpRegion mailbox #1: public ACPI methods */
struct opregion_acpi {
	uint32_t drdy;		/* driver readiness */
	uint32_t csts;		/* notification status */
	uint32_t cevt;		/* current event */
	uint8_t rsvd1[20];
	uint32_t didl[8];	/* supported display devices ID list */
	uint32_t cpdl[8];	/* currently presented display list */
	uint32_t cadl[8];	/* currently active display list */
	uint32_t nadl[8];	/* next active devices list */
	uint32_t aslp;		/* ASL sleep time-out */
	uint32_t tidx;		/* toggle table index */
	uint32_t chpd;		/* current hotplug enable indicator */
	uint32_t clid;		/* current lid state*/
	uint32_t cdck;		/* current docking state */
	uint32_t sxsw;		/* Sx state resume */
	uint32_t evts;		/* ASL supported events */
	uint32_t cnot;		/* current OS notification */
	uint32_t nrdy;		/* driver status */
	uint32_t did2[7];
	uint32_t cpd2[7];
	uint8_t rsvd2[4];
} __attribute__((packed));

/* OpRegion mailbox #2: SWSCI */
struct opregion_swsci {
	uint32_t scic;		/* SWSCI command|status|data */
	uint32_t parm;		/* command parameters */
	uint32_t dslp;		/* driver sleep time-out */
	uint8_t rsvd[244];
} __attribute__((packed));

/* OpRegion mailbox #3: ASLE */
struct opregion_asle {
	uint32_t ardy;		/* driver readiness */
	uint32_t aslc;		/* ASLE interrupt command */
	uint32_t tche;		/* technology enabled indicator */
	uint32_t alsi;		/* current ALS illuminance reading */
	uint32_t bclp;		/* backlight brightness to set */
	uint32_t pfit;		/* panel fitting state */
	uint32_t cblv;		/* current brightness level */
	uint16_t bclm[20];	/* backlight level duty cycle mapping table */
	uint32_t cpfm;		/* current panel fitting mode */
	uint32_t epfm;		/* enabled panel fitting modes */
	uint8_t plut_header;    /* panel LUT and identifier */
	uint8_t plut_identifier[10];	/* panel LUT and identifier */
	uint8_t plut[63];	/* panel LUT and identifier */
	uint32_t pfmb;		/* PWM freq and min brightness */
	uint32_t ccdv;
	uint32_t pcft;
	uint32_t srot;
	uint32_t iuer;
	uint8_t fdss[8];
	uint32_t fdsp;
	uint32_t stat;
	uint8_t rsvd[86];
} __attribute__((packed));

/* OpRegion mailbox #4: VBT */
struct opregion_vbt {
	char product_string[20];
	/* rest ignored */
} __attribute__((packed));

/* OpRegion mailbox #5: ASLE extension */
struct opregion_asle_ext {
	uint32_t phed;
	uint8_t bddc[256];
} __attribute__((packed));

static uint32_t decode_header(const void *buffer)
{
	const struct opregion_header *header = buffer;
	char *s;

	if (strncmp("IntelGraphicsMem", header->sign, sizeof(header->sign))) {
		fprintf(stderr, "invalid opregion signature\n");
		return 0;
	}

	printf("OpRegion Header:\n");

	s = strndup(header->sign, sizeof(header->sign));
	printf("\tsign:\t%s\n", s);
	free(s);

	printf("\tsize:\t0x%08x\n", header->size);
	printf("\tover:\t0x%08x\n", header->over);

	s = strndup(header->sver, sizeof(header->sver));
	printf("\tsver:\t%s\n", s);
	free(s);

	s = strndup(header->vver, sizeof(header->vver));
	printf("\tvver:\t%s\n", s);
	free(s);

	s = strndup(header->gver, sizeof(header->gver));
	printf("\tgver:\t%s\n", s);
	free(s);

	printf("\tmbox:\t0x%08x\n", header->mbox);

	printf("\tdmod:\t0x%08x\n", header->dmod);
	printf("\tpcon:\t0x%08x\n", header->pcon);

	s = strndup(header->dver, sizeof(header->dver));
	printf("\tdver:\t%s\n", s);
	free(s);

	printf("\n");

	return header->mbox;
}

static void decode_acpi(const void *buffer)
{
	const struct opregion_acpi *acpi = buffer;
	int i;

	printf("OpRegion Mailbox 1: Public ACPI Methods:\n");

	printf("\tdrdy:\t0x%08x\n", acpi->drdy);
	printf("\tcsts:\t0x%08x\n", acpi->csts);
	printf("\tcevt:\t0x%08x\n", acpi->cevt);

	printf("\tdidl:\n");
	for (i = 0; i < ARRAY_SIZE(acpi->didl); i++)
		printf("\t\tdidl[%d]:\t0x%08x\n", i, acpi->didl[i]);

	printf("\tcpdl:\n");
	for (i = 0; i < ARRAY_SIZE(acpi->cpdl); i++)
		printf("\t\tcpdl[%d]:\t0x%08x\n", i, acpi->cpdl[i]);

	printf("\tcadl:\n");
	for (i = 0; i < ARRAY_SIZE(acpi->cadl); i++)
		printf("\t\tcadl[%d]:\t0x%08x\n", i, acpi->cadl[i]);

	printf("\tnadl:\n");
	for (i = 0; i < ARRAY_SIZE(acpi->nadl); i++)
		printf("\t\tnadl[%d]:\t0x%08x\n", i, acpi->nadl[i]);

	printf("\taslp:\t0x%08x\n", acpi->aslp);
	printf("\ttidx:\t0x%08x\n", acpi->tidx);
	printf("\tchpd:\t0x%08x\n", acpi->chpd);
	printf("\tclid:\t0x%08x\n", acpi->clid);
	printf("\tcdck:\t0x%08x\n", acpi->cdck);
	printf("\tsxsw:\t0x%08x\n", acpi->sxsw);
	printf("\tevts:\t0x%08x\n", acpi->evts);
	printf("\tcnot:\t0x%08x\n", acpi->cnot);
	printf("\tnrdy:\t0x%08x\n", acpi->nrdy);

	printf("\tdid2:\n");
	for (i = 0; i < ARRAY_SIZE(acpi->did2); i++)
		printf("\t\tdid2[%d]:\t0x%08x\n", i, acpi->did2[i]);

	printf("\tcpd2:\n");
	for (i = 0; i < ARRAY_SIZE(acpi->cpd2); i++)
		printf("\t\tcpd2[%d]:\t0x%08x\n", i, acpi->cpd2[i]);

	printf("\n");
}

static void decode_swsci(const void *buffer)
{
	const struct opregion_swsci *swsci = buffer;

	printf("OpRegion Mailbox 2: Software SCI Interface (SWSCI):\n");

	printf("\tscic:\t0x%08x\n", swsci->scic);
	printf("\tparm:\t0x%08x\n", swsci->parm);
	printf("\tdslp:\t0x%08x\n", swsci->dslp);

	printf("\n");
}

static void decode_asle(const void *buffer)
{
	const struct opregion_asle *asle = buffer;
	int i;

	printf("OpRegion Mailbox 3: BIOS to Driver Notification (ASLE):\n");

	printf("\tardy:\t0x%08x\n", asle->ardy);
	printf("\taslc:\t0x%08x\n", asle->aslc);
	printf("\ttche:\t0x%08x\n", asle->tche);
	printf("\talsi:\t0x%08x\n", asle->alsi);
	printf("\tbclp:\t0x%08x\n", asle->bclp);
	printf("\tpfit:\t0x%08x\n", asle->pfit);
	printf("\tcblv:\t0x%08x\n", asle->cblv);

	printf("\tbclm:\n");
	for (i = 0; i < ARRAY_SIZE(asle->bclm); i++) {
		int valid = asle->bclm[i] & (1 << 15);
		int percentage = (asle->bclm[i] & 0x7f00) >> 8;
		int duty_cycle = asle->bclm[i] & 0xff;

		printf("\t\tbclm[%d]:\t0x%04x", i, asle->bclm[i]);
		if (valid)
			printf(" (%3d%% -> 0x%02x)\n", percentage, duty_cycle);
		else
			printf("\n");

	}

	printf("\tcpfm:\t0x%08x\n", asle->cpfm);
	printf("\tepfm:\t0x%08x\n", asle->epfm);

	printf("\tplut header:\t0x%02x\n", asle->plut_header);

	printf("\tplut identifier:");
	for (i = 0; i < ARRAY_SIZE(asle->plut_identifier); i++)
		printf(" %02x", asle->plut_identifier[i]);
	printf("\n");

	printf("\tplut:\n");
	for (i = 0; i < ARRAY_SIZE(asle->plut); i++) {
		const int COLUMNS = 7;

		if (i % COLUMNS == 0)
			printf("\t\tplut[%d]:\t", i / COLUMNS);

		printf("%02x ", asle->plut[i]);

		if (i % COLUMNS == COLUMNS - 1)
			printf("\n");
	}

	printf("\tpfmb:\t0x%08x\n", asle->pfmb);
	printf("\tccdv:\t0x%08x\n", asle->ccdv);
	printf("\tpcft:\t0x%08x\n", asle->pcft);
	printf("\tsrot:\t0x%08x\n", asle->srot);
	printf("\tiuer:\t0x%08x\n", asle->iuer);

	printf("\tfdss:\t");
	for (i = 0; i < ARRAY_SIZE(asle->fdss); i++)
		printf("%02x ", asle->fdss[i]);
	printf("\n");

	printf("\tfdsp:\t0x%08x\n", asle->fdsp);
	printf("\tstat:\t0x%08x\n", asle->stat);

	printf("\n");
}

static void decode_vbt(const void *buffer)
{
	const struct opregion_vbt *vbt = buffer;
	char *s;

	printf("OpRegion Mailbox 4: Video BIOS Table (VBT):\n");

	s = strndup(vbt->product_string, sizeof(vbt->product_string));
	printf("\tproduct string:\t%s\n", s);
	free(s);

	printf("\t(use intel_bios_reader to decode the VBT)\n");

	printf("\n");
}

static void decode_asle_ext(const void *buffer)
{
	const struct opregion_asle_ext *asle_ext = buffer;
	int i;

	printf("OpRegion Mailbox 5: BIOS to Driver Notification Extension:\n");

	printf("\tphed:\t0x%08x\n", asle_ext->phed);

	printf("\tbddc:\n");
	for (i = 0; i < ARRAY_SIZE(asle_ext->bddc); i++) {
		const int COLUMNS = 16;

		if (i % COLUMNS == 0)
			printf("\t\tbddc[0x%02x]:\t", i);

		printf("%02x ", asle_ext->bddc[i]);

		if (i % COLUMNS == COLUMNS - 1)
			printf("\n");
	}

	printf("\n");
}

static void decode_opregion(const uint8_t *opregion, int size)
{
	uint32_t mbox;

	/* XXX: allow decoding up to size */
	if (OPREGION_ASLE_EXT_OFFSET + sizeof(struct opregion_asle_ext) > size) {
		fprintf(stderr, "buffer too small\n");
		return;
	}

	mbox = decode_header(opregion + OPREGION_HEADER_OFFSET);
	if (mbox & MBOX_ACPI)
		decode_acpi(opregion + OPREGION_ACPI_OFFSET);
	if (mbox & MBOX_SWSCI)
		decode_swsci(opregion + OPREGION_SWSCI_OFFSET);
	if (mbox & MBOX_ASLE)
		decode_asle(opregion + OPREGION_ASLE_OFFSET);
	if (mbox & MBOX_VBT)
		decode_vbt(opregion + OPREGION_VBT_OFFSET);
	if (mbox & MBOX_ASLE_EXT)
		decode_asle_ext(opregion + OPREGION_ASLE_EXT_OFFSET);
}

int main(int argc, char *argv[])
{
	const char *filename = "/sys/kernel/debug/dri/0/i915_opregion";
	int fd;
	struct stat finfo;
	uint8_t *opregion;
	int c, option_index = 0;

	static struct option long_options[] = {
		{ "file", required_argument, 0, 'f' },
		{ "help", no_argument, 0, 'h' },
		{ 0 },
	};

	while ((c = getopt_long(argc, argv, "hf:",
				long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			printf("usage: intel_opregion_decode [-f|--file=<input>]\n");
			return 0;
		case 'f':
			filename = optarg;
			break;
		default:
			fprintf(stderr, "unkown command options\n");
			return 1;
		}
	}

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		printf("Couldn't open \"%s\": %s\n", filename, strerror(errno));
		return 1;
	}

	if (stat(filename, &finfo)) {
		printf("failed to stat \"%s\": %s\n", filename, strerror(errno));
		return 1;
	}

	if (finfo.st_size == 0) {
		int len = 0, ret;
		finfo.st_size = 8192;
		opregion = malloc(finfo.st_size);
		while ((ret = read(fd, opregion + len, finfo.st_size - len))) {
			if (ret < 0) {
				printf("failed to read \"%s\": %s\n", filename,
				       strerror(errno));
				return 1;
			}

			len += ret;
			if (len == finfo.st_size) {
				finfo.st_size *= 2;
				opregion = realloc(opregion, finfo.st_size);
			}
		}
	} else {
		opregion = mmap(NULL, finfo.st_size, PROT_READ, MAP_SHARED,
				fd, 0);
		if (opregion == MAP_FAILED) {
			printf("failed to map \"%s\": %s\n", filename,
			       strerror(errno));
			return 1;
		}
	}

	decode_opregion(opregion, finfo.st_size);

	return 0;
}
