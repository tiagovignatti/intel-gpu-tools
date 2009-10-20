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
#include <stdlib.h>
#include "gen4asm.h"
#include "brw_defines.h"

extern long int gen_level;

int set_instruction_dest(struct brw_instruction *instr,
			 struct dst_operand *dest);
int set_instruction_src0(struct brw_instruction *instr,
			 struct src_operand *src);
int set_instruction_src1(struct brw_instruction *instr,
			 struct src_operand *src);
void set_instruction_options(struct brw_instruction *instr,
			     struct brw_instruction *options);
void set_instruction_predicate(struct brw_instruction *instr,
			       struct brw_instruction *predicate);
void set_instruction_predicate(struct brw_instruction *instr,
			       struct brw_instruction *predicate);
void set_direct_dst_operand(struct dst_operand *dst, struct direct_reg *reg,
			    int type);
void set_direct_src_operand(struct src_operand *src, struct direct_reg *reg,
			    int type);

%}

%start ROOT

%union {
	char *string;
	int integer;
	double number;
	struct brw_instruction instruction;
	struct brw_program program;
	struct region {
		int vert_stride, width, horiz_stride;
	} region;
	struct direct_reg direct_reg;
	struct indirect_reg indirect_reg;

	imm32_t imm32;

	struct dst_operand dst_operand;
	struct src_operand src_operand;
}

%token COLON
%token SEMICOLON
%token LPAREN RPAREN
%token LANGLE RANGLE
%token LCURLY RCURLY
%token LSQUARE RSQUARE
%token COMMA
%token DOT ABS
%left  PLUS MINUS 
%left  MULTIPLY DIVIDE

%token <integer> TYPE_UD TYPE_D TYPE_UW TYPE_W TYPE_UB TYPE_B
%token <integer> TYPE_VF TYPE_HF TYPE_V TYPE_F

%token ALIGN1 ALIGN16 SECHALF COMPR SWITCH ATOMIC NODDCHK NODDCLR
%token MASK_DISABLE BREAKPOINT EOT

%token SEQ ANY2H ALL2H ANY4H ALL4H ANY8H ALL8H ANY16H ALL16H ANYV ALLV
%token <integer> ZERO EQUAL NOT_ZERO NOT_EQUAL GREATER GREATER_EQUAL LESS LESS_EQUAL
%token <integer> ROUND_INCREMENT OVERFLOW UNORDERED
%token <integer> GENREG MSGREG ADDRESSREG ACCREG FLAGREG
%token <integer> MASKREG AMASK IMASK LMASK CMASK
%token <integer> MASKSTACKREG LMS IMS MASKSTACKDEPTHREG IMSD LMSD
%token <integer> NOTIFYREG STATEREG CONTROLREG IPREG
%token GENREGFILE MSGREGFILE

%token <integer> MOV FRC RNDU RNDD RNDE RNDZ NOT LZD
%token <integer> MUL MAC MACH LINE SAD2 SADA2 DP4 DPH DP3 DP2
%token <integer> AVG ADD SEL AND OR XOR SHR SHL ASR CMP CMPN
%token <integer> SEND NOP JMPI IF IFF WHILE ELSE BREAK CONT HALT MSAVE
%token <integer> PUSH MREST POP WAIT DO ENDIF ILLEGAL

%token NULL_TOKEN MATH SAMPLER GATEWAY READ WRITE URB THREAD_SPAWNER

%token MSGLEN RETURNLEN
%token <integer> ALLOCATE USED COMPLETE TRANSPOSE INTERLEAVE
%token SATURATE

%token <integer> INTEGER
%token <string> STRING
%token <number> NUMBER

%token <integer> INV LOG EXP SQRT RSQ POW SIN COS SINCOS INTDIV INTMOD
%token <integer> INTDIVMOD
%token SIGNED SCALAR

%token <integer> X Y Z W

%type <integer> exp
%type <instruction> instruction unaryinstruction binaryinstruction
%type <instruction> binaryaccinstruction triinstruction sendinstruction
%type <instruction> jumpinstruction branchloopinstruction elseinstruction
%type <instruction> breakinstruction syncinstruction specialinstruction
%type <instruction> msgtarget
%type <instruction> instoptions instoption_list predicate
%type <string> label
%type <program> instrseq
%type <integer> instoption
%type <integer> unaryop binaryop binaryaccop branchloopop breakop
%type <integer> conditionalmodifier saturate negate abs chansel
%type <integer> writemask_x writemask_y writemask_z writemask_w
%type <integer> regtype srcimmtype execsize dstregion immaddroffset
%type <integer> subregnum sampler_datatype
%type <integer> urb_swizzle urb_allocate urb_used urb_complete
%type <integer> math_function math_signed math_scalar
%type <integer> predctrl predstate
%type <region> region region_wh indirectregion
%type <direct_reg> directgenreg directmsgreg addrreg accreg flagreg maskreg
%type <direct_reg> maskstackreg notifyreg
/* %type <direct_reg>  maskstackdepthreg */
%type <direct_reg> statereg controlreg ipreg nullreg
%type <direct_reg> dstoperandex_typed srcarchoperandex_typed
%type <indirect_reg> indirectgenreg indirectmsgreg addrparam
%type <integer> mask_subreg maskstack_subreg 
/* %type <intger> maskstackdepth_subreg */
%type <imm32> imm32
%type <dst_operand> dst dstoperand dstoperandex dstreg post_dst writemask
%type <src_operand> directsrcoperand srcarchoperandex directsrcaccoperand
%type <src_operand> indirectsrcoperand
%type <src_operand> src srcimm imm32reg payload srcacc srcaccimm swizzle
%type <src_operand> relativelocation relativelocation2 locationstackcontrol

