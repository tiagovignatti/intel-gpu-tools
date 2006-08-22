%{
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
#include <string.h>
#include "gen4asm.h"
#include "brw_defines.h"

%}

%start ROOT

%union {
	char *s;
	int integer;
	double number;
	struct brw_instruction instruction;
	struct brw_program program;
	struct region {
		int vert_stride, width, horiz_stride;
	} region;
	struct gen_reg {
		int reg_file, reg_nr, subreg_nr;
	} direct_gen_reg; /* XXX: naming */
	double imm32;
}

%token SEMICOLON
%token LPAREN RPAREN
%token LANGLE RANGLE
%token LCURLY RCURLY
%token COMMA
%token DOT

%token TYPE_UD, TYPE_D, TYPE_UW, TYPE_W, TYPE_UB, TYPE_B,
%token TYPE_VF, TYPE_HF, TYPE_V, TYPE_F

%token <integer> ALIGN1 ALIGN16 MASK_DISABLE EOT

%token GENREG MSGREG ACCREG ADDRESSREG FLAGREG CONTROLREG IPREG

%token MOV
%token MUL MAC MACH LINE SAD2 SADA2 DP4 DPH DP3 DP2
%token ADD
%token SEND NULL_TOKEN MATH SAMPLER GATEWAY READ WRITE URB THREAD_SPAWNER
%token NOP

%token MSGLEN RETURNLEN
%token SATURATE

%token <integer> INTEGER
%token <number> NUMBER

%type <instruction> instruction unaryinstruction binaryinstruction
%type <instruction> binaryaccinstruction triinstruction sendinstruction
%type <instruction> specialinstruction
%type <instruction> dst dstoperand dstoperandex dstreg
%type <instruction> directsrcaccoperand src directsrcoperand srcimm
%type <instruction> srcacc srcaccimm
%type <instruction> instoptions instoption_list
%type <program> instrseq
%type <integer> instoption
%type <integer> unaryop binaryop binaryaccop
%type <integer> conditionalmodifier saturate
%type <integer> regtype srcimmtype execsize dstregion
%type <integer> subregnum msgtarget
%type <region> region
%type <direct_gen_reg> directgenreg directmsgreg addrreg accreg flagreg maskreg
%type <direct_gen_reg> nullreg
%type <imm32> imm32

%%

ROOT:		instrseq
		{
		  compiled_program = $1;
		}
;

instrseq:	instruction SEMICOLON instrseq
		{
		  struct brw_program_instruction *list_entry =
		    calloc(sizeof(struct brw_program_instruction), 1);
		  list_entry->instruction = $1;

		  list_entry->next = $3.first;
		  $3.first = list_entry;

		  $$ = $3;
		}
		| instruction SEMICOLON
		{
		  struct brw_program_instruction *list_entry =
		    calloc(sizeof(struct brw_program_instruction), 1);
		  list_entry->instruction = $1;

		  list_entry->next = NULL;

		  $$.first = list_entry;
		}
		| error SEMICOLON instrseq
		{
		  $$ = $3;
		}
;

/* 1.4.1: Instruction groups */
instruction:	unaryinstruction
		| binaryinstruction
		| binaryaccinstruction
		| triinstruction
		| specialinstruction
;

unaryinstruction:
		predicate unaryop conditionalmodifier saturate execsize
		dst srcaccimm instoptions
		{
		  $$.header.opcode = $2;
		  $$.header.saturate = $3;
		  $$.header.destreg__conditionalmod = $4;
		  $$.header.execution_size = $5;
		  set_instruction_dest(&$$, &$6);
		  set_instruction_src0(&$$, &$7);
		  set_instruction_options(&$$, &$8);
		}
;

unaryop:	MOV { $$ = BRW_OPCODE_MOV; }
;

binaryinstruction:
		predicate binaryop conditionalmodifier saturate execsize
		dst src srcimm instoptions
		{
		  $$.header.opcode = $2;
		  $$.header.saturate = $3;
		  $$.header.destreg__conditionalmod = $4;
		  $$.header.execution_size = $5;
		  set_instruction_dest(&$$, &$6);
		  set_instruction_src0(&$$, &$7);
		  set_instruction_src1(&$$, &$8);
		  set_instruction_options(&$$, &$9);
		}
