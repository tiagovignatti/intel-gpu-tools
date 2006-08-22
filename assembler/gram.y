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
	struct direct_gen_reg {
		int reg_file, reg_nr, subreg_nr;
	} direct_gen_reg;
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

%token ALIGN1

%token GENREGFILE MSGREGFILE

%token MOV
%token MUL MAC MACH LINE SAD2 SADA2 DP4 DPH DP3 DP2
%token ADD
%token SEND NULL_TOKEN MATH SAMPLER GATEWAY READ WRITE URB THREAD_SPAWNER

%token MSGLEN RETURNLEN

%token <integer> INTEGER
%token <number> NUMBER

%type <instruction> instruction unaryinstruction binaryinstruction
%type <instruction> binaryaccinstruction triinstruction sendinstruction
%type <instruction> dstoperand dstoperandex dstreg accreg
%type <instruction> directsrcaccoperand src directsrcoperand srcimm
%type <instruction> srcacc srcaccimm
%type <program> instrseq
%type <integer> unaryop binaryop binaryaccop triop
%type <integer> regtype srcimmtype execsize dstregion
%type <integer> gensubregnum msgsubregnum msgtarget
%type <region> region
%type <direct_gen_reg> directgenreg directmsgreg
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
;

unaryinstruction: predicate unaryop execsize dst srcaccimm instoptions
		{
		  $$.header.opcode = $2;
		  $$.header.execution_size = $3;
		}
;

unaryop:	MOV { $$ = BRW_OPCODE_MOV; }
;

binaryinstruction:
		predicate binaryop execsize dst src srcimm instoptions
		{
		  $$.header.opcode = $2;
		  $$.header.execution_size = $3;
		}
;

binaryop:	MUL { $$ = BRW_OPCODE_MUL; }

binaryaccinstruction:
		predicate binaryaccop execsize dst srcacc srcimm instoptions
		{
		  $$.header.opcode = $2;
		  $$.header.execution_size = $3;
		}
;

binaryaccop:	ADD { $$ = BRW_OPCODE_ADD; }
;

triinstruction:	sendinstruction

/* XXX formatting of this instruction */
sendinstruction: predicate SEND INTEGER execsize postdst curdst msgtarget
		MSGLEN INTEGER RETURNLEN INTEGER instoptions
		{
		  $$.header.opcode = BRW_OPCODE_SEND;
		  $$.header.execution_size = $4;
		  $$.header.destreg__conditonalmod = $3;
		}

/* XXX! */
postdst: dstoperand
;

/* XXX! */
curdst: directsrcoperand
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

/** XXX: dstoperandex */
dst:		dstoperand

/* XXX: dstregion writemask */
dstoperand:	dstreg dstregion regtype
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  $$.bits1 = $1.bits1;
		  $$.bits1.da1.dest_reg_type = $2;
		}

dstoperandex:	accreg dstregion regtype
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  $$.bits1 = $1.bits1;
		  $$.bits1.da1.dest_reg_type = $2;
		}

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

/* 1.4.5: Register files and register numbers */
directgenreg:	GENREGFILE INTEGER gensubregnum
		{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.reg_nr = $2;
		  $$.subreg_nr = $3;
		}

gensubregnum:	DOT INTEGER
		{
		  $$ = $2;
		}
		|
		{
		  /* Default to subreg 0 if unspecified. */
		  $$ = 0;
		}
;

directmsgreg:	MSGREGFILE INTEGER msgsubregnum
		{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.reg_nr = $2;
		  $$.subreg_nr = $3;
		}
;

msgsubregnum:	gensubregnum
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
		  $$.vert_stride = $2;
		  $$.width = $4;
		  $$.horiz_stride = $6;
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
imm32:		INTEGER | NUMBER

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
		  $$ = ffs($2);
		}
;

/* 1.4.13: Instruction options */
/* XXX: this is a comma-separated list, really. */
instoptions:	LCURLY instoption RCURLY

/* XXX: fill me in. alignctrl, comprctrl, threadctrl, depctrl, maskctrl,
 * debugctrl, sendctrl
 */
instoption:	ALIGN1

%%
extern int yylineno;

void yyerror (char *msg)
{
	fprintf(stderr, "parse error \"%s\" at line %d, token \"%s\"\n",
		msg, yylineno, lex_text());
}

