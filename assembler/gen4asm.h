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
#include <stdbool.h>
#include <assert.h>

#include "brw_reg.h"
#include "brw_defines.h"
#include "brw_structs.h"
#include "gen8_instruction.h"

extern long int gen_level;
extern int advanced_flag;
extern int errors;

#define WARN_ALWAYS	(1 << 0)
#define WARN_ALL	(1 << 31)
extern unsigned int warning_flags;

extern char *input_filename;

extern struct brw_context genasm_context;
extern struct brw_compile genasm_compile;

/* Predicate for Gen X and above */
#define IS_GENp(x) (gen_level >= (x)*10)

/* Predicate for Gen X exactly */
#define IS_GENx(x) (gen_level >= (x)*10 && gen_level < ((x)+1)*10)

/* Predicate to match Haswell processors */
#define IS_HASWELL(x) (gen_level == 75)

void yyerror (char *msg);

#define STRUCT_SIZE_ASSERT(TYPE, SIZE) \
typedef struct { \
          char compile_time_assert_ ## TYPE ## _size[ \
              (sizeof (struct TYPE) == (SIZE)) ? 1 : -1]; \
        } _ ## TYPE ## SizeCheck

/* ensure nobody changes the size of struct brw_instruction */
STRUCT_SIZE_ASSERT(brw_instruction, 16);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

struct condition {
    	int cond;
	int flag_reg_nr;
	int flag_subreg_nr;
};

struct predicate {
    unsigned pred_control:4;
    unsigned pred_inverse:1;
    unsigned flag_reg_nr:1;
    unsigned flag_subreg_nr:1;
};

struct options {
    unsigned access_mode:1;
    unsigned compression_control:2; /* gen6: quater control */
    unsigned thread_control:2;
    unsigned dependency_control:2;
    unsigned mask_control:1;
    unsigned debug_control:1;
    unsigned acc_wr_control:1;

    unsigned end_of_thread:1;
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
 * This structure is the internal representation of source operands in the
 * parser.
 */
struct src_operand {
	struct brw_reg reg;
	int default_region;
	uint32_t imm32; /* set if src_operand is expressing a branch offset */
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

enum assembler_instruction_type {
    GEN4ASM_INSTRUCTION_GEN,
    GEN4ASM_INSTRUCTION_GEN_RELOCATABLE,
    GEN4ASM_INSTRUCTION_GEN8,
    GEN4ASM_INSTRUCTION_GEN8_RELOCATABLE,
    GEN4ASM_INSTRUCTION_LABEL,
};

struct label_instruction {
    char   *name;
};

struct relocation {
    char *first_reloc_target, *second_reloc_target; // JIP and UIP respectively
    int first_reloc_offset, second_reloc_offset; // in number of instructions
};

/**
 * This structure is just the list container for instructions accumulated by
 * the parser and labels.
 */
struct brw_program_instruction {
    enum assembler_instruction_type type;
    unsigned inst_offset;
    union {
	struct brw_instruction gen;
	struct gen8_instruction gen8;
	struct label_instruction label;
    } insn;
    struct relocation reloc;
    struct brw_program_instruction *next;
};

static inline bool is_label(struct brw_program_instruction *instruction)
{
    return instruction->type == GEN4ASM_INSTRUCTION_LABEL;
}

static inline char *label_name(struct brw_program_instruction *i)
{
    assert(is_label(i));
    return i->insn.label.name;
}

static inline bool is_relocatable(struct brw_program_instruction *intruction)
{
    return intruction->type == GEN4ASM_INSTRUCTION_GEN_RELOCATABLE;
}

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
    struct brw_reg reg;
    int element_size;
    struct region src_region;
    int dst_region;
};
struct declared_register *find_register(char *name);
void insert_register(struct declared_register *reg);

int yyparse(void);
int yylex(void);
int yylex_destroy(void);

char *
lex_text(void);

#endif /* __GEN4ASM_H__ */