%%
exp:		INTEGER { $$ = $1; }
		| exp MULTIPLY exp { $$ = $1 * $3; } 
		| exp DIVIDE exp { if ($3) $$ = $1 / $3; else YYERROR;}
		| exp PLUS exp { $$ = $1 + $3; }
		| exp MINUS exp { $$ = $1 - $3; }
		| MINUS exp { $$ = -$2;}
		| LPAREN exp RPAREN { $$ = $2; }

ROOT:		instrseq
		{
		  compiled_program = $1;
		}
;


label:          STRING COLON
		{
    		  $$ = $1;
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
                | label instrseq
                {
                  struct brw_program_instruction *list_entry =
                    calloc(sizeof(struct brw_program_instruction), 1);
                  list_entry->string = $1;
                  list_entry->islabel = 1;
                  list_entry->next = $2.first;
                      $2.first = list_entry;
                      $$ = $2;
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
		| jumpinstruction
		| branchloopinstruction
		| elseinstruction
		| breakinstruction
		| syncinstruction
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
		  set_instruction_options(&$$, &$8);
		  set_instruction_predicate(&$$, &$1);
		  if (set_instruction_dest(&$$, &$6) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$7) != 0)
		    YYERROR;
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
		  set_instruction_options(&$$, &$9);
		  set_instruction_predicate(&$$, &$1);
		  if (set_instruction_dest(&$$, &$6) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$7) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$8) != 0)
		    YYERROR;
		}
;

binaryop:	MUL | MAC | MACH | LINE | SAD2 | SADA2 | DP4 | DPH | DP3 | DP2
;

binaryaccinstruction:
		predicate binaryaccop conditionalmodifier saturate execsize
		dst srcacc srcimm instoptions
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  $$.header.destreg__conditionalmod = $3;
		  $$.header.saturate = $4;
		  $$.header.execution_size = $5;
		  set_instruction_options(&$$, &$9);
		  set_instruction_predicate(&$$, &$1);
		  if (set_instruction_dest(&$$, &$6) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$7) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$8) != 0)
		    YYERROR;
		}
;

binaryaccop:	AVG | ADD | SEL | AND | OR | XOR | SHR | SHL | ASR | CMP | CMPN
;

triinstruction:	sendinstruction
;

sendinstruction: predicate SEND execsize exp post_dst payload msgtarget
		MSGLEN exp RETURNLEN exp instoptions
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
		  set_instruction_predicate(&$$, &$1);
		  if (set_instruction_dest(&$$, &$5) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$6) != 0)
		    YYERROR;
		  $$.bits1.da1.src1_reg_file = BRW_IMMEDIATE_VALUE;
		  $$.bits1.da1.src1_reg_type = BRW_REGISTER_TYPE_D;

		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = $7.bits2.send_gen5.sfid;
                      $$.bits2.send_gen5.end_of_thread = $12.bits3.generic_gen5.end_of_thread;
                      $$.bits3.generic_gen5 = $7.bits3.generic_gen5;
                      $$.bits3.generic_gen5.msg_length = $9;
                      $$.bits3.generic_gen5.response_length = $11;
                      $$.bits3.generic_gen5.end_of_thread =
                          $12.bits3.generic_gen5.end_of_thread;
		  } else {
                      $$.bits3.generic = $7.bits3.generic;
                      $$.bits3.generic.msg_length = $9;
                      $$.bits3.generic.response_length = $11;
                      $$.bits3.generic.end_of_thread =
                          $12.bits3.generic.end_of_thread;
		  }
		}
;

