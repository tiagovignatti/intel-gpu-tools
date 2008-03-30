/* -*- c-basic-offset: 8 -*- */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include "gen4asm.h"

extern FILE *yyin;

extern int errors;

char *input_filename = "<stdin>";

struct brw_program compiled_program;

static const struct option longopts[] = {
	{ NULL, 0, NULL, 0 }
};

static void usage(void)
{
	fprintf(stderr, "usage: intel-gen4asm [-o outputfile] inputfile\n");
}

int main(int argc, char **argv)
{
	char *output_file = NULL;
	FILE *output = stdout;
	struct brw_program_instruction *entry;
	int err;
	char o;

	while ((o = getopt_long(argc, argv, "o:", longopts, NULL)) != -1) {
		switch (o) {
		case 'o':
			if (strcmp(optarg, "-") != 0)
				output_file = optarg;
			break;
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usage();
		exit(1);
	}

	if (strcmp(argv[0], "-") != 0) {
		input_filename = argv[0];
		yyin = fopen(input_filename, "r");
		if (yyin == NULL) {
			perror("Couldn't open input file");
			exit(1);
		}
	}

	err = yyparse();

	if (err || errors)
		exit (1);

	if (output_file) {
		output = fopen(output_file, "w");
		if (output == NULL) {
			perror("Couldn't open output file");
			exit(1);
		}
	}
	for (entry = compiled_program.first;
	     entry != NULL;
	     entry = entry->next) {
		fprintf(output, "   { 0x%08x, 0x%08x, 0x%08x, 0x%08x },\n",
		       ((int *)(&entry->instruction))[0],
		       ((int *)(&entry->instruction))[1],
		       ((int *)(&entry->instruction))[2],
		       ((int *)(&entry->instruction))[3]);
	}

	fflush (output);
	if (ferror (output)) {
		perror ("Could not flush output file");
		if (output_file)
			unlink (output_file);
		err = 1;
	}
	return err;
}
