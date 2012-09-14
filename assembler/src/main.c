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

long int gen_level = 4;
int advanced_flag = 0; /* 0: in unit of byte, 1: in unit of data element size */
int binary_like_output = 0; /* 0: default output style, 1: nice C-style output */
int need_export = 0;
char *input_filename = "<stdin>";
char *export_filename = NULL;

const char const *binary_prepend = "static const char gen_eu_bytes[] = {\n";

struct brw_program compiled_program;
struct program_defaults program_defaults = {.register_type = BRW_REGISTER_TYPE_F};


#define HASHSZ 	37
static struct declared_register *declared_register_table[HASHSZ];

static const struct option longopts[] = {
	{"advanced", no_argument, 0, 'a'},
	{"binary", no_argument, 0, 'b'},
	{"export", required_argument, 0, 'e'},
	{"input_list", required_argument, 0, 'l'},
	{"output", required_argument, 0, 'o'},
	{"gen", required_argument, 0, 'g'},
	{ NULL, 0, NULL, 0 }
};

static void usage(void)
{
	fprintf(stderr, "usage: intel-gen4asm [options] inputfile\n");
	fprintf(stderr, "OPTIONS:\n");
	fprintf(stderr, "\t-a, --advanced                       Set advanced flag\n");
	fprintf(stderr, "\t-b, --binary                         C style binary output\n");
	fprintf(stderr, "\t-e, --export {exportfile}            Export label file\n");
	fprintf(stderr, "\t-l, --input_list {entrytablefile}    Input entry_table_list file\n");
	fprintf(stderr, "\t-o, --output {outputfile}            Specify output file\n");
	fprintf(stderr, "\t-g, --gen <4|5|6|7>                  Specify GPU generation\n");
}

static int hash(char *name)
{
    unsigned ret = 0;
    while(*name)
        ret = (ret << 1) + (*name++);
    return ret % HASHSZ;
}

struct declared_register *find_register(char *name)
{
    int index = hash(name);
    struct declared_register *reg;
    for (reg = declared_register_table[index];reg; reg = reg->next)
	if (strcasecmp(reg->name,name) == 0)
	    return reg;
    return NULL;
}

void insert_register(struct declared_register *reg)
{
    int	index = hash(reg->name);
    reg->next = declared_register_table[index];
    declared_register_table[index] = reg;
}

static void free_register_table(void)
{
    struct declared_register *reg, *next;
    int i;
    for (i = 0; i < HASHSZ; i++) {
	reg = declared_register_table[i];
	while(reg) {
	    next = reg->next;
	    free(reg->name);
	    free(reg);
	    reg = next;
	}
    }
}

struct entry_point_item {
	char *str;
	struct entry_point_item *next;
} *entry_point_table;

static int read_entry_file(char *fn)
{
	FILE *entry_table_file;
	char buf[2048];
	struct entry_point_item **p = &entry_point_table;
	if (!fn)
		return 0;
	if ((entry_table_file = fopen(fn, "r")) == NULL)
		return -1;
	while (fgets(buf, sizeof(buf)-1, entry_table_file) != NULL) {
		// drop the final char '\n'
		if(buf[strlen(buf)-1] == '\n')
			buf[strlen(buf)-1] = 0;
		*p = calloc(1, sizeof(struct entry_point_item));
		(*p)->str = strdup(buf);
		p = &((*p)->next);
	}
	fclose(entry_table_file);
	return 0;
}

static int is_entry_point(char *s)
{
	struct entry_point_item *p;
	for (p = entry_point_table; p; p = p->next) {
	    if (strcmp(p->str, s) == 0)
		return 1;
	}
	return 0;
}

static void free_entry_point_table(struct entry_point_item *p) {
	if (p) {
		free_entry_point_table(p->next);
		free(p->str);
		free(p);
	}
}

