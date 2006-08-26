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
	} direct_reg;
	double imm32;
}

%token SEMICOLON
%token LPAREN RPAREN
%token LANGLE RANGLE
%token LCURLY RCURLY
%token COMMA
%token DOT
%token MINUS ABS

%token <integer> TYPE_UD, TYPE_D, TYPE_UW, TYPE_W, TYPE_UB, TYPE_B,
%token <integer> TYPE_VF, TYPE_HF, TYPE_V, TYPE_F

%token ALIGN1 ALIGN16 SECHALF COMPR SWITCH ATOMIC NODDCHK NODDCLR
%token MASK_DISABLE BREAKPOINT EOT

%token <integer> GENREG MSGREG ADDRESSREG ACCREG FLAGREG
%token <integer> MASKREG AMASK IMASK LMASK CMASK
%token <integer> MASKSTACKREG LMS IMS MASKSTACKDEPTHREG IMSD LMSD
%token <integer> NOTIFYREG STATEREG CONTROLREG IPREG

%token <integer> MOV FRC RNDU RNDD RNDE RNDZ NOT LZD
%token <integer> MUL MAC MACH LINE SAD2 SADA2 DP4 DPH DP3 DP2
%token <integer> AVG ADD SEL AND OR XOR SHR SHL ASR CMP CMPN
%token <integer> SEND NOP JMPI IF IFF WHILE SEND ELSE BREAK CONT HALT MSAVE
%token <integer> PUSH MREST POP WAIT DO ENDIF ILLEGAL

%token NULL_TOKEN MATH SAMPLER GATEWAY READ WRITE URB THREAD_SPAWNER

%token MSGLEN RETURNLEN
%token <integer> ALLOCATE USED COMPLETE TRANSPOSE INTERLEAVE
%token SATURATE

%token <integer> INTEGER
%token <number> NUMBER

%token <integer> INV LOG EXP SQRT RSQ POW SIN COS SINCOS INTDIV INTMOD
%token <integer> INTDIVMOD
%token SIGNED SCALAR

%type <instruction> instruction unaryinstruction binaryinstruction
%type <instruction> binaryaccinstruction triinstruction sendinstruction
%type <instruction> specialinstruction
%type <instruction> dst dstoperand dstoperandex dstreg
%type <instruction> directsrcaccoperand srcarchoperandex src directsrcoperand
%type <instruction> srcimm imm32reg
%type <instruction> srcacc srcaccimm payload post_dst msgtarget
%type <instruction> instoptions instoption_list
%type <program> instrseq
%type <integer> instoption
%type <integer> unaryop binaryop binaryaccop
%type <integer> conditionalmodifier saturate negate abs
%type <integer> regtype srcimmtype execsize dstregion
%type <integer> subregnum sampler_datatype
%type <integer> urb_swizzle urb_allocate urb_used urb_complete
%type <integer> math_function math_signed math_scalar
%type <region> region
%type <direct_reg> directgenreg directmsgreg addrreg accreg flagreg maskreg
%type <direct_reg> maskstackreg maskstackdepthreg notifyreg
%type <direct_reg> statereg controlreg ipreg nullreg
%type <direct_reg> dstoperandex_typed srcarchoperandex_typed
%type <integer> mask_subreg maskstack_subreg maskstackdepth_subreg
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
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  $$.header.destreg__conditionalmod = $3;
		  $$.header.saturate = $4;
		  $$.header.execution_size = $5;
		  set_instruction_dest(&$$, &$6);
		  set_instruction_src0(&$$, &$7);
		  set_instruction_options(&$$, &$8);
		}
;

unaryop:	MOV | FRC | RNDU | RNDD | RNDE | RNDZ | NOT | LZD
;

binaryinstruction:
		predicate binaryop conditionalmodifier saturate execsize
		dst src srcimm instoptions
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  $$.header.destreg__conditionalmod = $3;
		  $$.header.saturate = $4;
		  $$.header.execution_size = $5;
		  set_instruction_dest(&$$, &$6);
		  set_instruction_src0(&$$, &$7);
		  set_instruction_src1(&$$, &$8);
		  set_instruction_options(&$$, &$9);
		}
;

binaryop:	MUL | MAC | MACH | LINE | SAD2 | SADA2 | DP4 | DPH | DP3 | DP2