jumpinstruction: predicate JMPI relativelocation2
		{
		  struct direct_reg dst;
		  struct dst_operand ip_dst;
		  struct src_operand ip_src;

		  /* The jump instruction requires that the IP register
		   * be the destination and first source operand, while the
		   * offset is the second source operand.  The next instruction
		   * is the post-incremented IP plus the offset.
		   */
		  dst.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  dst.reg_nr = BRW_ARF_IP;
		  dst.subreg_nr = 0;

		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  set_direct_dst_operand(&ip_dst, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_dest(&$$, &ip_dst);
		  set_direct_src_operand(&ip_src, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_src0(&$$, &ip_src);
		  set_instruction_src1(&$$, &$3);
		}
		| predicate JMPI STRING
		{
		    struct direct_reg dst;
		    struct dst_operand ip_dst;
		    struct src_operand ip_src;
		    struct src_operand imm;
		    
		    /* The jump instruction requires that the IP register
	 	     * be the destination and first source operand, while the
	             * offset is the second source operand.  The next instruction
		      is the post-incremented IP plus the offset.
		     */
		    dst.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		    dst.reg_nr = BRW_ARF_IP;
		    dst.subreg_nr = 0;
		    memset (&imm, '\0', sizeof (imm));
		    imm.reg_file = BRW_IMMEDIATE_VALUE;
		    imm.reg_type = BRW_REGISTER_TYPE_D;
		    imm.imm32 = 0;
		    
		    bzero(&$$, sizeof($$));
		    $$.header.opcode = $2;
		    set_direct_dst_operand(&ip_dst, &dst, BRW_REGISTER_TYPE_UD);
		    set_instruction_dest(&$$, &ip_dst);
		    set_instruction_predicate(&$$, &$1);
		    set_direct_src_operand(&ip_src, &dst, BRW_REGISTER_TYPE_UD);
		    set_instruction_src0(&$$, &ip_src);
		    set_instruction_src1(&$$, &imm);
		    $$.reloc_target = $3;
		}
;

branchloopinstruction:
		predicate branchloopop relativelocation
		{
		  struct direct_reg dst;
		  struct dst_operand ip_dst;
		  struct src_operand ip_src;

		  /* The branch instructions require that the IP register
		   * be the destination and first source operand, while the
		   * offset is the second source operand.  The offset is added
		   * to the pre-incremented IP.
		   */
		  dst.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  dst.reg_nr = BRW_ARF_IP;
		  dst.subreg_nr = 0;

		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  set_instruction_predicate(&$$, &$1);
		  set_direct_dst_operand(&ip_dst, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_dest(&$$, &ip_dst);
		  set_direct_src_operand(&ip_src, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_src0(&$$, &ip_src);
		  set_instruction_src1(&$$, &$3);
		}
;

branchloopop:	IF | IFF | WHILE
;

elseinstruction: ELSE relativelocation
		{
		  struct direct_reg dst;
		  struct dst_operand ip_dst;
		  struct src_operand ip_src;

		  /* The jump instruction requires that the IP register
		   * be the destination and first source operand, while the
		   * offset is the second source operand.  The offset is added
		   * to the IP pre-increment.
		   */
		  dst.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  dst.reg_nr = BRW_ARF_IP;
		  dst.subreg_nr = 0;

		  /* Set the istack pop count, which must always be 1. */
		  $2.imm32 |= (1 << 16);

		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $1;
		  set_direct_dst_operand(&ip_dst, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_dest(&$$, &ip_dst);
		  set_direct_src_operand(&ip_src, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_src0(&$$, &ip_src);
		  set_instruction_src1(&$$, &$2);
		}
;

breakinstruction: breakop locationstackcontrol
		{
		  struct direct_reg dst;
		  struct dst_operand ip_dst;
		  struct src_operand ip_src;

		  /* The jump instruction requires that the IP register
		   * be the destination and first source operand, while the
		   * offset is the second source operand.  The offset is added
		   * to the IP pre-increment.
		   */
		  dst.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  dst.reg_nr = BRW_ARF_IP;
		  dst.subreg_nr = 0;

		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $1;
		  set_direct_dst_operand(&ip_dst, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_dest(&$$, &ip_dst);
		  set_direct_src_operand(&ip_src, &dst, BRW_REGISTER_TYPE_UD);
		  set_instruction_src0(&$$, &ip_src);
		  set_instruction_src1(&$$, &$2);
		}
;

breakop:	BREAK | CONT | HALT
;

/*
maskpushop:	MSAVE | PUSH
;
 */

syncinstruction: predicate WAIT notifyreg
		{
		  struct direct_reg null;
		  struct dst_operand null_dst;
		  struct src_operand null_src;
		  struct src_operand notify_src;

		  null.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  null.reg_nr = BRW_ARF_NULL;
		  null.subreg_nr = 0;
		  
		  notify_src.reg_file = $3.reg_file;
		  notify_src.reg_nr = $3.reg_nr;
		  notify_src.subreg_nr = $3.subreg_nr;
		  notify_src.reg_type = BRW_REGISTER_TYPE_UD;

		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $2;
		  set_direct_dst_operand(&null_dst, &null, BRW_REGISTER_TYPE_UD);
		  set_instruction_dest(&$$, &null_dst);
		  set_direct_src_operand(&null_src, &null, BRW_REGISTER_TYPE_UD);
		  set_instruction_src0(&$$, &notify_src);
		  set_instruction_src1(&$$, &null_src);
		}
;

specialinstruction: NOP
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $1;
		}
		| DO
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $1;
		}
		| ENDIF
		{
		  bzero(&$$, sizeof($$));
		  $$.header.opcode = $1;
		  $$.bits1.da1.src1_reg_file = BRW_IMMEDIATE_VALUE;
		  $$.bits1.da1.src1_reg_type = BRW_REGISTER_TYPE_D;
		  $$.bits3.if_else.pop_count = 1;
		}
;

/* XXX! */
payload: directsrcoperand
;

post_dst:	dst
;

msgtarget:	NULL_TOKEN
		{
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid= BRW_MESSAGE_TARGET_NULL;
                      $$.bits3.generic_gen5.header_present = 0;  /* ??? */
		  } else {
                      $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_NULL;
		  }
		}
		| SAMPLER LPAREN INTEGER COMMA INTEGER COMMA
		sampler_datatype RPAREN
		{
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = BRW_MESSAGE_TARGET_SAMPLER;
                      $$.bits3.generic_gen5.header_present = 1;   /* ??? */
                      $$.bits3.sampler_gen5.binding_table_index = $3;
                      $$.bits3.sampler_gen5.sampler = $5;
                      $$.bits3.sampler_gen5.simd_mode = 2; /* SIMD16, maybe we should add a new parameter */
		  } else {
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
		}
		| MATH math_function saturate math_signed math_scalar
		{
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = BRW_MESSAGE_TARGET_MATH;
                      $$.bits3.generic_gen5.header_present = 0;
                      $$.bits3.math_gen5.function = $2;
                      if ($3 == BRW_INSTRUCTION_SATURATE)
                          $$.bits3.math_gen5.saturate = 1;
                      else
                          $$.bits3.math_gen5.saturate = 0;
                      $$.bits3.math_gen5.int_type = $4;
                      $$.bits3.math_gen5.precision = BRW_MATH_PRECISION_FULL;
                      $$.bits3.math_gen5.data_type = $5;
		  } else {
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
		}
		| GATEWAY
		{
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = BRW_MESSAGE_TARGET_GATEWAY;
                      $$.bits3.generic_gen5.header_present = 0;  /* ??? */
		  } else {
                      $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_GATEWAY;
		  }
		}
		| READ  LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA
                INTEGER RPAREN
		{
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = 
                          BRW_MESSAGE_TARGET_DATAPORT_READ;
                      $$.bits3.generic_gen5.header_present = 1;
                      $$.bits3.dp_read_gen5.binding_table_index = $3;
                      $$.bits3.dp_read_gen5.target_cache = $5;
                      $$.bits3.dp_read_gen5.msg_control = $7;
                      $$.bits3.dp_read_gen5.msg_type = $9;
		  } else {
                      $$.bits3.generic.msg_target =
                          BRW_MESSAGE_TARGET_DATAPORT_READ;
                      $$.bits3.dp_read.binding_table_index = $3;
                      $$.bits3.dp_read.target_cache = $5;
                      $$.bits3.dp_read.msg_control = $7;
                      $$.bits3.dp_read.msg_type = $9;
		  }
		}
		| WRITE LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA
		INTEGER RPAREN
		{
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid =
                          BRW_MESSAGE_TARGET_DATAPORT_WRITE;
                      $$.bits3.generic_gen5.header_present = 1;
                      $$.bits3.dp_write_gen5.binding_table_index = $3;
                      $$.bits3.dp_write_gen5.pixel_scoreboard_clear = ($5 & 0x8) >> 3;
                      $$.bits3.dp_write_gen5.msg_control = $5 & 0x7;
                      $$.bits3.dp_write_gen5.msg_type = $7;
                      $$.bits3.dp_write_gen5.send_commit_msg = $9;
		  } else {
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
		}
		| URB INTEGER urb_swizzle urb_allocate urb_used urb_complete
		{
		  $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_URB;
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = BRW_MESSAGE_TARGET_URB;
                      $$.bits3.generic_gen5.header_present = 1;
                      $$.bits3.urb_gen5.opcode = BRW_URB_OPCODE_WRITE;
                      $$.bits3.urb_gen5.offset = $2;
                      $$.bits3.urb_gen5.swizzle_control = $3;
                      $$.bits3.urb_gen5.pad = 0;
                      $$.bits3.urb_gen5.allocate = $4;
                      $$.bits3.urb_gen5.used = $5;
                      $$.bits3.urb_gen5.complete = $6;
		  } else {
                      $$.bits3.generic.msg_target = BRW_MESSAGE_TARGET_URB;
                      $$.bits3.urb.opcode = BRW_URB_OPCODE_WRITE;
                      $$.bits3.urb.offset = $2;
                      $$.bits3.urb.swizzle_control = $3;
                      $$.bits3.urb.pad = 0;
                      $$.bits3.urb.allocate = $4;
                      $$.bits3.urb.used = $5;
                      $$.bits3.urb.complete = $6;
		  }
		}
		| THREAD_SPAWNER  LPAREN INTEGER COMMA INTEGER COMMA
                        INTEGER RPAREN
		{
		  $$.bits3.generic.msg_target =
		    BRW_MESSAGE_TARGET_THREAD_SPAWNER;
		  if (gen_level == 5) {
                      $$.bits2.send_gen5.sfid = 
                          BRW_MESSAGE_TARGET_THREAD_SPAWNER;
                      $$.bits3.generic_gen5.header_present = 0;
                      $$.bits3.thread_spawner_gen5.opcode = $3;
                      $$.bits3.thread_spawner_gen5.requester_type  = $5;
                      $$.bits3.thread_spawner_gen5.resource_select = $7;
		  } else {
                      $$.bits3.generic.msg_target =
                          BRW_MESSAGE_TARGET_THREAD_SPAWNER;
                      $$.bits3.thread_spawner.opcode = $3;
                      $$.bits3.thread_spawner.requester_type  = $5;
                      $$.bits3.thread_spawner.resource_select = $7;
		  }
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
;

math_scalar:	/* empty */ { $$ = 0; }
		| SCALAR { $$ = 1; }
;

/* 1.4.2: Destination register */

dst:		dstoperand | dstoperandex
;

dstoperand:	dstreg dstregion writemask regtype
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.horiz_stride = $2;
		  $$.writemask_set = $3.writemask_set;
		  $$.writemask = $3.writemask;
		  $$.reg_type = $4;
		}
;

/* The dstoperandex returns an instruction with just the destination register
 * filled in.
 */
dstoperandex:	dstoperandex_typed dstregion regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.horiz_stride = $2;
		  $$.reg_type = $3;
		}
		| maskstackreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.horiz_stride = 1;
		  $$.reg_type = BRW_REGISTER_TYPE_UW;
		}
		| controlreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.horiz_stride = 1;
		  $$.reg_type = BRW_REGISTER_TYPE_UD;
		}
		| ipreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.horiz_stride = 1;
		  $$.reg_type = BRW_REGISTER_TYPE_UD;
		}
		| nullreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.horiz_stride = 1;
		  $$.reg_type = BRW_REGISTER_TYPE_F;
		}
