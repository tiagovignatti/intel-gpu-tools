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

#include <inttypes.h>

typedef unsigned char GLubyte;
typedef short GLshort;
typedef unsigned int GLuint;
typedef int GLint;
typedef float GLfloat;

#include "brw_structs.h"

void yyerror (char *msg);

/**
 * This structure is the internal representation of directly-addressed
 * registers in the parser.
 */
struct direct_reg {
	int reg_file, reg_nr, subreg_nr;
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

	int address_mode; /* 0 if direct, 1 if register-indirect */
	int address_subreg_nr;
	int indirect_offset; /* XXX */

	int swizzle_set;
	int swizzle_x, swizzle_y, swizzle_z, swizzle_w;

	uint32_t imm32; /* only set if reg_file == BRW_IMMEDIATE_VALUE */
} src_operand;

/**
 * This structure is just the list container for instructions accumulated by
 * the parser.
 */
struct brw_program_instruction {
	struct brw_instruction instruction;
	struct brw_program_instruction *next;
};

/**
 * This structure is a list of instructions.  It is the final output of the
 * parser.
 */
struct brw_program {
	struct brw_program_instruction *first;
};

extern struct brw_program compiled_program;

int yyparse(void);
int yylex(void);

char *
lex_text(void);
