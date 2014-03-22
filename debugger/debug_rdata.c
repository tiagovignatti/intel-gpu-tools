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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "intel_io.h"
#include "intel_chipset.h"

struct eu_rdata {
	union {
		struct {
			uint8_t sendc_dep : 1;
			uint8_t swh_dep : 1;
			uint8_t pwc_dep : 1;
			uint8_t n2_dep : 1;
			uint8_t n1_dep : 1;
			uint8_t n0_dep : 1;
			uint8_t flag1_dep : 1;
			uint8_t flag0_dep : 1;
			uint8_t indx_dep : 1;
			uint8_t mrf_dep : 1;
			uint8_t dst_dep : 1;
			uint8_t src2_dep : 1;
			uint8_t src1_dep : 1;
			uint8_t src0_dep : 1;
			uint8_t mp_dep_pin : 1;
			uint8_t sp_dep_pin : 1;
			uint8_t fftid : 8;
			uint8_t ffid : 4;
			uint8_t instruction_valid : 1;
			uint8_t thread_status : 3;
		};
		uint32_t dword;
	} ud0;

	union {
		struct {
			uint8_t mrf_addr : 4;
			uint8_t dst_addr : 7;
			uint8_t src2_addr : 7;
			uint8_t src1_addr : 7;
			uint8_t src0_addr : 7;
		};
		uint32_t dword;
	} ud1;

	union {
		struct {
			uint16_t exip : 12;
			uint8_t opcode : 7;
			uint8_t pwc : 8;
			uint8_t instruction_valid : 1;
			uint8_t mbz : 4;
		};
		uint32_t dword;
	} ud2;
};

const char *thread_status[] = 
	{"INVALID", "invalid/no thread", "standby (dependency)", "INVALID", "Executing",
	 "INVALID" , "INVALID" , "INVALID"};

static struct eu_rdata
collect_rdata(int eu, int tid) {
	struct eu_rdata rdata;

	intel_register_write(0x7800, eu << 16 | (3 * tid) << 8);
	rdata.ud0.dword = intel_register_read(0x7840);

	intel_register_write(0x7800, eu << 16 | (3 * tid + 1) << 8);
	rdata.ud1.dword = intel_register_read(0x7840);

	intel_register_write(0x7800, eu << 16 | (3 * tid + 2) << 8);
	rdata.ud2.dword = intel_register_read(0x7840);

	return rdata;
}
static void
print_rdata(struct eu_rdata rdata) {
	printf("\t%s\n", thread_status[rdata.ud0.thread_status]);
	printf("\tn1_dep: %d\n", rdata.ud0.n1_dep);
	printf("\tpwc_dep: %d\n", rdata.ud0.pwc_dep);
	printf("\tswh_dep: %d\n", rdata.ud0.swh_dep);
	printf("\tsource 0 %x\n", rdata.ud1.src0_addr);
	printf("\tsource 1 %x\n", rdata.ud1.src1_addr);
	printf("\tsource 2 %x\n", rdata.ud1.src2_addr);
	printf("\tdest  %x\n", rdata.ud1.dst_addr);
	printf("\tmrf  %x\n", rdata.ud1.mrf_addr);
	printf("\tIP: %x\n", rdata.ud2.exip);
	printf("\topcode: %x\n", rdata.ud2.opcode);
}

static void
find_stuck_threads(void)
{
	int i, j;
	for (i = 0; i < 15; i++)
		for (j = 0; j < 5; j++) {
			struct eu_rdata rdata;
			rdata = collect_rdata(i, j);
			if (rdata.ud0.thread_status == 2 ||
			    rdata.ud0.thread_status == 4) {
				printf("%d %d:\n", i, j);
				print_rdata(rdata);
			}
	}
}

int main(int argc, char *argv[]) {
	struct pci_device *pci_dev;
	pci_dev = intel_get_pci_device();

	intel_register_access_init(pci_dev, 1);
	find_stuck_threads();
//	collect_rdata(atoi(argv[1]), atoi(argv[2]));
	return 0;
}