;

dstoperandex_typed: accreg | flagreg | addrreg | maskreg
;

/* Returns a partially complete destination register consisting of the
 * direct or indirect register addressing fields, but not stride or writemask.
 */
dstreg:		directgenreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_mode = BRW_ADDRESS_DIRECT;
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		}
		| directmsgreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_mode = BRW_ADDRESS_DIRECT;
		  $$.reg_file = $1.reg_file;
		  $$.reg_nr = $1.reg_nr;
		  $$.subreg_nr = $1.subreg_nr;
		}
		| indirectgenreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		  $$.reg_file = $1.reg_file;
		  $$.address_subreg_nr = $1.address_subreg_nr;
		  $$.indirect_offset = $1.indirect_offset;
		}
		| indirectmsgreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		  $$.reg_file = $1.reg_file;
		  $$.address_subreg_nr = $1.address_subreg_nr;
		  $$.indirect_offset = $1.indirect_offset;
		}
;

/* 1.4.3: Source register */
srcaccimm:	srcacc | imm32reg
;

srcacc:		directsrcaccoperand | indirectsrcoperand
;

srcimm:		directsrcoperand | imm32reg
;

imm32reg:	imm32 srcimmtype
		{
		  union {
		    int i;
		    float f;
		  } intfloat;
		  uint32_t	d;

		  switch ($2) {
		  case BRW_REGISTER_TYPE_UD:
		  case BRW_REGISTER_TYPE_D:
		  case BRW_REGISTER_TYPE_V:
		    switch ($1.r) {
		    case imm32_d:
		      d = $1.u.d;
		      break;
		    default:
		      fprintf (stderr, "non-int D/UD/V representation\n");
		      YYERROR;
		    }
		    break;
		  case BRW_REGISTER_TYPE_UW:
		  case BRW_REGISTER_TYPE_W:
		    switch ($1.r) {
		    case imm32_d:
		      d = $1.u.d;
		      break;
		    default:
		      fprintf (stderr, "non-int W/UW representation\n");
		      YYERROR;
		    }
		    d &= 0xffff;
		    d |= d << 16;
		    break;
		  case BRW_REGISTER_TYPE_F:
		    switch ($1.r) {
		    case imm32_f:
		      intfloat.f = $1.u.f;
		      break;
		    case imm32_d:
		      intfloat.f = (float) $1.u.d;
		      break;
		    default:
		      fprintf (stderr, "non-float F representation\n");
		      YYERROR;
		    }
		    d = intfloat.i;
		    break;
		  case BRW_REGISTER_TYPE_VF:
		    fprintf (stderr, "Immediate type VF not supported yet\n");
		    YYERROR;
		  default:
		    fprintf(stderr, "unknown immediate type %d\n", $2);
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_IMMEDIATE_VALUE;
		  $$.reg_type = $2;
		  $$.imm32 = d;
		}
;

directsrcaccoperand:	directsrcoperand
		| accreg regtype
		{
		  set_direct_src_operand(&$$, &$1, $2);
		}
;

/* Returns a source operand in the src0 fields of an instruction. */
srcarchoperandex: srcarchoperandex_typed region regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = $1.reg_file;
		  $$.reg_type = $3;
		  $$.subreg_nr = $1.subreg_nr;
		  $$.reg_nr = $1.reg_nr;
		  $$.vert_stride = $2.vert_stride;
		  $$.width = $2.width;
		  $$.horiz_stride = $2.horiz_stride;
		  $$.negate = 0;
		  $$.abs = 0;
		}
		| maskstackreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UB);
		}
		| controlreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| statereg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| notifyreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| ipreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| nullreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