binaryaccinstruction:
		predicate binaryaccop conditionalmodifier saturate execsize
		dst srcacc srcimm instoptions
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  $$.header.destreg__conditionalmod = $3;
		  $$.header.saturate = $4;
		  $$.header.execution_size = $5;
		  set_instruction_dest(&$$, &$6);
		  set_instruction_src0(&$$, &$7);
		  set_instruction_src1(&$$, &$8);
		  set_instruction_options(&$$, &$9);
		}
;

binaryaccop:	AVG | ADD | SEL | AND | OR | XOR | SHR | SHL | ASR | CMP | CMPN
;

triinstruction:	sendinstruction

sendinstruction: predicate SEND execsize INTEGER post_dst payload msgtarget
		MSGLEN INTEGER RETURNLEN INTEGER instoptions
		{
		  /* Send instructions are messy.  The first argument is the
		   * post destination -- the grf register that the response
		   * starts from.  The second argument is the current
		   * destination, which is the start of the message arguments
		   * to the shared function, and where src0 payload is loaded
		   * to if not null.  The payload is typically based on the
		   * grf 0 thread payload of your current thread, and is
		   * implicitly loaded if non-null.
		   */
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  $$.header.execution_size = $3;
		  $$.header.destreg__conditionalmod = $4; /* msg reg index */
		  set_instruction_dest(&$$, &$5);
		  set_instruction_src0(&$$, &$6);
		  $$.bits1.da1.src1_reg_file = BRW_IMMEDIATE_VALUE;
		  $$.bits1.da1.src1_reg_type = BRW_REGISTER_TYPE_D;
		  $$.bits3.generic = $7.bits3.generic;
		  $$.bits3.generic.msg_length = $9;
		  $$.bits3.generic.response_length = $11;
		  $$.bits3.generic.end_of_thread =
		    $12.bits3.generic.end_of_thread;
		}

branchloopop:	IF | IFF | WHILE
;

breakop:	BREAK | CONT | WAIT

maskpushop:	MSAVE | PUSH
;

specialinstruction: NOP
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $1;
		}

/* XXX! */
payload: directsrcoperand
;

post_dst:	dst
;

msgtarget:	NULL_TOKEN
		{
		  $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_NULL;
		}
		| SAMPLER LPAREN INTEGER COMMA INTEGER COMMA
		sampler_datatype RPAREN
		{
		  $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_SAMPLER;
		  $$.bits3.sampler.binding_table_index = $3;
		  $$.bits3.sampler.sampler = $5;
		  switch ($7) {
		  case TYPE_F:
		    $$.bits3.sampler.return_format =
		      BRW_SAMPLER_RETURN_FORMAT_FLOAT32;
		    break;
		  case TYPE_UD:
		    $$.bits3.sampler.return_format =
		      BRW_SAMPLER_RETURN_FORMAT_UINT32;
		    break;
		  case TYPE_D:
		    $$.bits3.sampler.return_format =
		      BRW_SAMPLER_RETURN_FORMAT_SINT32;
		    break;
		  }
		}
		| MATH math_function saturate math_signed math_scalar
		{
		  $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_MATH;
		  $$.bits3.math.function = $2;
		  if ($3 == BRW_INSTRUCTION_SATURATE)
		    $$.bits3.math.saturate = 1;
		  else
		    $$.bits3.math.saturate = 0;
		  $$.bits3.math.int_type = $4;
		  $$.bits3.math.precision = BRW_MATH_PRECISION_FULL;
		  $$.bits3.math.data_type = $5;
		}
		| GATEWAY
		{
		  $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_GATEWAY;
		}
		| READ
		{
		  $$.bits3.generic.msg_target =
		    BRW_MESSAGE_TARGET_DATAPORT_READ;
		}
		| WRITE LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA
		INTEGER RPAREN
		{
		  $$.bits3.generic.msg_target =
		    BRW_MESSAGE_TARGET_DATAPORT_WRITE;
		  $$.bits3.dp_write.binding_table_index = $3;
		  /* The msg control field of brw_struct.h is split into
		   * msg control and pixel_scoreboard_clear, even though
		   * pixel_scoreboard_clear isn't common to all write messages.
		   */
		  $$.bits3.dp_write.pixel_scoreboard_clear = ($5 & 0x8) >> 3;
		  $$.bits3.dp_write.msg_control = $5 & 0x7;
		  $$.bits3.dp_write.msg_type = $7;
		  $$.bits3.dp_write.send_commit_msg = $9;
		}
		| URB INTEGER urb_swizzle urb_allocate urb_used urb_complete
		{
		  $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_URB;
		  $$.bits3.urb.opcode = BRW_URB_OPCODE_WRITE;
		  $$.bits3.urb.offset = $2;
		  $$.bits3.urb.swizzle_control = $3;
		  $$.bits3.urb.pad = 0;
		  $$.bits3.urb.allocate = $4;
		  $$.bits3.urb.used = $5;
		  $$.bits3.urb.complete = $6;
		}
		| THREAD_SPAWNER
		{
		  $$.bits3.generic.msg_target =
		    BRW_MESSAGE_TARGET_THREAD_SPAWNER;
		}
