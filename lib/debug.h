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

#ifndef _DEBUG_H_
#define _DEBUG_H_

#define DEBUG_PROTOCOL_VERSION 1
#define COMMUNICATION_OFFSET 0xc00
#define COMMUNICATION_QWORD 0xc0

#define STATE_EU_MSG 0x47534d65 /* eMSG */
#define STATE_CPU_ACK 0x4b434163 /* cACK */
#define STATE_OFFSET 0xc20
#define STATE_QWORD 0xc2

#define TX_OFFSET 0xc40
#define TX_QWORD 0xc4
#define RX_OFFSET 0xc60
#define RX_QWORD 0xc6

#ifndef GEN_ASM
typedef uint32_t grf[8];
typedef uint32_t mrf[8];
typedef uint8_t cr[12];
typedef uint32_t sr;

#define DWORD8(x) {x, x, x, x, x, x, x, x}

const static grf protocol_version = DWORD8(DEBUG_PROTOCOL_VERSION);
const static grf eu_msg = DWORD8(STATE_EU_MSG);
const static grf cpu_ack = DWORD8(STATE_CPU_ACK);

struct eu_state {
	mrf m_regs[15];
	grf g_regs[16];
	grf pad;

/* 0x400 */
	cr cr0;
	sr sr0;
	uint32_t beef_pad[4];
	uint8_t pad2[992 + 1024];

/* 0xc00 COMMUNICATION_OFFSET */
	grf version;
	grf state_magic;
	grf eu_tx;
	grf eu_rx;

	uint8_t pad3[896];
} __attribute__((packed));

static inline void
print_reg(uint8_t reg[32]) {
	uint32_t *dwords = (uint32_t *)reg;
	printf("%08x %08x %08x %08x %08x %08x %08x %08x",
		dwords[7], dwords[6], dwords[5], dwords[4],
		dwords[3], dwords[2], dwords[1], dwords[0]);
}

static inline void
print_creg(uint8_t reg[12]) {
	uint32_t *dwords = (uint32_t *)reg;
	printf("%08x %08x %08x", dwords[2], dwords[1], dwords[0]);
}
#endif

#endif