;

srcarchoperandex_typed: flagreg | addrreg | maskreg
;

src:		directsrcoperand | indirectsrcoperand
;

directsrcoperand:
		negate abs directgenreg region regtype swizzle
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_mode = BRW_ADDRESS_DIRECT;
		  $$.reg_file = $3.reg_file;
		  $$.reg_nr = $3.reg_nr;
		  $$.subreg_nr = $3.subreg_nr;
		  $$.reg_type = $5;
		  $$.vert_stride = $4.vert_stride;
		  $$.width = $4.width;
		  $$.horiz_stride = $4.horiz_stride;
		  $$.negate = $1;
		  $$.abs = $2;
		  $$.swizzle_set = $6.swizzle_set;
		  $$.swizzle_x = $6.swizzle_x;
		  $$.swizzle_y = $6.swizzle_y;
		  $$.swizzle_z = $6.swizzle_z;
		  $$.swizzle_w = $6.swizzle_w;
		}
		| srcarchoperandex
;

indirectsrcoperand:
		negate abs indirectgenreg indirectregion regtype swizzle
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		  $$.reg_file = $3.reg_file;
		  $$.address_subreg_nr = $3.address_subreg_nr;
		  $$.indirect_offset = $3.indirect_offset;
		  $$.reg_type = $5;
		  $$.vert_stride = $4.vert_stride;
		  $$.width = $4.width;
		  $$.horiz_stride = $4.horiz_stride;
		  $$.negate = $1;
		  $$.abs = $2;
		  $$.swizzle_set = $6.swizzle_set;
		  $$.swizzle_x = $6.swizzle_x;
		  $$.swizzle_y = $6.swizzle_y;
		  $$.swizzle_z = $6.swizzle_z;
		  $$.swizzle_w = $6.swizzle_w;
		}
;

/* 1.4.4: Address Registers */
/* Returns a partially-completed indirect_reg consisting of the address
 * register fields for register-indirect access.
 */
addrparam:	addrreg immaddroffset
		{
		  if ($2 < -512 || $2 > 511) {
		    fprintf(stderr, "Address immediate offset %d out of"
			    "range\n", $2);
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.address_subreg_nr = $1.subreg_nr;
		  $$.indirect_offset = $2;
		}
;

/* The immaddroffset provides an immediate offset value added to the addresses
 * from the address register in register-indirect register access.
 */
immaddroffset:	/* empty */ { $$ = 0; }
		| exp
;


/* 1.4.5: Register files and register numbers */
subregnum:	DOT exp
		{
		  $$ = $2;
		}
		|
		{
		  /* Default to subreg 0 if unspecified. */
		  $$ = 0;
		}
;

directgenreg:	GENREG subregnum
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.reg_nr = $1;
		  $$.subreg_nr = $2;
		}
;

indirectgenreg: GENREGFILE LSQUARE addrparam RSQUARE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_GENERAL_REGISTER_FILE;
		  $$.address_subreg_nr = $3.address_subreg_nr;
		  $$.indirect_offset = $3.indirect_offset;
		}
;

directmsgreg:	MSGREG subregnum
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_MESSAGE_REGISTER_FILE;
		  $$.reg_nr = $1;
		  $$.subreg_nr = $2;
		}
;

indirectmsgreg: MSGREGFILE LSQUARE addrparam RSQUARE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_MESSAGE_REGISTER_FILE;
		  $$.address_subreg_nr = $3.address_subreg_nr;
		  $$.indirect_offset = $3.indirect_offset;
		}
;

addrreg:	ADDRESSREG subregnum
		{
		  if ($1 != 0) {
		    fprintf(stderr,
			    "address register number %d out of range", $1);
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
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
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_ACCUMULATOR | $1;
		  $$.subreg_nr = $2;
		}
;

flagreg:	FLAGREG
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_FLAG | 0;
		  $$.subreg_nr = $1;
		}
;

maskreg:	MASKREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "mask register number %d out of range", $1);
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK;
		  $$.subreg_nr = $2;
		}
		| mask_subreg
		{
		  memset (&$$, '\0', sizeof ($$));
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
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK;
		  $$.subreg_nr = $2;
		}
		| maskstack_subreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK;
		  $$.subreg_nr = $1;
		}