;

urb_allocate:	ALLOCATE { $$ = 1; }
		| /* empty */ { $$ = 0; }
;

urb_used:	USED { $$ = 1; }
		| /* empty */ { $$ = 0; }
;

urb_complete:	COMPLETE { $$ = 1; }
		| /* empty */ { $$ = 0; }
;

urb_swizzle:	TRANSPOSE { $$ = BRW_URB_SWIZZLE_TRANSPOSE; }
		| INTERLEAVE { $$ = BRW_URB_SWIZZLE_INTERLEAVE; }
		| /* empty */ { $$ = BRW_URB_SWIZZLE_NONE; }
;

sampler_datatype:
		TYPE_F
		| TYPE_UD
		| TYPE_D
;

math_function:	INV | LOG | EXP | SQRT | POW | SIN | COS | SINCOS | INTDIV
		| INTMOD | INTDIVMOD
;

math_signed:	/* empty */ { $$ = 0; }
		| SIGNED { $$ = 1; }

math_scalar:	/* empty */ { $$ = 0; }
		| SCALAR { $$ = 1; }

/* 1.4.2: Destination register */

dst:		dstoperand | dstoperandex
;

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

/* The dstoperandex returns an instruction with just the destination register
 * filled in.
 */
dstoperandex:	dstoperandex_typed dstregion regtype
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		  $$.bits1.da1.dest_horiz_stride = $2;
		  $$.bits1.da1.dest_reg_type = $3;
		}
		| maskstackreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		  $$.bits1.da1.dest_horiz_stride = 1;
		  $$.bits1.da1.dest_reg_type = BRW_REGISTER_TYPE_UW;
		}
		| controlreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		  $$.bits1.da1.dest_horiz_stride = 1;
		  $$.bits1.da1.dest_reg_type = BRW_REGISTER_TYPE_UD;
		}
		| ipreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		  $$.bits1.da1.dest_horiz_stride = 1;
		  $$.bits1.da1.dest_reg_type = BRW_REGISTER_TYPE_UD;
		}
		| nullreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		  $$.bits1.da1.dest_horiz_stride = 1;
		  $$.bits1.da1.dest_reg_type = BRW_REGISTER_TYPE_F;
		}
;

dstoperandex_typed: accreg | flagreg | addrreg | maskreg
;

/* XXX: indirectgenreg, directmsgreg, indirectmsgreg */
dstreg:		directgenreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		}
		| directmsgreg
		{
		  $$.bits1.da1.dest_reg_file = $1.reg_file;
		  $$.bits1.da1.dest_reg_nr = $1.reg_nr;
		  $$.bits1.da1.dest_subreg_nr = $1.subreg_nr;
		}
;

/* 1.4.3: Source register */
srcaccimm:	srcacc | imm32reg
;

/* XXX: indirectsrcaccoperand */
srcacc:		directsrcaccoperand
;

srcimm:		directsrcoperand | imm32reg

