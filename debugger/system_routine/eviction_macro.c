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

#include <stdio.h>

#define START 0x100
#define END ((128 << 10) / 4)

int main(int argc, char *argv[]) {
	int i;
	printf("#ifdef SANDYBRIDGE\n");
	printf("#define EVICT_CACHE \\\n");
	printf("\tmov (1) m0.5:ud g0.5:ud FLAGS; \\\n");
	for (i = START; i < END - 8; i+=0x8) {
		printf("\tmov (1) m0.2:ud 0x%04x:ud FLAGS; \\\n", i);
		printf("\tWRITE_SCRATCH4(m0); \\\n");
	}

	printf("\tmov (1) m0.2:ud 0x%04x:ud FLAGS; \\\n", i);
	printf("\tWRITE_SCRATCH4(m0)\n");
	printf("#else\n");
	printf("#define EVICT_CACHE\n");
	printf("#endif\n");
}