;

maskstack_subreg: IMS | LMS
;

/*
maskstackdepthreg: MASKSTACKDEPTHREG subregnum
		{
		  if ($1 > 0) {
		    fprintf(stderr,
			    "mask stack register number %d out of range", $1);
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK_DEPTH;
		  $$.subreg_nr = $2;
		}
		| maskstackdepth_subreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_MASK_STACK_DEPTH;
		  $$.subreg_nr = $1;
		}
;

maskstackdepth_subreg: IMSD | LMSD
;
 */

notifyreg:	NOTIFYREG
		{
		  if ($1 > 1) {
		    fprintf(stderr,
			    "notification register number %d out of range",
			    $1);
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
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
		  memset (&$$, '\0', sizeof ($$));
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
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_CONTROL | $1;
		  $$.subreg_nr = $2;
		}
;

ipreg:		IPREG
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_IP;
		  $$.subreg_nr = 0;
		}
;

nullreg:	NULL_TOKEN
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.reg_nr = BRW_ARF_NULL;
		  $$.subreg_nr = 0;
		}
;

/* 1.4.6: Relative locations */
relativelocation: imm32
		{
		  if ($1.r != imm32_d) {
		    fprintf (stderr,
			     "error: non-int offset representation\n");
		    YYERROR;
		  }
		    
		  if ($1.u.d > 32767 || $1.u.d < -32768) {
		    fprintf(stderr,
			    "error: relative offset %d out of range\n", $1.u.d);
		    YYERROR;
		  }

		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_IMMEDIATE_VALUE;
		  $$.reg_type = BRW_REGISTER_TYPE_D;
		  $$.imm32 = $1.u.d & 0x0000ffff;
		}
;

relativelocation2:
		imm32
		{
		  if ($1.r != imm32_d) {
		    fprintf (stderr,
			     "error: non-int location representation\n");
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_IMMEDIATE_VALUE;
		  $$.reg_type = BRW_REGISTER_TYPE_D;
		  $$.imm32 = $1.u.d;
		}
		| directgenreg region regtype
		{
		  set_direct_src_operand(&$$, &$1, $3);
		  $$.vert_stride = $2.vert_stride;
		  $$.width = $2.width;
		  $$.horiz_stride = $2.horiz_stride;
		}
;

locationstackcontrol:
		imm32
		{
		  if ($1.r != imm32_d) {
		    fprintf (stderr,
			     "error: non-int stack control representation\n");
		    YYERROR;
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg_file = BRW_IMMEDIATE_VALUE;
		  $$.reg_type = BRW_REGISTER_TYPE_D;
		  $$.imm32 = $1.u.d;
		}
;

/* 1.4.7: Regions */
dstregion:	LANGLE exp RANGLE
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

region:		LANGLE exp COMMA exp COMMA exp RANGLE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.vert_stride = ffs($2);
		  $$.width = ffs($4) - 1;
		  $$.horiz_stride = ffs($6);
		}
;

/* region_wh is used in specifying indirect operands where rather than having
 * a vertical stride, you use subsequent address registers to get a new base
 * offset for the next row.
 */
region_wh:	LANGLE exp COMMA exp RANGLE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.vert_stride = BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL;
		  $$.width = ffs($2) - 1;
		  $$.horiz_stride = ffs($4);
		}
;

indirectregion:	region | region_wh
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
;

srcimmtype:	TYPE_F { $$ = BRW_REGISTER_TYPE_F; }
		| TYPE_UD { $$ = BRW_REGISTER_TYPE_UD; }
		| TYPE_D { $$ = BRW_REGISTER_TYPE_D; }
		| TYPE_UW { $$ = BRW_REGISTER_TYPE_UW; }
		| TYPE_W { $$ = BRW_REGISTER_TYPE_W; }
		| TYPE_V { $$ = BRW_REGISTER_TYPE_V; }
		| TYPE_VF { $$ = BRW_REGISTER_TYPE_VF; }
;

/* 1.4.10: Swizzle control */
/* Returns the swizzle control for an align16 instruction's source operand
 * in the src0 fields.
 */
swizzle:	/* empty */
		{
		  $$.swizzle_set = 0;
		  $$.swizzle_x = BRW_CHANNEL_X;
		  $$.swizzle_y = BRW_CHANNEL_Y;
		  $$.swizzle_z = BRW_CHANNEL_Z;
		  $$.swizzle_w = BRW_CHANNEL_W;
		}
		| DOT chansel
		{
		  $$.swizzle_set = 1;
		  $$.swizzle_x = $2;
		  $$.swizzle_y = $2;
		  $$.swizzle_z = $2;
		  $$.swizzle_w = $2;
		}
		| DOT chansel chansel chansel chansel
		{
		  $$.swizzle_set = 1;
		  $$.swizzle_x = $2;
		  $$.swizzle_y = $3;
		  $$.swizzle_z = $4;
		  $$.swizzle_w = $5;
		}
;

chansel:	X | Y | Z | W
;

/* 1.4.9: Write mask */
/* Returns a partially completed dst_operand, with just the writemask bits
 * filled out.
 */
writemask:	/* empty */
		{
		  $$.writemask_set = 0;
		  $$.writemask = 0xf;
		}
		| DOT writemask_x writemask_y writemask_z writemask_w
		{
		  $$.writemask_set = 1;
		  $$.writemask = $2 | $3 | $4 | $5;
		}
;

writemask_x:	/* empty */ { $$ = 0; }
		 | X { $$ = 1 << BRW_CHANNEL_X; }
;

writemask_y:	/* empty */ { $$ = 0; }
		 | Y { $$ = 1 << BRW_CHANNEL_Y; }
;

writemask_z:	/* empty */ { $$ = 0; }
		 | Z { $$ = 1 << BRW_CHANNEL_Z; }
;

writemask_w:	/* empty */ { $$ = 0; }
		 | W { $$ = 1 << BRW_CHANNEL_W; }