imm32reg:	imm32 srcimmtype
		{
		  $$.bits1.da1.src0_reg_file = BRW_IMMEDIATE_VALUE;
		  $$.bits1.da1.src0_reg_type = $2;
		  switch ($2) {
		  case BRW_REGISTER_TYPE_UD:
		    $$.bits3.ud = $1;
		    break;
		  case BRW_REGISTER_TYPE_D:
		    $$.bits3.id = $1;
		    break;
		  case BRW_REGISTER_TYPE_UW:
		    $$.bits3.ud = $1;
		    break;
		  case BRW_REGISTER_TYPE_W:
		    $$.bits3.id = $1;
		    break;
		  case BRW_REGISTER_TYPE_UB:
		    $$.bits3.ud = $1;
		    /* There is no native byte immediate type */
		    $$.bits1.da1.src0_reg_type = BRW_REGISTER_TYPE_UD;
		    break;
		  case BRW_REGISTER_TYPE_B:
		    $$.bits3.id = $1;
		    /* There is no native byte immediate type */
		    $$.bits1.da1.src0_reg_type = BRW_REGISTER_TYPE_D;
		    break;
		  case BRW_REGISTER_TYPE_F:
		    $$.bits3.fd = $1;
		    break;
		  default:
		    fprintf(stderr, "unknown immediate type %d\n", $2);
		    YYERROR;
		  }
		}
;

/* XXX: accreg regtype */
directsrcaccoperand:	directsrcoperand
;

/* Returns a source operand in the src0 fields of an instruction. */
srcarchoperandex: srcarchoperandex_typed region regtype
		{
		  $$.bits1.da1.src0_reg_file = $1.reg_file;
		  $$.bits1.da1.src0_reg_type = $3;
		  $$.bits2.da1.src0_subreg_nr = $1.subreg_nr;
		  $$.bits2.da1.src0_reg_nr = $1.reg_nr;
		  $$.bits2.da1.src0_vert_stride = $2.vert_stride;
		  $$.bits2.da1.src0_width = $2.width;
		  $$.bits2.da1.src0_horiz_stride = $2.horiz_stride;
		  $$.bits2.da1.src0_negate = 0;
		  $$.bits2.da1.src0_abs = 0;
		}
		| maskstackreg
		{
		  set_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UB);
		}
		| controlreg
		{
		  set_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| statereg
		{
		  set_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| notifyreg
		{
		  set_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| ipreg
		{
		  set_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| nullreg
		{
		  set_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
;

srcarchoperandex_typed: flagreg | addrreg | maskreg
;

/* XXX: indirectsrcoperand */
src:		directsrcoperand
;

directsrcoperand:
		negate abs directgenreg region regtype
		{
		  /* Returns a source operand in the src0 fields of an
		   * instruction.
		   */
		  $$.bits1.da1.src0_reg_file = $3.reg_file;
		  $$.bits1.da1.src0_reg_type = $5;
		  $$.bits2.da1.src0_subreg_nr = $3.subreg_nr;
		  $$.bits2.da1.src0_reg_nr = $3.reg_nr;
		  $$.bits2.da1.src0_vert_stride = $4.vert_stride;
		  $$.bits2.da1.src0_width = $4.width;
		  $$.bits2.da1.src0_horiz_stride = $4.horiz_stride;
		  $$.bits2.da1.src0_negate = $1;
		  $$.bits2.da1.src0_abs = $2;
		}
		| srcarchoperandex
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
directgenreg:	GENREG subregnum
		{
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.reg_nr = $1;
		  $$.subreg_nr = $2;
		}

directmsgreg:	MSGREG subregnum
		{
		  $$.reg_file = BRW_MESSAGE_REGISTER_FILE;
		  $$.reg_nr = $1;
		  $$.subreg_nr = $2;
		}
;

addrreg:	ADDRESSREG subregnum
		{
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_ADDRESS | $1;
		  $$.subreg_nr = $2;
		}
;

accreg:		ACCREG subregnum
		{
		  if ($1 > 1) {
		    fprintf(stderr,
			    "accumulator register number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_ACCUMULATOR | $1;
		  $$.subreg_nr = $2;
		}
;

flagreg:	FLAGREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "flag register number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_FLAG | $1;
		  $$.subreg_nr = $2;
		}
;

maskreg:	MASKREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "mask register number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK;
		  $$.subreg_nr = $2;
		}
		| mask_subreg
		{
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK;
		  $$.subreg_nr = $1;
		}
;

mask_subreg:	AMASK | IMASK | LMASK | CMASK
;

maskstackreg:	MASKSTACKREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "mask stack register number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK;
		  $$.subreg_nr = $2;
		}
		| maskstack_subreg
		{
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK;
		  $$.subreg_nr = $1;
		}
;

maskstack_subreg: IMS | LMS
;

maskstackdepthreg: MASKSTACKDEPTHREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "mask stack register number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK_DEPTH;
		  $$.subreg_nr = $2;
		}
		| maskstackdepth_subreg
		{
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK_DEPTH;
		  $$.subreg_nr = $1;
		}
;

maskstackdepth_subreg: IMSD | LMSD
;

notifyreg:	NOTIFYREG
		{
		  if ($1 > 1) {
		    fprintf(stderr,
			    "notification register number %d out of range",
			    $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_NOTIFICATION_COUNT;
		  $$.subreg_nr = 0;
		}
;

statereg:	STATEREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "state register number %d out of range", $1);
		    YYERROR;
		  }
		  if ($2 > 1) {
		    fprintf(stderr,
			    "state subregister number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_STATE | $1;
		  $$.subreg_nr = $2;
		}
;

controlreg:	CONTROLREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "control register number %d out of range", $1);
		    YYERROR;
		  }
		  if ($2 > 2) {
		    fprintf(stderr,
			    "control subregister number %d out of range", $1);
		    YYERROR;
		  }
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_CONTROL | $1;
		  $$.subreg_nr = $2;
		}
;

ipreg:		IPREG
		{
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_IP;
		  $$.subreg_nr = 0;
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
		| TYPE_W { $$ = BRW_REGISTER_TYPE_W; }
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
;

negate:		/* empty */ { $$ = 0; }
		| MINUS { $$ = 1; }

abs:		/* empty */ { $$ = 0; }
		| ABS { $$ = 1; }

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

conditionalmodifier: { $$ = 0; }
;

/* 1.4.13: Instruction options */
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
		  case SECHALF:
		    $$.header.compression_control |= BRW_COMPRESSION_2NDHALF;
		    break;
		  case COMPR:
		    $$.header.compression_control |=
		      BRW_COMPRESSION_COMPRESSED;
		    break;
		  case SWITCH:
		    $$.header.thread_control |= BRW_THREAD_SWITCH;
		    break;
		  case ATOMIC:
		    $$.header.thread_control |= BRW_THREAD_ATOMIC;
		    break;
		  case NODDCHK:
		    $$.header.dependency_control |= BRW_DEPENDENCY_NOTCHECKED;
		    break;
		  case NODDCLR:
		    $$.header.dependency_control |= BRW_DEPENDENCY_NOTCLEARED;
		    break;
		  case MASK_DISABLE:
		    $$.header.mask_control = BRW_MASK_DISABLE;
		    break;
		  case BREAKPOINT:
		    $$.header.debug_control = BRW_DEBUG_BREAKPOINT;
		    break;
		  case EOT:
		    /* XXX: EOT shouldn't be an instoption, I don't think */
		    $$.bits3.generic.end_of_thread = 1;
		    break;
		  }
		}
		| /* empty, header defaults to zeroes. */
		{
		  bzero(&$$, sizeof($$));
		}