static void
print_instruction(FILE *output, struct brw_program_instruction *entry)
{
	if (binary_like_output) {
		fprintf(output, "\t0x%02x, 0x%02x, 0x%02x, 0x%02x, "
				"0x%02x, 0x%02x, 0x%02x, 0x%02x,\n"
				"\t0x%02x, 0x%02x, 0x%02x, 0x%02x, "
				"0x%02x, 0x%02x, 0x%02x, 0x%02x,\n",
			((unsigned char *)(&entry->instruction))[0],
			((unsigned char *)(&entry->instruction))[1],
			((unsigned char *)(&entry->instruction))[2],
			((unsigned char *)(&entry->instruction))[3],
			((unsigned char *)(&entry->instruction))[4],
			((unsigned char *)(&entry->instruction))[5],
			((unsigned char *)(&entry->instruction))[6],
			((unsigned char *)(&entry->instruction))[7],
			((unsigned char *)(&entry->instruction))[8],
			((unsigned char *)(&entry->instruction))[9],
			((unsigned char *)(&entry->instruction))[10],
			((unsigned char *)(&entry->instruction))[11],
			((unsigned char *)(&entry->instruction))[12],
			((unsigned char *)(&entry->instruction))[13],
			((unsigned char *)(&entry->instruction))[14],
			((unsigned char *)(&entry->instruction))[15]);
	} else {
		fprintf(output, "   { 0x%08x, 0x%08x, 0x%08x, 0x%08x },\n",
			((int *)(&entry->instruction))[0],
			((int *)(&entry->instruction))[1],
			((int *)(&entry->instruction))[2],
			((int *)(&entry->instruction))[3]);
	}
}
int main(int argc, char **argv)
{
	char *output_file = NULL;
	char *entry_table_file = NULL;
	FILE *output = stdout;
	FILE *export_file;
	struct brw_program_instruction *entry, *entry1, *tmp_entry;
	int err, inst_offset;
	char o;
	while ((o = getopt_long(argc, argv, "e:l:o:g:ab", longopts, NULL)) != -1) {
		switch (o) {
		case 'o':
			if (strcmp(optarg, "-") != 0)
				output_file = optarg;

			break;

		case 'g':
			gen_level = strtol(optarg, NULL, 0);

			if (gen_level < 4 || gen_level > 7) {
				usage();
				exit(1);
			}

			break;

		case 'a':
			advanced_flag = 1;
			break;
		case 'b':
			binary_like_output = 1;
			break;

		case 'e':
			need_export = 1;
			if (strcmp(optarg, "-") != 0)
				export_filename = optarg;
			break;

		case 'l':
			if (strcmp(optarg, "-") != 0)
				entry_table_file = optarg;
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

	if (strcmp(argv[0], "-"))
		fclose(yyin);

	yylex_destroy();

	if (err || errors)
		exit (1);

	if (output_file) {
		output = fopen(output_file, "w");
		if (output == NULL) {
			perror("Couldn't open output file");
			exit(1);
		}

	}

	if (read_entry_file(entry_table_file)) {
		fprintf(stderr, "Read entry file error\n");
		exit(1);
	}
	inst_offset = 0 ;
	for (entry = compiled_program.first;
		entry != NULL; entry = entry->next) {
	    entry->inst_offset = inst_offset;
	    entry1 = entry->next;
	    if (entry1 && entry1->islabel && is_entry_point(entry1->string)) {
		// insert empty instructions until (inst_offset+1) % 4 == 0
		while (((inst_offset+1) % 4) != 0) {
		    tmp_entry = calloc(sizeof(*tmp_entry), 1);
		    entry->next = tmp_entry;
		    tmp_entry->next = entry1;
		    entry = tmp_entry;
		    tmp_entry->inst_offset = ++inst_offset;
		}
	    }
	    if (!entry->islabel)
              inst_offset++;
	}

	if (need_export) {
		if (export_filename) {
			export_file = fopen(export_filename, "w");
		} else {
			export_file = fopen("export.inc", "w");
		}
		for (entry = compiled_program.first;
			entry != NULL; entry = entry->next) {
		    if (entry->islabel) 
			fprintf(export_file, "#define %s_IP %d\n",
				entry->string, (gen_level == 5 ? 2 : 1)*(entry->inst_offset));
		}
		fclose(export_file);
	}

	for (entry = compiled_program.first;
		entry != NULL; entry = entry->next) {
	    if (!entry->islabel) {
		if (entry->instruction.reloc_target) {
			entry1 = entry;
			int found = 0;
			do {
			if (entry1->islabel && 
				strcmp(entry1->string, 
				    entry->instruction.reloc_target) == 0) {
			    int offset = 
				entry1->inst_offset - entry->inst_offset;
			    int delta = (entry->instruction.header.opcode == BRW_OPCODE_JMPI ? 1 : 0);
                            if (gen_level >= 5)
                                    entry->instruction.bits3.ud = 2 * (offset - delta);
                            else
                                    entry->instruction.bits3.ud = offset - delta;

                            if (entry->instruction.header.opcode == BRW_OPCODE_ELSE)
                                    entry->instruction.bits3.if_else.pop_count = 1;
				found = 1;
			    break;
			}
			entry1 = entry1->next;
			if (entry1 == NULL)
				entry1 = compiled_program.first;
			} while (entry1 != entry);
		    if (found == 0)
			fprintf(stderr, "can not find label %s\n",
				entry->instruction.reloc_target);
		}
	    }
	}


	if (binary_like_output)
		fprintf(output, "%s", binary_prepend);

	for (entry = compiled_program.first;
		entry != NULL;
		entry = entry1) {
	    entry1 = entry->next;
	    if (!entry->islabel)
		print_instruction(output, entry);
	    else
		free(entry->string);
	    free(entry);
	}
	if (binary_like_output)
		fprintf(output, "};");

	free_entry_point_table(entry_point_table);
	free_register_table();
	fflush (output);
	if (ferror (output)) {
	    perror ("Could not flush output file");
	    if (output_file)
		unlink (output_file);
	    err = 1;
	}
	return err;
}