;

/* 1.4.11: Immediate values */
imm32:		exp { $$.r = imm32_d; $$.u.d = $1; }
		| MINUS exp { $$.r = imm32_d; $$.u.d = -$2; }
		| NUMBER { $$.r = imm32_f; $$.u.f = $1; }
;

/* 1.4.12: Predication and modifiers */
predicate:	/* empty */
		{
		  $$.header.predicate_control = BRW_PREDICATE_NONE;
		  $$.bits2.da1.flag_reg_nr = 0;
		  $$.header.predicate_inverse = 0;
		}
		| LPAREN predstate flagreg predctrl RPAREN
		{
		  $$.header.predicate_control = $4;
		  /* XXX: Should deal with erroring when the user tries to
		   * set a predicate for one flag register and conditional
		   * modification on the other flag register.
		   */
		  $$.bits2.da1.flag_reg_nr = $3.subreg_nr;
		  $$.header.predicate_inverse = $2;
		}
;

predstate:	/* empty */ { $$ = 0; }
		| PLUS { $$ = 0; }
		| MINUS { $$ = 1; }
;

predctrl:	/* empty */ { $$ = BRW_PREDICATE_NORMAL; }
		| DOT X { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_X; }
		| DOT Y { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_Y; }
		| DOT Z { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_Z; }
		| DOT W { $$ = BRW_PREDICATE_ALIGN16_REPLICATE_W; }
		| ANYV { $$ = BRW_PREDICATE_ALIGN1_ANYV; }
		| ALLV { $$ = BRW_PREDICATE_ALIGN1_ALLV; }
		| ANY2H { $$ = BRW_PREDICATE_ALIGN1_ANY2H; }
		| ALL2H { $$ = BRW_PREDICATE_ALIGN1_ALL2H; }
		| ANY4H { $$ = BRW_PREDICATE_ALIGN1_ANY4H; }
		| ALL4H { $$ = BRW_PREDICATE_ALIGN1_ALL4H; }
		| ANY8H { $$ = BRW_PREDICATE_ALIGN1_ANY8H; }
		| ALL8H { $$ = BRW_PREDICATE_ALIGN1_ALL8H; }
		| ANY16H { $$ = BRW_PREDICATE_ALIGN1_ANY16H; }
		| ALL16H { $$ = BRW_PREDICATE_ALIGN1_ALL16H; }
;

negate:		/* empty */ { $$ = 0; }
		| MINUS { $$ = 1; }
;

abs:		/* empty */ { $$ = 0; }
		| ABS { $$ = 1; }
;

execsize:	LPAREN exp RPAREN
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

conditionalmodifier: /* empty */    { $$ = BRW_CONDITIONAL_NONE; }
		| ZERO
		| EQUAL
		| NOT_ZERO
		| NOT_EQUAL
		| GREATER
		| GREATER_EQUAL
		| LESS
		| LESS_EQUAL
		| ROUND_INCREMENT
		| OVERFLOW
		| UNORDERED
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
extern char *input_filename;

int errors;

void yyerror (char *msg)
{
	fprintf(stderr, "%s: %d: %s at \"%s\"\n",
		input_filename, yylineno, msg, lex_text());
	++errors;
}

/**
 * Fills in the destination register information in instr from the bits in dst.
 */
int set_instruction_dest(struct brw_instruction *instr,
			 struct dst_operand *dest)
{
	if (dest->address_mode == BRW_ADDRESS_DIRECT &&
	    instr->header.access_mode == BRW_ALIGN_1) {
		instr->bits1.da1.dest_reg_file = dest->reg_file;
		instr->bits1.da1.dest_reg_type = dest->reg_type;
		instr->bits1.da1.dest_subreg_nr = dest->subreg_nr;
		instr->bits1.da1.dest_reg_nr = dest->reg_nr;
		instr->bits1.da1.dest_horiz_stride = dest->horiz_stride;
		instr->bits1.da1.dest_address_mode = dest->address_mode;
		if (dest->writemask_set) {
			fprintf(stderr, "error: write mask set in align1 "
				"instruction\n");
			return 1;
		}
	} else if (dest->address_mode == BRW_ADDRESS_DIRECT) {
		instr->bits1.da16.dest_reg_file = dest->reg_file;
		instr->bits1.da16.dest_reg_type = dest->reg_type;
		instr->bits1.da16.dest_subreg_nr = dest->subreg_nr;
		instr->bits1.da16.dest_reg_nr = dest->reg_nr;
		instr->bits1.da16.dest_address_mode = dest->address_mode;
		instr->bits1.da16.dest_writemask = dest->writemask;
	} else if (instr->header.access_mode == BRW_ALIGN_1) {
		instr->bits1.ia1.dest_reg_file = dest->reg_file;
		instr->bits1.ia1.dest_reg_type = dest->reg_type;
		instr->bits1.ia1.dest_subreg_nr = dest->address_subreg_nr;
		instr->bits1.ia1.dest_horiz_stride = dest->horiz_stride;
		instr->bits1.ia1.dest_indirect_offset = dest->indirect_offset;
		instr->bits1.ia1.dest_address_mode = dest->address_mode;
		if (dest->writemask_set) {
			fprintf(stderr, "error: write mask set in align1 "
				"instruction\n");
			return 1;
		}
	} else {
		instr->bits1.ia16.dest_reg_file = dest->reg_file;
		instr->bits1.ia16.dest_reg_type = dest->reg_type;
		instr->bits1.ia16.dest_subreg_nr = dest->address_subreg_nr;
		instr->bits1.ia16.dest_writemask = dest->writemask;
		instr->bits1.ia16.dest_indirect_offset = dest->indirect_offset;
		instr->bits1.ia16.dest_address_mode = dest->address_mode;
	}

	return 0;
}