;

binaryop:	MUL { $$ = BRW_OPCODE_MUL; }
		| MAC { $$ = BRW_OPCODE_MAC; }

binaryaccinstruction:
		predicate binaryaccop conditionalmodifier saturate execsize
		dst srcacc srcimm instoptions
		{
		  $$.header.opcode = $2;
		  $$.header.saturate = $3;
		  $$.header.destreg__conditionalmod = $4;
		  $$.header.execution_size = $5;
		  set_instruction_dest(&$$, &$6);
		  set_instruction_src0(&$$, &$7);
		  set_instruction_src1(&$$, &$8);
		  set_instruction_options(&$$, &$9);
		}
;

binaryaccop:	ADD { $$ = BRW_OPCODE_ADD; }
;

triinstruction:	sendinstruction

/* XXX formatting of this instruction */
sendinstruction: predicate SEND INTEGER execsize dst payload msgtarget
		MSGLEN INTEGER RETURNLEN INTEGER instoptions
		{
		  $$.header.opcode = BRW_OPCODE_SEND;
		  $$.header.execution_size = $4;
		  $$.header.destreg__conditionalmod = $3;
		}

specialinstruction: NOP
		{
		  $$.header.opcode = BRW_OPCODE_NOP;
		}

/* XXX! */
payload: directsrcoperand
;

msgtarget:	NULL_TOKEN { $$ = BRW_MESSAGE_TARGET_NULL; }
		| SAMPLER { $$ = BRW_MESSAGE_TARGET_SAMPLER; }
		| MATH { $$ = BRW_MESSAGE_TARGET_MATH; }
		| GATEWAY { $$ = BRW_MESSAGE_TARGET_GATEWAY; }
		| READ { $$ = BRW_MESSAGE_TARGET_DATAPORT_READ; }
		| WRITE { $$ = BRW_MESSAGE_TARGET_DATAPORT_WRITE; }
		| URB { $$ = BRW_MESSAGE_TARGET_URB; }
		| THREAD_SPAWNER { $$ = BRW_MESSAGE_TARGET_THREAD_SPAWNER; }
;

/* 1.4.2: Destination register */

dst:		dstoperand | dstoperandex

/* XXX: dstregion writemask */
dstoperand:	dstreg dstregion regtype
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  $$.bits1 = $1.bits1;
		  $$.bits1.da1.dest_horiz_stride = $2;
		  $$.bits1.da1.dest_reg_type = $3;
		}
;

dstoperandex:	accreg dstregion regtype
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		  $$.bits1.da1.dest_horiz_stride = $2;
		  $$.bits1.da1.dest_reg_type = $3;
		}
		| nullreg
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		}
;

/* XXX: indirectgenreg, directmsgreg, indirectmsgreg */
dstreg:		directgenreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		}
		| directmsgreg

;

/* 1.4.3: Source register */
srcaccimm:	srcacc
		| imm32 srcimmtype
		{
		  $$.bits1.da1.src0_reg_file = BRW_IMMEDIATE_VALUE;
		  switch ($2) {
		  case BRW_REGISTER_TYPE_UD:
		    $$.bits3.ud = $1;
		    break;
		  case BRW_REGISTER_TYPE_D:
		    $$.bits3.id = $1;
		    break;
		  case BRW_REGISTER_TYPE_F:
		    $$.bits3.fd = $1;
		    break;
		  }
		}
;

/* XXX: indirectsrcaccoperand */
srcacc:		directsrcaccoperand
;

srcimm:		directsrcoperand
		| imm32 srcimmtype
		{
		  $$.bits1.da1.src0_reg_file = BRW_IMMEDIATE_VALUE;
		  switch ($2) {
		  case BRW_REGISTER_TYPE_UD:
		    $$.bits3.ud = $1;
		    break;
		  case BRW_REGISTER_TYPE_D:
		    $$.bits3.id = $1;
		    break;
		  case BRW_REGISTER_TYPE_F:
		    $$.bits3.fd = $1;
		    break;
		  }
		}
;

