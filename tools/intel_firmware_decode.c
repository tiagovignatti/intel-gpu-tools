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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Damien Lespiau <damien.lespiau@intel.com>
 */

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "igt_core.h"

#define __packed                        __attribute__((packed))

struct intel_css_header {
	/* 0x09 for DMC */
	uint32_t module_type;

	/* Includes the DMC specific header in dwords */
	uint32_t header_len;

	/* always value would be 0x10000 */
	uint32_t header_ver;

	/* Not used */
	uint32_t module_id;

	/* Not used */
	uint32_t module_vendor;

	/* in YYYYMMDD format */
	uint32_t date;

	/* Size in dwords (CSS_Headerlen + PackageHeaderLen + dmc FWsLen)/4 */
	uint32_t size;

	/* Not used */
	uint32_t key_size;

	/* Not used */
	uint32_t modulus_size;

	/* Not used */
	uint32_t exponent_size;

	/* Not used */
	uint32_t reserved1[12];

	/* Major Minor */
	uint32_t version;

	/* Not used */
	uint32_t reserved2[8];

	/* Not used */
	uint32_t kernel_header_info;
} __packed;

struct intel_fw_info {
	uint16_t reserved1;

	/* Stepping (A, B, C, ..., *). * is a wildcard */
	char stepping;

	/* Sub-stepping (0, 1, ..., *). * is a wildcard */
	char substepping;

	uint32_t offset;
	uint32_t reserved2;
} __packed;

struct intel_package_header {
	/* DMC container header length in dwords */
	unsigned char header_len;

	/* always value would be 0x01 */
	unsigned char header_ver;

	unsigned char reserved[10];

	/* Number of valid entries in the FWInfo array below */
	uint32_t num_entries;

	struct intel_fw_info fw_info[20];
} __packed;

struct intel_dmc_header {
	/* always value would be 0x40403E3E */
	uint32_t signature;

	/* DMC binary header length */
	unsigned char header_len;

	/* 0x01 */
	unsigned char header_ver;

	/* Reserved */
	uint16_t dmcc_ver;

	/* Major, Minor */
	uint32_t	project;

	/* Firmware program size (excluding header) in dwords */
	uint32_t	fw_size;

	/* Major Minor version */
	uint32_t fw_version;

	/* Number of valid MMIO cycles present. */
	uint32_t mmio_count;

	/* MMIO address */
	uint32_t mmioaddr[8];

	/* MMIO data */
	uint32_t mmiodata[8];

	/* FW filename  */
	unsigned char dfile[32];

	uint32_t reserved1[2];
} __packed;

typedef struct {
	int fd;
	uint8_t *base;
	struct intel_css_header *css_header;
	struct intel_package_header *package_header;
} csr_t;

static void csr_open(csr_t *ctx, const char *filename)
{
	struct stat st;

	ctx->fd = open(filename, O_RDWR);
	igt_fail_on_f(ctx->fd == -1, "Couldn't open %s\n", filename);

	fstat(ctx->fd, &st);
	ctx->base = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
			 ctx->fd, 0);
	igt_fail_on_f(ctx->base == MAP_FAILED, "Couldn't mmap %s\n", filename);

	printf("Firmware: %s (%"PRId64" bytes)\n", filename, (int64_t)st.st_size);

	ctx->css_header = (struct intel_css_header *)ctx->base;
	ctx->package_header = (struct intel_package_header *)
				(ctx->base + sizeof(*ctx->css_header));
}

#define print_d32(p, field) \
	printf("    "#field": %u\n", (p)->field)
#define print_x32(p, field) \
	printf("    "#field": 0x%x\n", (p)->field)
#define print_s(p, field) \
	printf("    "#field": %s\n", (p)->field)

static const char *module_type_name(uint32_t module_type)
{
	switch (module_type) {
	case 0x9:
		return "DMC";
	default:
		return "Unknown";
	}
}

static void dump_css(csr_t *ctx)
{
	struct intel_css_header *css = ctx->css_header;

	printf("CSS header (%zd bytes)\n", sizeof(*css));
	printf("    module_type: %s (%d)\n", module_type_name(css->module_type),
	       css->module_type);
	print_d32(css, header_len);
	print_x32(css, header_ver);
	print_x32(css, module_id);
	print_x32(css, module_vendor);
	print_x32(css, date);
	print_d32(css, size);
	print_d32(css, key_size);
	print_d32(css, modulus_size);
	print_d32(css, exponent_size);
	/* uint32_t reserved1[12]; */
	printf("    version: %d.%d (0x%x)\n", css->version >> 16,
	       css->version & 0xffff, css->version);
	/* uint32_t reserved2[8]; */
	print_x32(css, kernel_header_info);

}

static void dump_dmc(csr_t *ctx, struct intel_fw_info *info)
{
	struct intel_dmc_header *dmc;
	unsigned int i;

	if (info->offset == 0xffffffff)
		return;

	dmc = (struct intel_dmc_header *)(ctx->base + sizeof(*ctx->css_header)
					  + sizeof(*ctx->package_header) +
					  info->offset);

	print_x32(dmc, signature);
	print_d32(dmc, header_len);
	print_d32(dmc, header_ver);
	print_d32(dmc, dmcc_ver);
	print_x32(dmc, project);
	print_d32(dmc, fw_size);
	print_x32(dmc, fw_version);
	print_d32(dmc, mmio_count);

	for (i = 0; i < dmc->mmio_count; i++) {
		printf("        write(0x%08x, 0x%08x)\n", dmc->mmioaddr[i],
		       dmc->mmiodata[i]);
	}
}

static void dump_package(csr_t *ctx)
{
	struct intel_package_header *package = ctx->package_header;
	unsigned int i;

	printf("Package header (%zd bytes)\n", sizeof(*package));

	print_d32(package, header_len);
	print_d32(package, header_ver);
	/* unsigned char reserved[10]; */
	print_d32(package, num_entries);

	for (i = 0; i < package->num_entries; i++) {
		struct intel_fw_info *info = &package->fw_info[i];

		printf("Firmware #%d\n", i + 1);
		printf("    stepping: %c.%c\n", info->stepping,
		       info->substepping);
		print_d32(info, offset);

		dump_dmc(ctx, info);
	}
}

static void csr_dump(csr_t *ctx)
{
	dump_css(ctx);
	dump_package(ctx);
}

static csr_t ctx;

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s firmware.bin\n", argv[0]);
		return 1;
	}

	csr_open(&ctx, argv[1]);
	csr_dump(&ctx);

	return 0;
}
