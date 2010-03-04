/* -*- c-basic-offset: 4 -*- */
/*
 * Copyright © 2007 Intel Corporation
 * Copyright © 2009 Intel Corporation
 * Copyright © 2010 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Carl Worth <cworth@cworth.org>
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

/** @file intel_decode.c
 * This file contains code to print out batchbuffer contents in a
 * human-readable format.
 *
 * The current version only supports i915 packets, and only pretty-prints a
 * subset of them.  The intention is for it to make just a best attempt to
 * decode, but never crash in the process.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <err.h>

#include "intel_decode.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"
#include "instdone.h"

static void
print_instdone (unsigned int instdone, unsigned int instdone1)
{
    int i;

    for (i = 0; i < num_instdone_bits; i++) {
	int busy = 0;

	if (instdone_bits[i].reg == INST_DONE_1) {
	    if (!(instdone1 & instdone_bits[i].bit))
		busy = 1;
	} else {
	    if (!(instdone & instdone_bits[i].bit))
		busy = 1;
	}

	if (busy)
	    printf("    busy: %s\n", instdone_bits[i].name);
    }
}

static void
print_i830_pgtbl_err(unsigned int reg)
{
	const char *str;

	switch((reg >> 3) & 0xf) {
	case 0x1: str = "Overlay TLB"; break;
	case 0x2: str = "Display A TLB"; break;
	case 0x3: str = "Host TLB"; break;
	case 0x4: str = "Render TLB"; break;
	case 0x5: str = "Display C TLB"; break;
	case 0x6: str = "Mapping TLB"; break;
	case 0x7: str = "Command Stream TLB"; break;
	case 0x8: str = "Vertex Buffer TLB"; break;
	case 0x9: str = "Display B TLB"; break;
	case 0xa: str = "Reserved System Memory"; break;
	case 0xb: str = "Compressor TLB"; break;
	case 0xc: str = "Binner TLB"; break;
	default: str = "unknown"; break;
	}

	if (str)
		printf ("    source = %s\n", str);

	switch(reg & 0x7) {
	case 0x0: str  = "Invalid GTT"; break;
	case 0x1: str = "Invalid GTT PTE"; break;
	case 0x2: str = "Invalid Memory"; break;
	case 0x3: str = "Invalid TLB miss"; break;
	case 0x4: str = "Invalid PTE data"; break;
	case 0x5: str = "Invalid LocalMemory not present"; break;
	case 0x6: str = "Invalid Tiling"; break;
	case 0x7: str = "Host to CAM"; break;
	}
	printf ("    error = %s\n", str);
}

static void
print_pgtbl_err(unsigned int reg, unsigned int devid)
{
	if (IS_965(devid)) {
	} else if (IS_9XX(devid)) {
	} else {
		return print_i830_pgtbl_err(reg);
	}
}

static void
read_data_file (const char * filename)
{
    FILE *file;
    int devid = PCI_CHIP_I855_GM;
    uint32_t *data = NULL;
    int data_size = 0, count = 0, line_number = 0, matched;
    char *line = NULL;
    size_t line_size;
    uint32_t offset, value;
    uint32_t gtt_offset = 0, new_gtt_offset;
    char *buffer_type[2] = {  "ringbuffer", "batchbuffer" };
    int is_batch = 1;

    file = fopen (filename, "r");
    if (file == NULL) {
	fprintf (stderr, "Failed to open %s: %s\n",
		 filename, strerror (errno));
	exit (1);
    }

    while (getline (&line, &line_size, file) > 0) {
	line_number++;

	matched = sscanf (line, "--- gtt_offset = 0x%08x\n", &new_gtt_offset);
	if (matched == 1) {
	    if (count) {
		printf("%s at 0x%08x:\n", buffer_type[is_batch], gtt_offset);
		intel_decode (data, count, gtt_offset, devid, 0);
		count = 0;
	    }
	    gtt_offset = new_gtt_offset;
	    is_batch = 1;
	    continue;
	}

	matched = sscanf (line, "--- ringbuffer = 0x%08x\n", &new_gtt_offset);
	if (matched == 1) {
	    if (count) {
		printf("%s at 0x%08x:\n", buffer_type[is_batch], gtt_offset);
		intel_decode (data, count, gtt_offset, devid, 0);
		count = 0;
	    }
	    gtt_offset = new_gtt_offset;
	    is_batch = 0;
	    continue;
	}

	matched = sscanf (line, "%08x : %08x", &offset, &value);
	if (matched != 2) {
	    unsigned int reg;

	    printf("%s", line);

	    matched = sscanf (line, "PCI ID: 0x%04x\n", &reg);
	    if (matched)
		    devid = reg;

	    matched = sscanf (line, "  ACTHD: 0x%08x\n", &reg);
	    if (matched)
		    intel_decode_context_set_head_tail(reg, 0xffffffff);

	    matched = sscanf (line, "  PGTBL_ER: 0x%08x\n", &reg);
	    if (matched && reg)
		    print_pgtbl_err(reg, devid);

	    matched = sscanf (line, "  INSTDONE: 0x%08x\n", &reg);
	    if (matched)
		print_instdone (reg, -1);

	    matched = sscanf (line, "  INSTDONE1: 0x%08x\n", &reg);
	    if (matched)
		print_instdone (-1, reg);

	    continue;
	}

	count++;

	if (count > data_size) {
	    data_size = data_size ? data_size * 2 : 1024;
	    data = realloc (data, data_size * sizeof (uint32_t));
	    if (data == NULL) {
		fprintf (stderr, "Out of memory.\n");
		exit (1);
	    }
	}

	data[count-1] = value;
    }

    if (count) {
	printf("%s at 0x%08x:\n", buffer_type[is_batch], gtt_offset);
	intel_decode (data, count, gtt_offset, devid, 0);
    }

    free (data);
    free (line);

    fclose (file);
}

int
main (int argc, char *argv[])
{
    const char *path;
    struct stat st;
    int err;

    if (argc > 2) {
	fprintf (stderr,
		 "intel_gpu_decode: Parse an Intel GPU i915_error_state\n"
		 "Usage:\n"
		 "\t%s [<file>]\n"
		 "\n"
		 "With no arguments, debugfs-dri-directory is probed for in "
		 "/debug and \n"
		 "/sys/kernel/debug.  Otherwise, it may be "
		 "specified.  If a file is given,\n"
		 "it is parsed as an GPU dump in the format of "
		 "/debug/dri/0/i915_error_state.\n",
		 argv[0]);
	return 1;
    }

    init_instdone_definitions();

    if (argc == 1) {
	path = "/debug/dri/0";
	err = stat (path, &st);
	if (err != 0) {
	    path = "/sys/kernel/debug/dri/0";
	    err = stat (path, &st);
	    if (err != 0) {
		errx(1,
		     "Couldn't find i915 debugfs directory.\n\n"
		     "Is debugfs mounted? You might try mounting it with a command such as:\n\n"
		     "\tsudo mount -t debugfs debugfs /sys/kernel/debug\n");
	    }
	}
    } else {
	path = argv[1];
	err = stat (path, &st);
	if (err != 0) {
	    fprintf (stderr, "Error opening %s: %s\n",
		     path, strerror (errno));
	    exit (1);
	}
    }

    if (S_ISDIR (st.st_mode)) {
	char *filename;

	asprintf (&filename, "%s/i915_error_state", path);
	read_data_file (filename);
	free (filename);
    } else {
	read_data_file (path);
    }

    return 0;
}