/* XXX: srcaccoperandex, accreg regtype */
directsrcaccoperand:	directsrcoperand
;

/* XXX: indirectsrcoperand */
src:		directsrcoperand
;

/* XXX: srcmodifier, swizzle srcaccoperandex */
directsrcoperand:	directgenreg region regtype
		{
		  /* Returns a source operand in the src0 fields of an
		   * instruction.
		   */
		  $$.bits1.da1.src0_reg_file = $1.reg_file;
		  $$.bits1.da1.src0_reg_type = $3;
		  $$.bits2.da1.src0_subreg_nr = $1.subreg_nr;
		  $$.bits2.da1.src0_reg_nr = $1.reg_nr;
		  $$.bits2.da1.src0_vert_stride = $2.vert_stride;
		  $$.bits2.da1.src0_width = $2.width;
		  $$.bits2.da1.src0_horiz_stride = $2.horiz_stride;
		}
;

subregnum:	DOT INTEGER
		{
		  $$ = $2;
		}
		|
		{
		  /* Default to subreg 0 if unspecified. */
		  $$ = 0;
		}
;

/* 1.4.5: Register files and register numbers */
directgenreg:	GENREG INTEGER subregnum
		{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.reg_nr = $2;
		  $$.subreg_nr = $3;
		}

directmsgreg:	MSGREG INTEGER subregnum
		{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.reg_nr = $2;
		  $$.subreg_nr = $3;
		}
;

accreg:		ACCREG INTEGER subregnum
		{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_ACCUMULATOR | $2;
		  $$.subreg_nr = $3;
		}
;

addrreg:	ADDRESSREG INTEGER subregnum
		{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_ADDRESS | $2;
		  $$.subreg_nr = $3;
		}
;

nullreg:	NULL_TOKEN
		{
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_NULL;
		  $$.subreg_nr = 0;
		}
;

/* 1.4.7: Regions */
dstregion:	LANGLE INTEGER RANGLE
		{
		  /* Returns a value for a horiz_stride field of an
		   * instruction.
		   */
		  if ($2 != 1 && $2 != 2 && $2 != 4) {
		    fprintf(stderr, "Invalid horiz size %d\n", $2);
		  }
		  $$ = ffs($2);
		}
;

region:		LANGLE INTEGER COMMA INTEGER COMMA INTEGER RANGLE
		{
		  $$.vert_stride = ffs($2);
		  $$.width = ffs($4) - 1;
		  $$.horiz_stride = ffs($6);
		}
;

/* 1.4.8: Types */

/* regtype returns an integer register type suitable for inserting into an
 * instruction.
 */
regtype:	TYPE_F { $$ = BRW_REGISTER_TYPE_F; }
		| TYPE_UD { $$ = BRW_REGISTER_TYPE_UD; }
		| TYPE_D { $$ = BRW_REGISTER_TYPE_D; }
		| TYPE_UW { $$ = BRW_REGISTER_TYPE_UW; }
		| TYPE_W { $$ = BRW_REGISTER_TYPE_UW; }
		| TYPE_UB { $$ = BRW_REGISTER_TYPE_UB; }
		| TYPE_B { $$ = BRW_REGISTER_TYPE_B; }
/* XXX: Add TYPE_VF and TYPE_HF */
srcimmtype:	regtype
;

/* 1.4.11: */
imm32:		INTEGER { $$ = $1; }
		| NUMBER { $$ = $1; }

/* 1.4.12: Predication and modifiers */
/* XXX: do the predicate */
predicate:

execsize:	LPAREN INTEGER RPAREN
		{
		  /* Returns a value for the execution_size field of an
		   * instruction.
		   */
		  if ($2 != 1 && $2 != 2 && $2 != 4 && $2 != 8 && $2 != 16 &&
		      $2 != 32) {
		    fprintf(stderr, "Invalid execution size %d\n", $2);
		    YYERROR;
		  }
		  $$ = ffs($2) - 1;
		}
;

saturate:	/* empty */ { $$ = BRW_INSTRUCTION_NORMAL; }
		| DOT SATURATE { $$ = BRW_INSTRUCTION_SATURATE; }
;

conditionalmodifier:
;

