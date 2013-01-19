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

#ifndef __GEN4ASM_H__
#define __GEN4ASM_H__

#include <inttypes.h>

typedef unsigned char GLubyte;
typedef short GLshort;
typedef unsigned int GLuint;
typedef int GLint;
typedef float GLfloat;

extern long int gen_level;

/* Predicate for Gen X and above */
#define IS_GENp(x) (gen_level >= (x)*10)

/* Predicate for Gen X exactly */
#define IS_GENx(x) (gen_level >= (x)*10 && gen_level < ((x)+1)*10)

/* Predicate to match Haswell processors */
#define IS_HASWELL(x) (gen_level == 75)

#include "brw_defines.h"
#include "brw_structs.h"

void yyerror (char *msg);

/**
 * This structure is the internal representation of directly-addressed
 * registers in the parser.
 */
struct direct_reg {
	int reg_file, reg_nr, subreg_nr;
};

struct condition {
    	int cond;
	int flag_reg_nr;
	int flag_subreg_nr;
};

struct region {
    int vert_stride, width, horiz_stride;
    int is_default;        
};
struct regtype {
    int type;
    int is_default;
};
/**
 * This structure is the internal representation of register-indirect addressed
 * registers in the parser.
 */

struct indirect_reg {
	int reg_file, address_subreg_nr, indirect_offset;
};

/**
 * This structure is the internal representation of destination operands in the
 * parser.
 */
struct dst_operand {
	int reg_file, reg_nr, subreg_nr, reg_type;

	int writemask_set;
	int writemask;

	int horiz_stride;
	int address_mode; /* 0 if direct, 1 if register-indirect */

	/* Indirect addressing */
	int address_subreg_nr;
	int indirect_offset;
};

/**
 * This structure is the internal representation of source operands in the 
 * parser.
 */
struct src_operand {
	int reg_file, reg_nr, subreg_nr, reg_type;

	int abs, negate;

	int horiz_stride, width, vert_stride;
	int default_region;

	int address_mode; /* 0 if direct, 1 if register-indirect */
	int address_subreg_nr;
	int indirect_offset; /* XXX */

	int swizzle_set;
	int swizzle_x, swizzle_y, swizzle_z, swizzle_w;

	uint32_t imm32; /* set if reg_file == BRW_IMMEDIATE_VALUE or it is expressing a branch offset */
	char *reloc_target; /* bspec: branching instructions JIP and UIP are source operands */
} src_operand;

typedef struct {
    enum {
	imm32_d, imm32_f
    } r;
    union {
	uint32_t    d;
	float	    f;
	int32_t	    signed_d;
    } u;
} imm32_t;

/**
 * This structure is just the list container for instructions accumulated by
 * the parser and labels.
 */
struct brw_program_instruction {
	struct brw_instruction instruction;
	struct brw_program_instruction *next;
	GLuint islabel;
	GLuint inst_offset;
	char   *string;
};

/**
 * This structure is a list of instructions.  It is the final output of the
 * parser.
 */
struct brw_program {
	struct brw_program_instruction *first;
	struct brw_program_instruction *last;
};

extern struct brw_program compiled_program;

#define TYPE_B_INDEX            0
#define TYPE_UB_INDEX           1
#define TYPE_W_INDEX            2
#define TYPE_UW_INDEX           3
#define TYPE_D_INDEX            4
#define TYPE_UD_INDEX           5
#define TYPE_F_INDEX            6

#define TOTAL_TYPES             7

struct program_defaults {
    int execute_size;
    int execute_type[TOTAL_TYPES];
    int register_type;
    int register_type_regfile;
    struct region source_region;
    struct region source_region_type[TOTAL_TYPES];
    struct region dest_region;
    struct region dest_region_type[TOTAL_TYPES];
};
extern struct program_defaults program_defaults;

struct declared_register {
    char *name;
    struct direct_reg base;
    int element_size;
    struct region src_region;
    int dst_region;
    int type;
};
struct declared_register *find_register(char *name);
void insert_register(struct declared_register *reg);
void add_label(char *name, int addr);
int label_to_addr(char *name, int start_addr);

int yyparse(void);
int yylex(void);
int yylex_destroy(void);

char *
lex_text(void);

#endif /* __GEN4ASM_H__ */
