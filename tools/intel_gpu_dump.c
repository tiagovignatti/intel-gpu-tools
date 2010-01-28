/* -*- c-basic-offset: 4 -*- */
/*
 * Copyright © 2007 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Carl Worth <cworth@cworth.org>
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

/* Read a data file of the following form:
 *
 *	Offset0 :  Data0
 *	Offset1 :  Data1
 *	...
 *
 * Where both Offset and Data are ASCII representations of 8-digit
 * hexadecimal numbers.
 *
 * After this function returns, *data will point to an allocated
 * buffer, (which should be free()ed by the caller), and *count will
 * indicate the number of data values read from the filename.
 *
 * Note: The values of the offset field are currently ignored. There
 * are no guarantees that errors will be detected at all, (but for any
 * error that is detected, this function is likely to just call
 * exit()).
 */
static void
read_data_file (const char * filename, int is_batch)
{
    FILE *file;
    uint32_t *data = NULL;
    int data_size = 0, count = 0, line_number = 0, matched;
    char *line = NULL;
    size_t line_size;
    uint32_t offset, value;
    uint32_t gtt_offset = 0, new_gtt_offset;
    char *buffer_type[2] = {  "ringbuffer", "batchbuffer" };

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

	    matched = sscanf (line, "  ACTHD: 0x%08x\n", &reg);
	    if (matched)
		    intel_decode_context_set_head_tail(reg, 0xffffffff);

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

/* Grab the head/tail pointers we know about so we can annotate the batch
 * and ring dumps.
 */
static void
parse_ringbuffer_info(const char *filename,
		      uint32_t *ring_head, uint32_t *ring_tail,
		      uint32_t *acthd)
{
    FILE *file;
    int matched;
    char *line = NULL;
    size_t line_size;

    *ring_head = 0xffffffff;
    *ring_tail = 0xffffffff;
    *acthd = 0xffffffff;

    file = fopen (filename, "r");
    if (file == NULL) {
	fprintf (stderr, "Failed to open %s: %s\n",
		 filename, strerror (errno));
	exit (1);
    }

    while (getline (&line, &line_size, file) > 0) {
	uint32_t val;

	matched = sscanf (line, "RingHead : %x\n", &val);
	if (matched == 1) {
	    *ring_head = val;
	    continue;
	}

	matched = sscanf (line, "RingTail : %x\n", &val);
	if (matched == 1) {
	    *ring_tail = val;
	    continue;
	}

	matched = sscanf (line, "Acthd : %x\n", &val);
	if (matched == 1) {
	    *acthd = val;
	    continue;
	}
    }

    free (line);

    fclose (file);
}

/* By default, we grab the state of the current hardware
 * by looking into the various debugfs nodes and grabbing all the
 * relevant data.
 *
 * A secondary mode is to interpret a file with data captured
 * previously. This is less interesting since a single file won't have
 * compelte information, (we want both ringbuffer plus batchbuffer as
 * well as error-status registers, etc). But for now, we'll start with
 * this secondary mode as we let this program mature.
 */
int
main (int argc, char *argv[])
{
    const char *path;
    struct stat st;
    int err;
    uint32_t instdone, instdone1 = 0;

    if (argc > 2) {
	fprintf (stderr,
		 "intel_gpu_dump: Parse an Intel GPU ringbuffer/batchbuffer state\n"
		 "\n"
		 "Usage:\n"
		 "\t%s\n"
		 "\t%s <debugfs-dri-directory>\n"
		 "\t%s <data-file>\n"
		 "\n"
		 "With no arguments, debugfs-dri-directory is probed for in "
		 "/debug and \n"
		 "/sys/kernel/debug.  Otherwise, it may be "
		 "specified.  If a file is given,\n"
		 "it is parsed as a batchbuffer in the format of "
		 "/debug/dri/0/i915_batchbuffers.\n",
		 argv[0], argv[0], argv[0]);
	return 1;
    }

    intel_get_mmio();
    init_instdone_definitions();

    if (argc == 1) {
	path = "/debug/dri/0";
	err = stat(path, &st);
	if (err != 0) {
	    path = "/sys/kernel/debug/dri/0";
	    err = stat(path, &st);
	    if (err != 0) {
		errx(1,
		     "Couldn't find i915 debugfs directory.\n\n"
		     "Is debugfs mounted? You might try mounting it with a command such as:\n\n"
		     "\tsudo mount -t debugfs debugfs /sys/kernel/debug\n");
	    }
	}
    } else {
	path = argv[1];
	err = stat(path, &st);
	if (err != 0) {
	    fprintf (stderr, "Error opening %s: %s\n",
		     path, strerror (errno));
	    exit (1);
	}
    }

    if (S_ISDIR(st.st_mode)) {
	char *filename;
	uint32_t ring_head, ring_tail, acthd;

	asprintf(&filename, "%s/i915_ringbuffer_info", path);

	err = stat(filename, &st);
	if (err != 0) {
	    fprintf (stderr,
		     "Error opening %s: %s\n\n"
		     "Perhaps your i915 kernel driver has no support for "
		     "dumping batchbuffer data?\n"
		     "(In kernels prior to 2.6.30 this requires "
		     "manually-applied patches.)\n",
		     filename, strerror (errno));
	    exit (1);
	}

	parse_ringbuffer_info(filename, &ring_head, &ring_tail, &acthd);
	free (filename);

	printf("ACTHD: 0x%08x\n", acthd);
	printf("EIR: 0x%08x\n", INREG(EIR));
	printf("EMR: 0x%08x\n", INREG(EMR));
	printf("ESR: 0x%08x\n", INREG(ESR));
	printf("PGTBL_ER: 0x%08x\n", INREG(PGTBL_ER));

	if (IS_965(devid)) {
	    instdone = INREG(INST_DONE_I965);
	    instdone1 = INREG(INST_DONE_1);

	    printf("IPEHR: 0x%08x\n", INREG(IPEHR_I965));
	    printf("IPEIR: 0x%08x\n", INREG(IPEIR_I965));
	    printf("INSTDONE: 0x%08x\n", instdone);
	    printf("INSTDONE1: 0x%08x\n", instdone1);
	} else {
	    instdone = INREG(INST_DONE);

	    printf("IPEHR: 0x%08x\n", INREG(IPEHR));
	    printf("IPEIR: 0x%08x\n", INREG(IPEIR));
	    printf("INSTDONE: 0x%08x\n", instdone);
	}

	print_instdone (instdone, instdone1);

	asprintf (&filename, "%s/i915_batchbuffers", path);
	intel_decode_context_set_head_tail(acthd, 0xffffffff);
	read_data_file (filename, 1);
	free (filename);

	asprintf (&filename, "%s/i915_ringbuffer_data", path);
	intel_decode_context_set_head_tail(ring_head, ring_tail);
	printf("Ringbuffer: ");
	printf("Reminder: head pointer is GPU read, tail pointer is CPU "
	       "write\n");
	read_data_file (filename, 0);
	free (filename);
    } else {
	read_data_file (path, 1);
    }

    return 0;
}
