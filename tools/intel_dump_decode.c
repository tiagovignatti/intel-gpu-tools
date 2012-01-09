/*
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#include <intel_bufmgr.h>

struct drm_intel_decode *ctx;

static void
read_bin_file(const char * filename)
{
	uint32_t buf[16384];
	int fd, offset, ret;

	if (!strcmp(filename, "-"))
		fd = fileno(stdin);
	else
		fd = open (filename, O_RDONLY);
	if (fd < 0) {
		fprintf (stderr, "Failed to open %s: %s\n",
			 filename, strerror (errno));
		exit (1);
	}

	drm_intel_decode_set_dump_past_end(ctx, 1);

	offset = 0;
	while ((ret = read (fd, buf, sizeof(buf))) > 0) {
		drm_intel_decode_set_batch_pointer(ctx, buf, offset, ret/4);
		drm_intel_decode(ctx);
		offset += ret;
	}
	close (fd);
}

static void
read_data_file(const char * filename)
{
    FILE *file;
    uint32_t *data = NULL;
    int data_size = 0, count = 0, line_number = 0, matched;
    char *line = NULL;
    size_t line_size;
    uint32_t offset, value;
    uint32_t gtt_offset = 0;

	if (!strcmp(filename, "-"))
		file = stdin;
	else
		file = fopen (filename, "r");

    if (file == NULL) {
	fprintf (stderr, "Failed to open %s: %s\n",
		 filename, strerror (errno));
	exit (1);
    }

    while (getline (&line, &line_size, file) > 0) {
	line_number++;

	matched = sscanf (line, "%08x : %08x", &offset, &value);
	if (matched != 2) {
	    printf("ignoring line %s", line);

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
	drm_intel_decode_set_batch_pointer(ctx, data, gtt_offset, count);
	drm_intel_decode(ctx);
    }

    free (data);
    free (line);

    fclose (file);
}

static void
read_autodetect_file(const char * filename)
{
	int binary = 0, c;
	FILE *file;

	file = fopen (filename, "r");
	if (file == NULL) {
		fprintf (stderr, "Failed to open %s: %s\n",
			 filename, strerror (errno));
		exit (1);
	}

	while ((c = fgetc(file)) != EOF) {
		/* totally lazy binary detector */
		if (c < 10) {
			binary = 1;
			break;
		}
	}

	fclose(file);

	if (binary == 1)
		read_bin_file(filename);
	else
		read_data_file(filename);

}


int
main (int argc, char *argv[])
{
	uint32_t devid = 0xa011;
	int i, c;
	int option_index = 0;
	int binary = -1;

	static struct option long_options[] = {
		{"devid", 1, 0, 'd'},
		{"ascii", 0, 0, 'a'},
		{"binary", 0, 0, 'b'}
	};

	while((c = getopt_long(argc, argv, "ab",
			       long_options, &option_index)) != -1) {
		switch(c) {
		case 'd':
			devid = strtoul(optarg, NULL, 0);
			break;
		case 'b':
			binary = 1;
			break;
		case 'a':
			binary = 0;
			break;
		default:
			printf("unkown command options\n");
			break;
		}
	}

	ctx = drm_intel_decode_context_alloc(devid);

	if (optind == argc) {
		fprintf(stderr, "no input file given\n");
		exit(-1);
	}

	for (i = optind; i < argc; i++) {
		/* For stdin input, let's read as data file */
		if (!strcmp(argv[i], "-")) {
			read_data_file(argv[i]);
			continue;
		}
		if (binary == 1)
			read_bin_file(argv[i]);
		else if (binary == 0)
			read_data_file(argv[i]);
		else
			read_autodetect_file(argv[i]);
	}

	return 0;
}
