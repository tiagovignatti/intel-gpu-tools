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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __INTEL_REG_SPEC_H__
#define __INTEL_REG_SPEC_H__

enum port_addr {
	PORT_NONE = 0,
	PORT_MMIO = -1,
	PORT_PORTIO_VGA = -2,	/* see vga reg read/write */
	PORT_MMIO_VGA = -3,	/* see vga reg read/write */

	/* vlv */
	PORT_BUNIT = 0x03,
	PORT_PUNIT = 0x04,
	PORT_NC = 0x11,
	PORT_DPIO = 0x12,
	PORT_GPIO_NC = 0x13,
	PORT_CCK = 0x14,
	PORT_CCU = 0xa9,
	PORT_DPIO2 = 0x1a,
	PORT_FLISDSI = 0x1b,

	/* threshold for interpreting port as mmio offset */
	PORT_MAX = 0xff,
};

struct port_desc {
	enum port_addr port;
	const char *name;
	uint32_t stride;
};

struct reg {
	struct port_desc port_desc;
	uint32_t mmio_offset;
	uint32_t addr;
	char *name;
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

static inline void *recalloc(void *ptr, size_t nmemb, size_t size)
{
	return realloc(ptr, nmemb * size);
}

int parse_port_desc(struct reg *reg, const char *s);
ssize_t intel_reg_spec_builtin(struct reg **regs, uint32_t devid);
ssize_t intel_reg_spec_file(struct reg **regs, const char *filename);
void intel_reg_spec_free(struct reg *regs, size_t n);
int intel_reg_spec_decode(char *buf, size_t bufsize, const struct reg *reg,
			  uint32_t val, uint32_t devid);
void intel_reg_spec_print_ports(void);

#endif /* __INTEL_REG_SPEC_H__ */
