/*
 * Copyright © 2008 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include "gen4asm.h"
#include "brw_eu.h"
#include "gen8_instruction.h"

static const struct option longopts[] = {
	{ NULL, 0, NULL, 0 }
};

static struct brw_program *
read_program (FILE *input)
{
    uint32_t			    inst[4];
    struct brw_program		    *program;
    struct brw_program_instruction  *entry, **prev;
    int			c;
    int			n = 0;

    program = malloc (sizeof (struct brw_program));
    program->first = NULL;
    prev = &program->first;
    while ((c = getc (input)) != EOF) {
	if (c == '0') {
	    if (fscanf (input, "x%x", &inst[n]) == 1) {
		++n;
		if (n == 4) {
		    entry = malloc (sizeof (struct brw_program_instruction));
		    memcpy (&entry->insn, inst, 4 * sizeof (uint32_t));
		    entry->next = NULL;
		    *prev = entry;
		    prev = &entry->next;
		    n = 0;
		}
	    }
	}
    }
    return program;
}

static struct brw_program *
read_program_binary (FILE *input)
{
    uint32_t			    temp;
    uint8_t			    inst[16];
    struct brw_program		    *program;
    struct brw_program_instruction  *entry, **prev;
    int			c;
    int			n = 0;

    program = malloc (sizeof (struct brw_program));
    program->first = NULL;
    prev = &program->first;
    while ((c = getc (input)) != EOF) {
	if (c == '0') {
	    if (fscanf (input, "x%2x", &temp) == 1) {
		inst[n++] = (uint8_t)temp;
		if (n == 16) {
		    entry = malloc (sizeof (struct brw_program_instruction));
		    memcpy (&entry->insn, inst, 16 * sizeof (uint8_t));
		    entry->next = NULL;
		    *prev = entry;
		    prev = &entry->next;
		    n = 0;
		}
	    }
	}
    }
    return program;
}

static void usage(void)
{
    fprintf(stderr, "usage: intel-gen4disasm [options] inputfile\n");
    fprintf(stderr, "\t-b, --binary                         C style binary output\n");
    fprintf(stderr, "\t-o, --output {outputfile}            Specify output file\n");
    fprintf(stderr, "\t-g, --gen <4|5|6|7|8|9>              Specify GPU generation\n");
}

int main(int argc, char **argv)
{
    struct brw_program	*program;
    FILE		*input = stdin;
    FILE		*output = stdout;
    char		*input_filename = NULL;
    char		*output_file = NULL;
    int			byte_array_input = 0;
    int			o;
    int			gen = 4;
    struct brw_program_instruction  *inst;

    while ((o = getopt_long(argc, argv, "o:bg:", longopts, NULL)) != -1) {
	switch (o) {
	case 'o':
	    if (strcmp(optarg, "-") != 0)
		output_file = optarg;
	    break;
	case 'b':
	    byte_array_input = 1;
	    break;
	case 'g':
	    gen = strtol(optarg, NULL, 10);

	    if (gen < 4 || gen > 9) {
		    usage();
		    exit(1);
	    }

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
	input = fopen(input_filename, "r");
	if (input == NULL) {
	    perror("Couldn't open input file");
	    exit(1);
	}
    }
    if (byte_array_input)
	program = read_program_binary (input);
    else
	program = read_program (input);
    if (!program)
	exit (1);
    if (output_file) {
	output = fopen (output_file, "w");
	if (output == NULL) {
	    perror("Couldn't open output file");
	    exit(1);
	}
    }

    for (inst = program->first; inst; inst = inst->next)
	if (gen >= 8)
	    gen8_disassemble(output, &inst->insn.gen8, gen);
	else
	    brw_disasm (output, &inst->insn.gen, gen);

    exit (0);
}