/* 1.4.13: Instruction options */
/* XXX: this is a comma-separated list, really. */
instoptions:	LCURLY instoption_list RCURLY
		{ $$ = $2; }
;

instoption_list: instoption instoption_list
		{
		  $$ = $2;
		  switch ($1) {
		  case ALIGN1:
		    $$.header.access_mode = BRW_ALIGN_1;
		    break;
		  case ALIGN16:
		    $$.header.access_mode = BRW_ALIGN_16;
		    break;
		  case MASK_DISABLE:
		    $$.header.mask_control = BRW_MASK_DISABLE;
		    break;
		  case EOT:
		    /* XXX: EOT shouldn't be here */
		    break;
		  }
		}
		| /* empty, header defaults to zeroes. */
;

/* XXX: fill me in. alignctrl, comprctrl, threadctrl, depctrl, maskctrl,
 * debugctrl, sendctrl
 */
instoption:	ALIGN1 | ALIGN16 | MASK_DISABLE | EOT
;

%%
extern int yylineno;

void yyerror (char *msg)
{
	fprintf(stderr, "parse error \"%s\" at line %d, token \"%s\"\n",
		msg, yylineno, lex_text());
}

/**
 * Fills in the destination register information in instr from the bits in dst.
 */
void set_instruction_dest(struct brw_instruction *instr,
			 struct brw_instruction *dest)
{
	instr->bits1.da1.dest_reg_file = dest->bits1.da1.dest_reg_file;
	instr->bits1.da1.dest_reg_type = dest->bits1.da1.dest_reg_type;
	instr->bits1.da1.dest_subreg_nr = dest->bits1.da1.dest_subreg_nr;
	instr->bits1.da1.dest_reg_nr = dest->bits1.da1.dest_reg_nr;
	instr->bits1.da1.dest_horiz_stride = dest->bits1.da1.dest_horiz_stride;
	instr->bits1.da1.dest_address_mode = dest->bits1.da1.dest_address_mode;
}


void set_instruction_src0(struct brw_instruction *instr,
			  struct brw_instruction *src)
{
	instr->bits1.da1.src0_reg_file = src->bits1.da1.src0_reg_file;
	instr->bits1.da1.src0_reg_type = src->bits1.da1.src0_reg_type;
	if (src->bits1.da1.src0_reg_file == BRW_IMMEDIATE_VALUE) {
		instr->bits3.ud = src->bits3.ud;
	} else {
		instr->bits2.da1.src0_subreg_nr =
			src->bits2.da1.src0_subreg_nr;
		instr->bits2.da1.src0_reg_nr = src->bits2.da1.src0_reg_nr;
		instr->bits2.da1.src0_vert_stride =
			src->bits2.da1.src0_vert_stride;
		instr->bits2.da1.src0_width = src->bits2.da1.src0_width;
		instr->bits2.da1.src0_horiz_stride =
			src->bits2.da1.src0_horiz_stride;
	}
}

void set_instruction_src1(struct brw_instruction *instr,
			  struct brw_instruction *src)
{
	instr->bits1.da1.src1_reg_file = src->bits1.da1.src0_reg_file;
	instr->bits1.da1.src1_reg_type = src->bits1.da1.src0_reg_type;
	if (src->bits1.da1.src0_reg_file == BRW_IMMEDIATE_VALUE) {
		instr->bits3.ud = src->bits3.ud;
	} else {
		instr->bits3.da1.src1_subreg_nr =
			src->bits2.da1.src0_subreg_nr;
		instr->bits3.da1.src1_reg_nr = src->bits2.da1.src0_reg_nr;
		instr->bits3.da1.src1_vert_stride =
			src->bits2.da1.src0_vert_stride;
		instr->bits3.da1.src1_width = src->bits2.da1.src0_width;
		instr->bits3.da1.src1_horiz_stride =
			src->bits2.da1.src0_horiz_stride;
	}
}

void set_instruction_options(struct brw_instruction *instr,
			     struct brw_instruction *options)
{
	instr->header.access_mode = options->header.access_mode;
	instr->header.mask_control = options->header.mask_control;
	instr->header.dependency_control = options->header.dependency_control;
	instr->header.compression_control =
		options->header.compression_control;
}