;

instoption:	ALIGN1 { $$ = ALIGN1; }
		| ALIGN16 { $$ = ALIGN16; }
		| SECHALF { $$ = SECHALF; }
		| COMPR { $$ = COMPR; }
		| SWITCH { $$ = SWITCH; }
		| ATOMIC { $$ = ATOMIC; }
		| NODDCHK { $$ = NODDCHK; }
		| NODDCLR { $$ = NODDCLR; }
		| MASK_DISABLE { $$ = MASK_DISABLE; }
		| BREAKPOINT { $$ = BREAKPOINT; }
		| EOT { $$ = EOT; }
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
		instr->bits2.da1.src0_negate = src->bits2.da1.src0_negate;
		instr->bits2.da1.src0_abs = src->bits2.da1.src0_abs;
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
		instr->bits3.da1.src1_negate = src->bits2.da1.src0_negate;
		instr->bits3.da1.src1_abs = src->bits2.da1.src0_abs;
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

void set_src_operand(struct brw_instruction *instr, struct gen_reg *reg,
		     int type)
{
	instr->bits1.da1.src0_reg_file = reg->reg_file;
	instr->bits1.da1.src0_reg_type = type;
	instr->bits2.da1.src0_subreg_nr = reg->subreg_nr;
	instr->bits2.da1.src0_reg_nr = reg->reg_nr;
	instr->bits2.da1.src0_vert_stride = 0;
	instr->bits2.da1.src0_width = 0;
	instr->bits2.da1.src0_horiz_stride = 1;
	instr->bits2.da1.src0_negate = 0;
	instr->bits2.da1.src0_abs = 0;
}