/* Sets the first source operand for the instruction.  Returns 0 on success. */
int set_instruction_src0(struct brw_instruction *instr,
			  struct src_operand *src)
{
	instr->bits1.da1.src0_reg_file = src->reg_file;
	instr->bits1.da1.src0_reg_type = src->reg_type;
	if (src->reg_file == BRW_IMMEDIATE_VALUE) {
		instr->bits3.ud = src->imm32;
	} else if (instr->header.access_mode == BRW_ALIGN_1) {
		instr->bits2.da1.src0_subreg_nr = src->subreg_nr;
		instr->bits2.da1.src0_reg_nr = src->reg_nr;
		instr->bits2.da1.src0_vert_stride = src->vert_stride;
		instr->bits2.da1.src0_width = src->width;
		instr->bits2.da1.src0_horiz_stride = src->horiz_stride;
		instr->bits2.da1.src0_negate = src->negate;
		instr->bits2.da1.src0_abs = src->abs;
		instr->bits2.da1.src0_address_mode = src->address_mode;
		if (src->swizzle_set) {
			fprintf(stderr, "error: swizzle bits set in align1 "
				"instruction\n");
			return 1;
		}
	} else {
		instr->bits2.da16.src0_subreg_nr = src->subreg_nr;
		instr->bits2.da16.src0_reg_nr = src->reg_nr;
		instr->bits2.da16.src0_vert_stride = src->vert_stride;
		instr->bits2.da16.src0_negate = src->negate;
		instr->bits2.da16.src0_abs = src->abs;
		instr->bits2.da16.src0_swz_x = src->swizzle_x;
		instr->bits2.da16.src0_swz_y = src->swizzle_y;
		instr->bits2.da16.src0_swz_z = src->swizzle_z;
		instr->bits2.da16.src0_swz_w = src->swizzle_w;
		instr->bits2.da16.src0_address_mode = src->address_mode;
	}

	return 0;
}

/* Sets the second source operand for the instruction.  Returns 0 on success.
 */
int set_instruction_src1(struct brw_instruction *instr,
			  struct src_operand *src)
{
	instr->bits1.da1.src1_reg_file = src->reg_file;
	instr->bits1.da1.src1_reg_type = src->reg_type;
	if (src->reg_file == BRW_IMMEDIATE_VALUE) {
		instr->bits3.ud = src->imm32;
	} else if (instr->header.access_mode == BRW_ALIGN_1) {
		instr->bits3.da1.src1_subreg_nr = src->subreg_nr;
		instr->bits3.da1.src1_reg_nr = src->reg_nr;
		instr->bits3.da1.src1_vert_stride = src->vert_stride;
		instr->bits3.da1.src1_width = src->width;
		instr->bits3.da1.src1_horiz_stride = src->horiz_stride;
		instr->bits3.da1.src1_negate = src->negate;
		instr->bits3.da1.src1_abs = src->abs;
		if (src->address_mode != BRW_ADDRESS_DIRECT) {
			fprintf(stderr, "error: swizzle bits set in align1 "
				"instruction\n");
			return 1;
		}
		if (src->swizzle_set) {
			fprintf(stderr, "error: swizzle bits set in align1 "
				"instruction\n");
			return 1;
		}
	} else {
		instr->bits3.da16.src1_subreg_nr = src->subreg_nr;
		instr->bits3.da16.src1_reg_nr = src->reg_nr;
		instr->bits3.da16.src1_vert_stride = src->vert_stride;
		instr->bits3.da16.src1_negate = src->negate;
		instr->bits3.da16.src1_abs = src->abs;
		instr->bits3.da16.src1_swz_x = src->swizzle_x;
		instr->bits3.da16.src1_swz_y = src->swizzle_y;
		instr->bits3.da16.src1_swz_z = src->swizzle_z;
		instr->bits3.da16.src1_swz_w = src->swizzle_w;
		if (src->address_mode != BRW_ADDRESS_DIRECT) {
			fprintf(stderr, "error: swizzle bits set in align1 "
				"instruction\n");
			return 1;
		}
	}

	return 0;
}

void set_instruction_options(struct brw_instruction *instr,
			     struct brw_instruction *options)
{
	/* XXX: more instr options */
	instr->header.access_mode = options->header.access_mode;
	instr->header.mask_control = options->header.mask_control;
	instr->header.dependency_control = options->header.dependency_control;
	instr->header.compression_control =
		options->header.compression_control;
}

void set_instruction_predicate(struct brw_instruction *instr,
			       struct brw_instruction *predicate)
{
	instr->header.predicate_control = predicate->header.predicate_control;
	instr->header.predicate_inverse = predicate->header.predicate_inverse;
	instr->bits2.da1.flag_reg_nr = predicate->bits2.da1.flag_reg_nr;
}

void set_direct_dst_operand(struct dst_operand *dst, struct direct_reg *reg,
			    int type)
{
	bzero(dst, sizeof(*dst));
	dst->address_mode = BRW_ADDRESS_DIRECT;
	dst->reg_file = reg->reg_file;
	dst->reg_nr = reg->reg_nr;
	dst->subreg_nr = reg->subreg_nr;
	dst->reg_type = type;
	dst->horiz_stride = 1;
	dst->writemask_set = 0;
	dst->writemask = 0xf;
}

void set_direct_src_operand(struct src_operand *src, struct direct_reg *reg,
			    int type)
{
	bzero(src, sizeof(*src));
	src->address_mode = BRW_ADDRESS_DIRECT;
	src->reg_file = reg->reg_file;
	src->reg_type = type;
	src->subreg_nr = reg->subreg_nr;
	src->reg_nr = reg->reg_nr;
	src->vert_stride = 0;
	src->width = 0;
	src->horiz_stride = 0;
	src->negate = 0;
	src->abs = 0;
	src->swizzle_set = 0;
	src->swizzle_x = BRW_CHANNEL_X;
	src->swizzle_y = BRW_CHANNEL_Y;
	src->swizzle_z = BRW_CHANNEL_Z;
	src->swizzle_w = BRW_CHANNEL_W;
}
