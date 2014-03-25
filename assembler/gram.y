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
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include "gen4asm.h"
#include "brw_eu.h"
#include "gen8_instruction.h"

#define DEFAULT_EXECSIZE (ffs(program_defaults.execute_size) - 1)
#define DEFAULT_DSTREGION -1

#define SWIZZLE(reg) (reg.dw1.bits.swizzle)

#define GEN(i)	(&(i)->insn.gen)
#define GEN8(i)	(&(i)->insn.gen8)

#define YYLTYPE YYLTYPE
typedef struct YYLTYPE
{
 int first_line;
 int first_column;
 int last_line;
 int last_column;
} YYLTYPE;

extern int need_export;
static struct src_operand src_null_reg =
{
    .reg.file = BRW_ARCHITECTURE_REGISTER_FILE,
    .reg.nr = BRW_ARF_NULL,
    .reg.type = BRW_REGISTER_TYPE_UD,
};
static struct brw_reg dst_null_reg =
{
    .file = BRW_ARCHITECTURE_REGISTER_FILE,
    .nr = BRW_ARF_NULL,
};
static struct brw_reg ip_dst =
{
    .file = BRW_ARCHITECTURE_REGISTER_FILE,
    .nr = BRW_ARF_IP,
    .type = BRW_REGISTER_TYPE_UD,
    .address_mode = BRW_ADDRESS_DIRECT,
    .hstride = 1,
    .dw1.bits.writemask = BRW_WRITEMASK_XYZW,
};
static struct src_operand ip_src =
{
    .reg.file = BRW_ARCHITECTURE_REGISTER_FILE,
    .reg.nr = BRW_ARF_IP,
    .reg.type = BRW_REGISTER_TYPE_UD,
    .reg.address_mode = BRW_ADDRESS_DIRECT,
    .reg.dw1.bits.swizzle = BRW_SWIZZLE_NOOP,
};

static int get_type_size(unsigned type);
static void set_instruction_opcode(struct brw_program_instruction *instr,
				   unsigned opcode);
static int set_instruction_dest(struct brw_program_instruction *instr,
				struct brw_reg *dest);
static int set_instruction_src0(struct brw_program_instruction *instr,
				struct src_operand *src,
				YYLTYPE *location);
static int set_instruction_src1(struct brw_program_instruction *instr,
				struct src_operand *src,
				YYLTYPE *location);
static int set_instruction_dest_three_src(struct brw_program_instruction *instr,
					  struct brw_reg *dest);
static int set_instruction_src0_three_src(struct brw_program_instruction *instr,
					  struct src_operand *src);
static int set_instruction_src1_three_src(struct brw_program_instruction *instr,
					  struct src_operand *src);
static int set_instruction_src2_three_src(struct brw_program_instruction *instr,
					  struct src_operand *src);
static void set_instruction_saturate(struct brw_program_instruction *instr,
				     int saturate);
static void set_instruction_options(struct brw_program_instruction *instr,
				    struct options options);
static void set_instruction_predicate(struct brw_program_instruction *instr,
				      struct predicate *p);
static void set_instruction_pred_cond(struct brw_program_instruction *instr,
				      struct predicate *p,
				      struct condition *c,
				      YYLTYPE *location);
static void set_direct_dst_operand(struct brw_reg *dst, struct brw_reg *reg,
				   int type);
static void set_direct_src_operand(struct src_operand *src, struct brw_reg *reg,
				   int type);

void set_branch_two_offsets(struct brw_program_instruction *insn, int jip_offset, int uip_offset);
void set_branch_one_offset(struct brw_program_instruction *insn, int jip_offset);

enum message_level {
    WARN,
    ERROR,
};

static void message(enum message_level level, YYLTYPE *location,
		    const char *fmt, ...)
{
    static const char *level_str[] = { "warning", "error" };
    va_list args;

    if (location)
	fprintf(stderr, "%s:%d:%d: %s: ", input_filename, location->first_line,
		location->first_column, level_str[level]);
    else
	fprintf(stderr, "%s:%s: ", input_filename, level_str[level]);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

#define warn(flag, l, fmt, ...)					\
    do {							\
	if (warning_flags & WARN_ ## flag)			\
	    message(WARN, l, fmt, ## __VA_ARGS__);	\
    } while(0)

#define error(l, fmt, ...)			\
    do {					\
	message(ERROR, l, fmt, ## __VA_ARGS__);	\
    } while(0)

/* like strcmp, but handles NULL pointers */
static bool strcmp0(const char *s1, const char* s2)
{
    if (!s1)
	return -(s1 != s2);
    if (!s2)
	return s1 != s2;
    return strcmp (s1, s2);
}

static bool region_equal(struct region *r1, struct region *r2)
{
    return memcmp(r1, r2, sizeof(struct region)) == 0;
}

static bool reg_equal(struct brw_reg *r1, struct brw_reg *r2)
{
    return memcmp(r1, r2, sizeof(struct brw_reg)) == 0;
}

static bool declared_register_equal(struct declared_register *r1,
				     struct declared_register *r2)
{
    if (strcmp0(r1->name, r2->name) != 0)
	return false;

    if (!reg_equal(&r1->reg, &r2->reg))
	return false;

    if (!region_equal(&r1->src_region, &r2->src_region))
	return false;

    if (r1->element_size != r2->element_size ||
        r1->dst_region != r2->dst_region)
	return false;

    return true;
}

static void brw_program_init(struct brw_program *p)
{
   memset(p, 0, sizeof(struct brw_program));
}

static void brw_program_append_entry(struct brw_program *p,
				     struct brw_program_instruction *entry)
{
    entry->next = NULL;
    if (p->last)
	p->last->next = entry;
    else
	p->first = entry;
    p->last = entry;
}

static void
brw_program_add_instruction(struct brw_program *p,
			    struct brw_program_instruction *instruction)
{
    struct brw_program_instruction *list_entry;

    list_entry = calloc(sizeof(struct brw_program_instruction), 1);
    list_entry->type = GEN4ASM_INSTRUCTION_GEN;
    list_entry->insn.gen = instruction->insn.gen;
    brw_program_append_entry(p, list_entry);
}

static void
brw_program_add_relocatable(struct brw_program *p,
			    struct brw_program_instruction *instruction)
{
    struct brw_program_instruction *list_entry;

    list_entry = calloc(sizeof(struct brw_program_instruction), 1);
    list_entry->type = GEN4ASM_INSTRUCTION_GEN_RELOCATABLE;
    list_entry->insn.gen = instruction->insn.gen;
    list_entry->reloc = instruction->reloc;
    brw_program_append_entry(p, list_entry);
}

static void brw_program_add_label(struct brw_program *p, const char *label)
{
    struct brw_program_instruction *list_entry;

    list_entry = calloc(sizeof(struct brw_program_instruction), 1);
    list_entry->type = GEN4ASM_INSTRUCTION_LABEL;
    list_entry->insn.label.name = strdup(label);
    brw_program_append_entry(p, list_entry);
}

static int resolve_dst_region(struct declared_register *reference, int region)
{
    int resolved = region;

    if (resolved == DEFAULT_DSTREGION) {
	if (reference)
	    resolved = reference->dst_region;
        else
            resolved = 1;
    }

    assert(resolved == 1 || resolved == 2 || resolved == 3);
    return resolved;
}

static inline int access_mode(struct brw_program_instruction *insn)
{
    if (IS_GENp(8))
	return gen8_access_mode(GEN8(insn));
    else
	return GEN(insn)->header.access_mode;
}

static inline int exec_size(struct brw_program_instruction *insn)
{
    if (IS_GENp(8))
	return gen8_exec_size(GEN8(insn));
    else
	return GEN(insn)->header.execution_size;
}

static void set_execsize(struct brw_program_instruction *insn, int execsize)
{
    if (IS_GENp(8))
	gen8_set_exec_size(GEN8(insn), execsize);
    else
	GEN(insn)->header.execution_size = execsize;
}

static bool validate_dst_reg(struct brw_program_instruction *insn, struct brw_reg *reg)
{

    if (reg->address_mode == BRW_ADDRESS_DIRECT &&
	access_mode(insn) == BRW_ALIGN_1 &&
	reg->dw1.bits.writemask != 0 &&
	reg->dw1.bits.writemask != BRW_WRITEMASK_XYZW)
    {
	fprintf(stderr, "error: write mask set in align1 instruction\n");
	return false;
    }

    if (reg->address_mode == BRW_ADDRESS_REGISTER_INDIRECT_REGISTER &&
	access_mode(insn) == BRW_ALIGN_16) {
	fprintf(stderr, "error: indirect Dst addr mode in align16 instruction\n");
	return false;
    }

    return true;
}

static bool validate_src_reg(struct brw_program_instruction *insn,
			     struct brw_reg reg,
			     YYLTYPE *location)
{
    int hstride_for_reg[] = {0, 1, 2, 4};
    int vstride_for_reg[] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256};
    int width_for_reg[] = {1, 2, 4, 8, 16};
    int execsize_for_reg[] = {1, 2, 4, 8, 16, 32};
    int width, hstride, vstride, execsize;

    if (reg.file == BRW_IMMEDIATE_VALUE)
	return true;

    if (access_mode(insn) == BRW_ALIGN_1 &&
	SWIZZLE(reg) && SWIZZLE(reg) != BRW_SWIZZLE_NOOP)
    {
	error(location, "swizzle bits set in align1 instruction\n");
	return false;
    }

    if (reg.address_mode == BRW_ADDRESS_REGISTER_INDIRECT_REGISTER &&
	access_mode(insn) == BRW_ALIGN_16) {
	fprintf(stderr, "error: indirect Source addr mode in align16 instruction\n");
	return false;
    }

    assert(reg.hstride >= 0 && reg.hstride < ARRAY_SIZE(hstride_for_reg));
    hstride = hstride_for_reg[reg.hstride];

    if (reg.vstride == 0xf) {
	vstride = -1;
    } else {
	assert(reg.vstride >= 0 && reg.vstride < ARRAY_SIZE(vstride_for_reg));
	vstride = vstride_for_reg[reg.vstride];
    }

    assert(reg.width >= 0 && reg.width < ARRAY_SIZE(width_for_reg));
    width = width_for_reg[reg.width];

    assert(exec_size(insn) >= 0 &&
	   exec_size(insn) < ARRAY_SIZE(execsize_for_reg));
    execsize = execsize_for_reg[exec_size(insn)];

    /* Register Region Restrictions */

    /* B. If ExecSize = Width and HorzStride ≠ 0, VertStride must be set to
     * Width * HorzStride. */
    if (execsize == width && hstride != 0) {
	if (vstride != -1 && vstride != width * hstride)
	    warn(ALL, location, "execution size == width and hstride != 0 but "
		 "vstride is not width * hstride\n");
    }

    /* D. If Width = 1, HorzStride must be 0 regardless of the values of
     * ExecSize and VertStride.
     *
     * FIXME: In "advanced mode" hstride is set to 1, this is probably a bug
     * to fix, but it changes the generated opcodes and thus needs validation.
     */
    if (width == 1 && hstride != 0)
	warn(ALL, location, "region width is 1 but horizontal stride is %d "
	     " (should be 0)\n", hstride);

    /* E. If ExecSize = Width = 1, both VertStride and HorzStride must be 0.
     * This defines a scalar. */
    if (execsize == 1 && width == 1) {
        if (hstride != 0)
	    warn(ALL, location, "execution size and region width are 1 but "
		 "horizontal stride is %d (should be 0)\n", hstride);
        if (vstride != 0)
	    warn(ALL, location, "execution size and region width are 1 but "
		 "vertical stride is %d (should be 0)\n", vstride);
    }

    return true;
}

static int get_subreg_address(unsigned regfile, unsigned type, unsigned subreg, unsigned address_mode)
{
    int unit_size = 1;

    assert(address_mode == BRW_ADDRESS_DIRECT);
    assert(regfile != BRW_IMMEDIATE_VALUE);

    if (advanced_flag)
	unit_size = get_type_size(type);

    return subreg * unit_size;
}

/* only used in indirect address mode.
 * input: sub-register number of an address register
 * output: the value of AddrSubRegNum in the instruction binary code
 *
 * input  output(advanced_flag==0)  output(advanced_flag==1)
 *  a0.0             0                         0
 *  a0.1        invalid input                  1
 *  a0.2             1                         2
 *  a0.3        invalid input                  3
 *  a0.4             2                         4
 *  a0.5        invalid input                  5
 *  a0.6             3                         6
 *  a0.7        invalid input                  7
 *  a0.8             4                  invalid input
 *  a0.10            5                  invalid input
 *  a0.12            6                  invalid input
 *  a0.14            7                  invalid input
 */
static int get_indirect_subreg_address(unsigned subreg)
{
    return advanced_flag == 0 ? subreg / 2 : subreg;
}

static void resolve_subnr(struct brw_reg *reg)
{
   if (reg->file == BRW_IMMEDIATE_VALUE)
	return;

   if (reg->address_mode == BRW_ADDRESS_DIRECT)
	reg->subnr = get_subreg_address(reg->file, reg->type, reg->subnr,
					reg->address_mode);
   else
        reg->subnr = get_indirect_subreg_address(reg->subnr);
}


%}
%locations

%start ROOT

%union {
	char *string;
	int integer;
	double number;
	struct brw_program_instruction instruction;
	struct brw_program program;
	struct region region;
	struct regtype regtype;
	struct brw_reg reg;
	struct condition condition;
	struct predicate predicate;
	struct options options;
	struct declared_register symbol_reg;
	imm32_t imm32;

	struct src_operand src_operand;
}

%token COLON
%token SEMICOLON
%token LPAREN RPAREN
%token LANGLE RANGLE
%token LCURLY RCURLY
%token LSQUARE RSQUARE
%token COMMA EQ
%token ABS DOT 
%token PLUS MINUS MULTIPLY DIVIDE

%token <integer> TYPE_UD TYPE_D TYPE_UW TYPE_W TYPE_UB TYPE_B
%token <integer> TYPE_VF TYPE_HF TYPE_V TYPE_F

%token ALIGN1 ALIGN16 SECHALF COMPR SWITCH ATOMIC NODDCHK NODDCLR
%token MASK_DISABLE BREAKPOINT ACCWRCTRL EOT

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
%token <integer> AVG ADD SEL AND OR XOR SHR SHL ASR CMP CMPN PLN
%token <integer> ADDC BFI1 BFREV CBIT F16TO32 F32TO16 FBH FBL
%token <integer> SEND SENDC NOP JMPI IF IFF WHILE ELSE BREAK CONT HALT MSAVE
%token <integer> PUSH MREST POP WAIT DO ENDIF ILLEGAL
%token <integer> MATH_INST
%token <integer> MAD LRP BFE BFI2 SUBB
%token <integer> CALL RET
%token <integer> BRD BRC

%token NULL_TOKEN MATH SAMPLER GATEWAY READ WRITE URB THREAD_SPAWNER VME DATA_PORT CRE

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

%token <integer> KERNEL_PRAGMA END_KERNEL_PRAGMA CODE_PRAGMA END_CODE_PRAGMA
%token <integer> REG_COUNT_PAYLOAD_PRAGMA REG_COUNT_TOTAL_PRAGMA DECLARE_PRAGMA
%token <integer> BASE ELEMENTSIZE SRCREGION DSTREGION TYPE

%token <integer> DEFAULT_EXEC_SIZE_PRAGMA DEFAULT_REG_TYPE_PRAGMA
%nonassoc SUBREGNUM
%nonassoc SNDOPR
%left  PLUS MINUS
%left  MULTIPLY DIVIDE
%right UMINUS
%nonassoc DOT
%nonassoc STR_SYMBOL_REG
%nonassoc EMPTEXECSIZE
%nonassoc LPAREN

%type <integer> exp sndopr
%type <integer> simple_int
%type <instruction> instruction unaryinstruction binaryinstruction
%type <instruction> binaryaccinstruction trinaryinstruction sendinstruction
%type <instruction> syncinstruction
%type <instruction> msgtarget
%type <instruction> mathinstruction
%type <instruction> nopinstruction
%type <instruction> relocatableinstruction breakinstruction
%type <instruction> ifelseinstruction loopinstruction haltinstruction
%type <instruction> multibranchinstruction subroutineinstruction jumpinstruction
%type <string> label
%type <program> instrseq
%type <integer> instoption
%type <integer> unaryop binaryop binaryaccop breakop
%type <integer> trinaryop
%type <integer> sendop
%type <condition> conditionalmodifier 
%type <predicate> predicate
%type <options> instoptions instoption_list
%type <integer> condition saturate negate abs chansel
%type <integer> writemask_x writemask_y writemask_z writemask_w
%type <integer> srcimmtype execsize dstregion immaddroffset
%type <integer> subregnum sampler_datatype
%type <integer> urb_swizzle urb_allocate urb_used urb_complete
%type <integer> math_function math_signed math_scalar
%type <integer> predctrl predstate
%type <region> region region_wh indirectregion declare_srcregion;
%type <regtype> regtype
%type <reg> directgenreg directmsgreg addrreg accreg flagreg maskreg
%type <reg> maskstackreg notifyreg
/* %type <reg>  maskstackdepthreg */
%type <reg> statereg controlreg ipreg nullreg
%type <reg> dstoperandex_typed srcarchoperandex_typed
%type <reg> sendleadreg
%type <reg> indirectgenreg indirectmsgreg addrparam
%type <integer> mask_subreg maskstack_subreg 
%type <integer> declare_elementsize declare_dstregion declare_type
/* %type <intger> maskstackdepth_subreg */
%type <symbol_reg> symbol_reg symbol_reg_p;
%type <imm32> imm32
%type <reg> dst dstoperand dstoperandex dstreg post_dst writemask
%type <reg> declare_base
%type <src_operand> directsrcoperand srcarchoperandex directsrcaccoperand
%type <src_operand> indirectsrcoperand
%type <src_operand> src srcimm imm32reg payload srcacc srcaccimm swizzle
%type <src_operand> relativelocation relativelocation2

%code {

#undef error
#define error(l, fmt, ...)			\
    do {					\
	message(ERROR, l, fmt, ## __VA_ARGS__);	\
	YYERROR;				\
    } while(0)

static void add_option(struct options *options, int option)
{
    switch (option) {
    case ALIGN1:
	options->access_mode = BRW_ALIGN_1;
	break;
    case ALIGN16:
	options->access_mode = BRW_ALIGN_16;
	break;
    case SECHALF:
	options->compression_control |= BRW_COMPRESSION_2NDHALF;
	break;
    case COMPR:
	if (!IS_GENp(6))
	    options->compression_control |= BRW_COMPRESSION_COMPRESSED;
	break;
    case SWITCH:
	options->thread_control |= BRW_THREAD_SWITCH;
	break;
    case ATOMIC:
	options->thread_control |= BRW_THREAD_ATOMIC;
	break;
    case NODDCHK:
	options->dependency_control |= BRW_DEPENDENCY_NOTCHECKED;
	break;
    case NODDCLR:
	options->dependency_control |= BRW_DEPENDENCY_NOTCLEARED;
	break;
    case MASK_DISABLE:
	options->mask_control = BRW_MASK_DISABLE;
	break;
    case BREAKPOINT:
	options->debug_control = BRW_DEBUG_BREAKPOINT;
	break;
    case ACCWRCTRL:
	options->acc_wr_control = BRW_ACCUMULATOR_WRITE_ENABLE;
	break;
    case EOT:
	options->end_of_thread = 1;
	break;
    }
}

}

%%
simple_int:     INTEGER { $$ = $1; }
		| MINUS INTEGER { $$ = -$2;}
;

exp:		INTEGER { $$ = $1; }
		| exp PLUS exp { $$ = $1 + $3; }
		| exp MINUS exp { $$ = $1 - $3; }
		| exp MULTIPLY exp { $$ = $1 * $3; } 
		| exp DIVIDE exp { if ($3) $$ = $1 / $3; else YYERROR;}
		| MINUS exp %prec UMINUS { $$ = -$2;}
		| LPAREN exp RPAREN { $$ = $2; }
		;

ROOT:		instrseq
		{
		  compiled_program = $1;
		}
;


label:          STRING COLON
;

declare_base:  	BASE EQ dstreg 
	       	{
		   $$ = $3;
	       	}
;
declare_elementsize:  ELEMENTSIZE EQ exp
		{
		   $$ = $3;
		}
;
declare_srcregion: /* empty */
		{
		  /* XXX is this default correct?*/
		  memset (&$$, '\0', sizeof ($$));
		  $$.vert_stride = ffs(0);
		  $$.width = BRW_WIDTH_1;
		  $$.horiz_stride = ffs(0);
		}
		| SRCREGION EQ region
		{
		    $$ = $3;
		}
;
declare_dstregion: /* empty */
		{
		    $$ = 1;
		}
		| DSTREGION EQ dstregion
		{
		    $$ = $3;
		}
;
declare_type:	TYPE EQ regtype
		{
		    $$ = $3.type;
		}
;
declare_pragma:	DECLARE_PRAGMA STRING declare_base declare_elementsize declare_srcregion declare_dstregion declare_type
		{
		    struct declared_register reg, *found, *new_reg;

		    reg.name = $2;
		    reg.reg = $3;
		    reg.element_size = $4;
		    reg.src_region = $5;
		    reg.dst_region = $6;
		    reg.reg.type = $7;

		    found = find_register($2);
		    if (found) {
		        if (!declared_register_equal(&reg, found))
			    error(&@1, "%s already defined and definitions "
				  "don't agree\n", $2);
			free($2); // $2 has been malloc'ed by strdup
		    } else {
			new_reg = malloc(sizeof(struct declared_register));
			*new_reg = reg;
			insert_register(new_reg);
		    }
		}
;

reg_count_total_pragma: 	REG_COUNT_TOTAL_PRAGMA exp
;
reg_count_payload_pragma: 	REG_COUNT_PAYLOAD_PRAGMA exp
;

default_exec_size_pragma:	DEFAULT_EXEC_SIZE_PRAGMA exp
				{
				    program_defaults.execute_size = $2;
				}
;
default_reg_type_pragma:	DEFAULT_REG_TYPE_PRAGMA regtype
				{
				    program_defaults.register_type = $2.type;
				}
;
pragma:		reg_count_total_pragma
		|reg_count_payload_pragma
		|default_exec_size_pragma
		|default_reg_type_pragma
		|declare_pragma
;		

instrseq:	instrseq pragma
		{
		    $$ = $1;
		}
		| instrseq instruction SEMICOLON
		{
		  brw_program_add_instruction(&$1, &$2);
		  $$ = $1;
		}
		| instruction SEMICOLON
		{
		  brw_program_init(&$$);
		  brw_program_add_instruction(&$$, &$1);
		}
		| instrseq relocatableinstruction SEMICOLON
		{
		  brw_program_add_relocatable(&$1, &$2);
		  $$ = $1;
		}
		| relocatableinstruction SEMICOLON
		{
		  brw_program_init(&$$);
		  brw_program_add_relocatable(&$$, &$1);
		}
		| instrseq SEMICOLON
		{
		    $$ = $1;
		}
		| instrseq label
        	{
		  brw_program_add_label(&$1, $2);
		  $$ = $1;
                }
		| label
		{
		  brw_program_init(&$$);
		  brw_program_add_label(&$$, $1);
		}
		| pragma
		{
		  $$.first = NULL;
		  $$.last = NULL;
		}
		| instrseq error SEMICOLON {
		  $$ = $1;
		}
;

/* 1.4.1: Instruction groups */
// binaryinstruction:    Source operands cannot be accumulators
// binaryaccinstruction: Source operands can be accumulators
instruction:	unaryinstruction
		| binaryinstruction
		| binaryaccinstruction
		| trinaryinstruction
		| sendinstruction
		| syncinstruction
		| mathinstruction
		| nopinstruction
;

/* relocatableinstruction are instructions that needs a relocation pass */
relocatableinstruction:	ifelseinstruction
			| loopinstruction
			| haltinstruction
			| multibranchinstruction
			| subroutineinstruction
			| jumpinstruction
			| breakinstruction
;

ifelseinstruction: ENDIF
		{
		  // for Gen4 
		  if(IS_GENp(6)) // For gen6+.
		    error(&@1, "should be 'ENDIF execsize relativelocation'\n");
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $1);
		  GEN(&$$)->header.thread_control |= BRW_THREAD_SWITCH;
		  GEN(&$$)->bits1.da1.dest_horiz_stride = 1;
		  GEN(&$$)->bits1.da1.src1_reg_file = BRW_ARCHITECTURE_REGISTER_FILE;
		  GEN(&$$)->bits1.da1.src1_reg_type = BRW_REGISTER_TYPE_UD;
		}
		| ENDIF execsize relativelocation instoptions
		{
		  // for Gen6+
		  /* Gen6, Gen7 bspec: predication is prohibited */
		  if(!IS_GENp(6)) // for gen6-
		    error(&@1, "ENDIF Syntax error: should be 'ENDIF'\n");
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $1);
		  set_execsize(&$$, $2);
		  $$.reloc.first_reloc_target = $3.reloc_target;
		  $$.reloc.first_reloc_offset = $3.imm32;
		}
		| ELSE execsize relativelocation instoptions
		{
		  if(!IS_GENp(6)) {
		    // for Gen4, Gen5. gen_level < 60
		    /* Set the istack pop count, which must always be 1. */
		    $3.imm32 |= (1 << 16);

		    memset(&$$, 0, sizeof($$));
		    set_instruction_opcode(&$$, $1);
		    GEN(&$$)->header.thread_control |= BRW_THREAD_SWITCH;
		    ip_dst.width = $2;
		    set_instruction_dest(&$$, &ip_dst);
		    set_instruction_src0(&$$, &ip_src, NULL);
		    set_instruction_src1(&$$, &$3, NULL);
		    $$.reloc.first_reloc_target = $3.reloc_target;
		    $$.reloc.first_reloc_offset = $3.imm32;
		  } else if(IS_GENp(6)) {
		    memset(&$$, 0, sizeof($$));
		    set_instruction_opcode(&$$, $1);
		    set_execsize(&$$, $2);
		    $$.reloc.first_reloc_target = $3.reloc_target;
		    $$.reloc.first_reloc_offset = $3.imm32;
		  } else {
		    error(&@1, "'ELSE' instruction is not implemented.\n");
		  }
		}
		| predicate IF execsize relativelocation
		{
		  /* The branch instructions require that the IP register
		   * be the destination and first source operand, while the
		   * offset is the second source operand.  The offset is added
		   * to the pre-incremented IP.
		   */
		  if(IS_GENp(7)) /* Error in Gen7+. */
		    error(&@2, "IF should be 'IF execsize JIP UIP'\n");

		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  if(!IS_GENp(6)) {
		    GEN(&$$)->header.thread_control |= BRW_THREAD_SWITCH;
		    ip_dst.width = $3;
		    set_instruction_dest(&$$, &ip_dst);
		    set_instruction_src0(&$$, &ip_src, NULL);
		    set_instruction_src1(&$$, &$4, NULL);
		  }
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		}
		| predicate IF execsize relativelocation relativelocation
		{
		  /* for Gen7+ */
		  if(!IS_GENp(7))
		    error(&@2, "IF should be 'IF execsize relativelocation'\n");

		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  set_execsize(&$$, $3);
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		  $$.reloc.second_reloc_target = $5.reloc_target;
		  $$.reloc.second_reloc_offset = $5.imm32;
		}
;

loopinstruction: predicate WHILE execsize relativelocation instoptions
		{
		  if(!IS_GENp(6)) {
		    /* The branch instructions require that the IP register
		     * be the destination and first source operand, while the
		     * offset is the second source operand.  The offset is added
		     * to the pre-incremented IP.
		     */
		    ip_dst.width = $3;
		    set_instruction_dest(&$$, &ip_dst);
		    memset(&$$, 0, sizeof($$));
		    set_instruction_predicate(&$$, &$1);
		    set_instruction_opcode(&$$, $2);
		    GEN(&$$)->header.thread_control |= BRW_THREAD_SWITCH;
		    set_instruction_src0(&$$, &ip_src, NULL);
		    set_instruction_src1(&$$, &$4, NULL);
		    $$.reloc.first_reloc_target = $4.reloc_target;
		    $$.reloc.first_reloc_offset = $4.imm32;
		  } else if (IS_GENp(6)) {
		    /* Gen6 spec:
		         dest must have the same element size as src0.
		         dest horizontal stride must be 1. */
		    memset(&$$, 0, sizeof($$));
		    set_instruction_predicate(&$$, &$1);
		    set_instruction_opcode(&$$, $2);
		    set_execsize(&$$, $3);
		    $$.reloc.first_reloc_target = $4.reloc_target;
		    $$.reloc.first_reloc_offset = $4.imm32;
		  } else {
		    error(&@2, "'WHILE' instruction is not implemented!\n");
		  }
		}
		| DO
		{
		  // deprecated
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $1);
		};

haltinstruction: predicate HALT execsize relativelocation relativelocation instoptions
		{
		  // for Gen6, Gen7
		  /* Gen6, Gen7 bspec: dst and src0 must be the null reg. */
		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		  $$.reloc.second_reloc_target = $5.reloc_target;
		  $$.reloc.second_reloc_offset = $5.imm32;
		  dst_null_reg.width = $3;
		  set_instruction_dest(&$$, &dst_null_reg);
		  set_instruction_src0(&$$, &src_null_reg, NULL);
		};

multibranchinstruction:
		predicate BRD execsize relativelocation instoptions
		{
		  /* Gen7 bspec: dest must be null. use Switch option */
		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  if (IS_GENp(8))
                      gen8_set_thread_control(GEN8(&$$), gen8_thread_control(GEN8(&$$)) | BRW_THREAD_SWITCH);
		  else
                      GEN(&$$)->header.thread_control |= BRW_THREAD_SWITCH;
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		  dst_null_reg.width = $3;
		  set_instruction_dest(&$$, &dst_null_reg);
		}
		| predicate BRC execsize relativelocation relativelocation instoptions
		{
		  /* Gen7 bspec: dest must be null. src0 must be null. use Switch option */
		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  if (IS_GENp(8))
                      gen8_set_thread_control(GEN8(&$$), gen8_thread_control(GEN8(&$$)) | BRW_THREAD_SWITCH);
		  else
                      GEN(&$$)->header.thread_control |= BRW_THREAD_SWITCH;
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		  $$.reloc.second_reloc_target = $5.reloc_target;
		  $$.reloc.second_reloc_offset = $5.imm32;
		  dst_null_reg.width = $3;
		  set_instruction_dest(&$$, &dst_null_reg);
		  set_instruction_src0(&$$, &src_null_reg, NULL);
		}
;

subroutineinstruction:
		predicate CALL execsize dst relativelocation instoptions
		{
		  /*
		    Gen6 bspec:
		       source, dest type should be DWORD.
		       dest must be QWord aligned.
		       source0 region control must be <2,2,1>.
		       execution size must be 2.
		       QtrCtrl is prohibited.
		       JIP is an immediate operand, must be of type W.
		    Gen7 bspec:
		       source, dest type should be DWORD.
		       dest must be QWord aligned.
		       source0 region control must be <2,2,1>.
		       execution size must be 2.
		   */
		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);

		  $4.type = BRW_REGISTER_TYPE_D; /* dest type should be DWORD */
		  $4.width = BRW_WIDTH_2; /* execution size must be 2. */
		  set_instruction_dest(&$$, &$4);

		  struct src_operand src0;
		  memset(&src0, 0, sizeof(src0));
		  src0.reg.type = BRW_REGISTER_TYPE_D; /* source type should be DWORD */
		  /* source0 region control must be <2,2,1>. */
		  src0.reg.hstride = 1; /*encoded 1*/
		  src0.reg.width = BRW_WIDTH_2;
		  src0.reg.vstride = 2; /*encoded 2*/
		  set_instruction_src0(&$$, &src0, NULL);

		  $$.reloc.first_reloc_target = $5.reloc_target;
		  $$.reloc.first_reloc_offset = $5.imm32;
		}
		| predicate RET execsize dstoperandex src instoptions
		{
		  /*
		     Gen6, 7:
		       source cannot be accumulator.
		       dest must be null.
		       src0 region control must be <2,2,1> (not specified clearly. should be same as CALL)
		   */
		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  dst_null_reg.width = BRW_WIDTH_2; /* execution size of RET should be 2 */
		  set_instruction_dest(&$$, &dst_null_reg);
		  $5.reg.type = BRW_REGISTER_TYPE_D;
		  $5.reg.hstride = 1; /*encoded 1*/
		  $5.reg.width = BRW_WIDTH_2;
		  $5.reg.vstride = 2; /*encoded 2*/
		  set_instruction_src0(&$$, &$5, NULL);
		}
;

unaryinstruction:
		predicate unaryop conditionalmodifier saturate execsize
		dst srcaccimm instoptions
		{
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  set_instruction_saturate(&$$, $4);
		  $6.width = $5;
		  set_instruction_options(&$$, $8);
		  set_instruction_pred_cond(&$$, &$1, &$3, &@3);
		  if (set_instruction_dest(&$$, &$6) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$7, &@7) != 0)
		    YYERROR;

		  if (!IS_GENp(6) && 
				get_type_size(GEN(&$$)->bits1.da1.dest_reg_type) * (1 << $6.width) == 64)
		    GEN(&$$)->header.compression_control = BRW_COMPRESSION_COMPRESSED;
		}
;

unaryop:	MOV | FRC | RNDU | RNDD | RNDE | RNDZ | NOT | LZD | BFREV | CBIT
          | F16TO32 | F32TO16 | FBH | FBL
;

// Source operands cannot be accumulators
binaryinstruction:
		predicate binaryop conditionalmodifier saturate execsize
		dst src srcimm instoptions
		{
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  set_instruction_saturate(&$$, $4);
		  set_instruction_options(&$$, $9);
		  set_instruction_pred_cond(&$$, &$1, &$3, &@3);
		  $6.width = $5;
		  if (set_instruction_dest(&$$, &$6) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$7, &@7) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$8, &@8) != 0)
		    YYERROR;

		  if (!IS_GENp(6) && 
				get_type_size(GEN(&$$)->bits1.da1.dest_reg_type) * (1 << $6.width) == 64)
		    GEN(&$$)->header.compression_control = BRW_COMPRESSION_COMPRESSED;
		}
;

/* bspec: BFI1 should not access accumulator. */
binaryop:	MUL | MAC | MACH | LINE | SAD2 | SADA2 | DP4 | DPH | DP3 | DP2 | PLN | BFI1
;

// Source operands can be accumulators
binaryaccinstruction:
		predicate binaryaccop conditionalmodifier saturate execsize
		dst srcacc srcimm instoptions
		{
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  set_instruction_saturate(&$$, $4);
		  $6.width = $5;
		  set_instruction_options(&$$, $9);
		  set_instruction_pred_cond(&$$, &$1, &$3, &@3);
		  if (set_instruction_dest(&$$, &$6) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$7, &@7) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$8, &@8) != 0)
		    YYERROR;

		  if (!IS_GENp(6) && 
				get_type_size(GEN(&$$)->bits1.da1.dest_reg_type) * (1 << $6.width) == 64)
		    GEN(&$$)->header.compression_control = BRW_COMPRESSION_COMPRESSED;
		}
;

/* TODO: bspec says ADDC/SUBB/CMP/CMPN/SHL/BFI1 cannot use accumulator as dest. */
binaryaccop:	AVG | ADD | SEL | AND | OR | XOR | SHR | SHL | ASR | CMP | CMPN | ADDC | SUBB
;

trinaryop:	MAD | LRP | BFE | BFI2
;

trinaryinstruction:
		predicate trinaryop conditionalmodifier saturate execsize
		dst src src src instoptions
{
		  memset(&$$, 0, sizeof($$));

		  set_instruction_pred_cond(&$$, &$1, &$3, &@3);

		  set_instruction_opcode(&$$, $2);
		  set_instruction_saturate(&$$, $4);

		  $6.width = $5;
		  if (set_instruction_dest_three_src(&$$, &$6))
		    YYERROR;
		  if (set_instruction_src0_three_src(&$$, &$7))
		    YYERROR;
		  if (set_instruction_src1_three_src(&$$, &$8))
		    YYERROR;
		  if (set_instruction_src2_three_src(&$$, &$9))
		    YYERROR;
		  set_instruction_options(&$$, $10);
}
;

sendop:		SEND | SENDC
;

sendinstruction: predicate sendop execsize exp post_dst payload msgtarget
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
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  $5.width = $3;
		  GEN(&$$)->header.destreg__conditionalmod = $4; /* msg reg index */
		  set_instruction_predicate(&$$, &$1);
		  if (set_instruction_dest(&$$, &$5) != 0)
		    YYERROR;

		  if (IS_GENp(6)) {
                      struct src_operand src0;

                      memset(&src0, 0, sizeof(src0));
                      src0.reg.address_mode = BRW_ADDRESS_DIRECT;

                      if (IS_GENp(7))
                          src0.reg.file = BRW_GENERAL_REGISTER_FILE;
                      else
                          src0.reg.file = BRW_MESSAGE_REGISTER_FILE;

                      src0.reg.type = BRW_REGISTER_TYPE_D;
                      src0.reg.nr = $4;
                      src0.reg.subnr = 0;
                      set_instruction_src0(&$$, &src0, NULL);
		  } else {
                      if (set_instruction_src0(&$$, &$6, &@6) != 0)
                          YYERROR;
		  }

		  if (IS_GENp(8)) {
		      gen8_set_src1_reg_file(GEN8(&$$), BRW_IMMEDIATE_VALUE);
		      gen8_set_src1_reg_type(GEN8(&$$), BRW_REGISTER_TYPE_D);
		  } else {
		      GEN(&$$)->bits1.da1.src1_reg_file = BRW_IMMEDIATE_VALUE;
		      GEN(&$$)->bits1.da1.src1_reg_type = BRW_REGISTER_TYPE_D;
		  }

		  if (IS_GENp(8)) {
		      GEN8(&$$)->data[3] = GEN8(&$7)->data[3];
		      gen8_set_sfid(GEN8(&$$), gen8_sfid(GEN8(&$7)));
		      gen8_set_mlen(GEN8(&$$), $9);
		      gen8_set_rlen(GEN8(&$$), $11);
		      gen8_set_eot(GEN8(&$$), $12.end_of_thread);
		  } else if (IS_GENp(5)) {
                      if (IS_GENp(6)) {
                          GEN(&$$)->header.destreg__conditionalmod = GEN(&$7)->bits2.send_gen5.sfid;
                      } else {
                          GEN(&$$)->header.destreg__conditionalmod = $4; /* msg reg index */
                          GEN(&$$)->bits2.send_gen5.sfid = GEN(&$7)->bits2.send_gen5.sfid;
                          GEN(&$$)->bits2.send_gen5.end_of_thread = $12.end_of_thread;
                      }

                      GEN(&$$)->bits3.generic_gen5 = GEN(&$7)->bits3.generic_gen5;
                      GEN(&$$)->bits3.generic_gen5.msg_length = $9;
                      GEN(&$$)->bits3.generic_gen5.response_length = $11;
                      GEN(&$$)->bits3.generic_gen5.end_of_thread = $12.end_of_thread;
		  } else {
                      GEN(&$$)->header.destreg__conditionalmod = $4; /* msg reg index */
                      GEN(&$$)->bits3.generic = GEN(&$7)->bits3.generic;
                      GEN(&$$)->bits3.generic.msg_length = $9;
                      GEN(&$$)->bits3.generic.response_length = $11;
                      GEN(&$$)->bits3.generic.end_of_thread = $12.end_of_thread;
		  }
		}
		| predicate sendop execsize dst sendleadreg payload directsrcoperand instoptions
		{
		  if (IS_GENp(6))
                      error(&@2, "invalid syntax for send on gen6+\n");

		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  GEN(&$$)->header.destreg__conditionalmod = $5.nr; /* msg reg index */

		  set_instruction_predicate(&$$, &$1);

		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$6, &@6) != 0)
		    YYERROR;
		  /* XXX is this correct? */
		  if (set_instruction_src1(&$$, &$7, &@7) != 0)
		    YYERROR;

		  }
		| predicate sendop execsize dst sendleadreg payload imm32reg instoptions
                {
		  if (IS_GENp(6))
                      error(&@2, "invalid syntax for send on gen6+\n");

		  if ($7.reg.type != BRW_REGISTER_TYPE_UD &&
		      $7.reg.type != BRW_REGISTER_TYPE_D &&
		      $7.reg.type != BRW_REGISTER_TYPE_V) {
		    error (&@7, "non-int D/UD/V representation: %d,"
			   "type=%d\n", $7.reg.dw1.ud, $7.reg.type);
		  }
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  GEN(&$$)->header.destreg__conditionalmod = $5.nr; /* msg reg index */

		  set_instruction_predicate(&$$, &$1);
		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$6, &@6) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$7, &@7) != 0)
		    YYERROR;
                }
		| predicate sendop execsize dst sendleadreg sndopr imm32reg instoptions
		{
		  struct src_operand src0;

		  if (!IS_GENp(6))
                      error(&@2, "invalid syntax for send on gen6+\n");

		  if ($7.reg.type != BRW_REGISTER_TYPE_UD &&
                      $7.reg.type != BRW_REGISTER_TYPE_D &&
                      $7.reg.type != BRW_REGISTER_TYPE_V) {
                      error(&@7,"non-int D/UD/V representation: %d,"
			    "type=%d\n", $7.reg.dw1.ud, $7.reg.type);
		  }

		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  set_instruction_predicate(&$$, &$1);

		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
                      YYERROR;

                  memset(&src0, 0, sizeof(src0));
                  src0.reg.address_mode = BRW_ADDRESS_DIRECT;

                  if (IS_GENp(7)) {
                      src0.reg.file = BRW_GENERAL_REGISTER_FILE;
                      src0.reg.type = BRW_REGISTER_TYPE_UB;
                  } else {
                      src0.reg.file = BRW_MESSAGE_REGISTER_FILE;
                      src0.reg.type = BRW_REGISTER_TYPE_D;
                  }

                  src0.reg.nr = $5.nr;
                  src0.reg.subnr = 0;
                  set_instruction_src0(&$$, &src0, NULL);
		  set_instruction_src1(&$$, &$7, NULL);

                  if (IS_GENp(8)) {
                      gen8_set_sfid(GEN8(&$$), $6 & EX_DESC_SFID_MASK);
                      gen8_set_eot(GEN8(&$$), !!($6 & EX_DESC_EOT_MASK));
		  } else {
                      GEN(&$$)->header.destreg__conditionalmod = ($6 & EX_DESC_SFID_MASK); /* SFID */
                      GEN(&$$)->bits3.generic_gen5.end_of_thread = !!($6 & EX_DESC_EOT_MASK);
                  }
		}
		| predicate sendop execsize dst sendleadreg sndopr directsrcoperand instoptions
		{
		  struct src_operand src0;

		  if (!IS_GENp(6))
                      error(&@2, "invalid syntax for send on gen6+\n");

                  if ($7.reg.file != BRW_ARCHITECTURE_REGISTER_FILE ||
                      ($7.reg.nr & 0xF0) != BRW_ARF_ADDRESS ||
                      ($7.reg.nr & 0x0F) != 0 ||
                      $7.reg.subnr != 0) {
                      error (&@7, "scalar register must be a0.0<0;1,0>:ud\n");
		  }

		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  set_instruction_predicate(&$$, &$1);

		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
                      YYERROR;

                  memset(&src0, 0, sizeof(src0));
                  src0.reg.address_mode = BRW_ADDRESS_DIRECT;

                  if (IS_GENp(7)) {
                      src0.reg.file = BRW_GENERAL_REGISTER_FILE;
                      src0.reg.type = BRW_REGISTER_TYPE_UB;
                  } else {
                      src0.reg.file = BRW_MESSAGE_REGISTER_FILE;
                      src0.reg.type = BRW_REGISTER_TYPE_D;
                  }

                  src0.reg.nr = $5.nr;
                  src0.reg.subnr = 0;
                  set_instruction_src0(&$$, &src0, NULL);

                  set_instruction_src1(&$$, &$7, &@7);

                  if (IS_GENp(8)) {
                      gen8_set_sfid(GEN8(&$$), $6 & EX_DESC_SFID_MASK);
                      gen8_set_eot(GEN8(&$$), !!($6 & EX_DESC_EOT_MASK));
		  } else {
                      GEN(&$$)->header.destreg__conditionalmod = ($6 & EX_DESC_SFID_MASK); /* SFID */
                      GEN(&$$)->bits3.generic_gen5.end_of_thread = !!($6 & EX_DESC_EOT_MASK);
                  }
		}
		| predicate sendop execsize dst sendleadreg payload sndopr imm32reg instoptions
		{
		  if (IS_GENp(6))
                      error(&@2, "invalid syntax for send on gen6+\n");

		  if ($8.reg.type != BRW_REGISTER_TYPE_UD &&
		      $8.reg.type != BRW_REGISTER_TYPE_D &&
		      $8.reg.type != BRW_REGISTER_TYPE_V) {
		    error(&@8, "non-int D/UD/V representation: %d,"
			  "type=%d\n", $8.reg.dw1.ud, $8.reg.type);
		  }
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  GEN(&$$)->header.destreg__conditionalmod = $5.nr; /* msg reg index */

		  set_instruction_predicate(&$$, &$1);
		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$6, &@6) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$8, &@8) != 0)
		    YYERROR;

		  if (IS_GENx(5)) {
		      GEN(&$$)->bits2.send_gen5.sfid = ($7 & EX_DESC_SFID_MASK);
		      GEN(&$$)->bits3.generic_gen5.end_of_thread = !!($7 & EX_DESC_EOT_MASK);
		  }
		}
		| predicate sendop execsize dst sendleadreg payload exp directsrcoperand instoptions
		{
		  if (IS_GENp(6))
                      error(&@2, "invalid syntax for send on gen6+\n");

		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  GEN(&$$)->header.destreg__conditionalmod = $5.nr; /* msg reg index */

		  set_instruction_predicate(&$$, &$1);

		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$6, &@6) != 0)
		    YYERROR;
		  /* XXX is this correct? */
		  if (set_instruction_src1(&$$, &$8, &@8) != 0)
		    YYERROR;
		  if (IS_GENx(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid = $7;
		  }
		}
		
;

sndopr: exp %prec SNDOPR
		{
			$$ = $1;
		}
;

jumpinstruction: predicate JMPI execsize relativelocation2
		{
		  /* The jump instruction requires that the IP register
		   * be the destination and first source operand, while the
		   * offset is the second source operand.  The next instruction
		   * is the post-incremented IP plus the offset.
		   */
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  if(advanced_flag) {
                      if (IS_GENp(8))
                          gen8_set_mask_control(GEN8(&$$), BRW_MASK_DISABLE);
                      else
                          GEN(&$$)->header.mask_control = BRW_MASK_DISABLE;
		  }
		  set_instruction_predicate(&$$, &$1);
		  ip_dst.width = BRW_WIDTH_1;
		  set_instruction_dest(&$$, &ip_dst);
		  set_instruction_src0(&$$, &ip_src, NULL);
		  set_instruction_src1(&$$, &$4, NULL);
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		}
;

mathinstruction: predicate MATH_INST execsize dst src srcimm math_function instoptions
		{
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);

		  if (IS_GENp(8))
                      gen8_set_math_function(GEN8(&$$), $7);
		  else
                      GEN(&$$)->header.destreg__conditionalmod = $7;

		  set_instruction_options(&$$, $8);
		  set_instruction_predicate(&$$, &$1);
		  $4.width = $3;
		  if (set_instruction_dest(&$$, &$4) != 0)
		    YYERROR;
		  if (set_instruction_src0(&$$, &$5, &@5) != 0)
		    YYERROR;
		  if (set_instruction_src1(&$$, &$6, &@6) != 0)
		    YYERROR;
		}
;

breakinstruction: predicate breakop execsize relativelocation relativelocation instoptions
		{
		  // for Gen6, Gen7
		  memset(&$$, 0, sizeof($$));
		  set_instruction_predicate(&$$, &$1);
		  set_instruction_opcode(&$$, $2);
		  set_execsize(&$$, $3);
		  $$.reloc.first_reloc_target = $4.reloc_target;
		  $$.reloc.first_reloc_offset = $4.imm32;
		  $$.reloc.second_reloc_target = $5.reloc_target;
		  $$.reloc.second_reloc_offset = $5.imm32;
		}
;

breakop:	BREAK | CONT
;

/*
maskpushop:	MSAVE | PUSH
;
 */

syncinstruction: predicate WAIT notifyreg
		{
		  struct brw_reg notify_dst;
		  struct src_operand notify_src;

		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $2);
		  set_direct_dst_operand(&notify_dst, &$3, BRW_REGISTER_TYPE_D);
		  notify_dst.width = BRW_WIDTH_1;
		  set_instruction_dest(&$$, &notify_dst);
		  set_direct_src_operand(&notify_src, &$3, BRW_REGISTER_TYPE_D);
		  set_instruction_src0(&$$, &notify_src, NULL);
		  set_instruction_src1(&$$, &src_null_reg, NULL);
		}
		
;

nopinstruction: NOP
		{
		  memset(&$$, 0, sizeof($$));
		  set_instruction_opcode(&$$, $1);
		};

/* XXX! */
payload: directsrcoperand
;

post_dst:	dst
;

msgtarget:	NULL_TOKEN
		{
		  if (IS_GENp(8)) {
		      gen8_set_sfid(GEN8(&$$), BRW_SFID_NULL);
		      gen8_set_header_present(GEN8(&$$), 0);
		  } else if (IS_GENp(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid= BRW_SFID_NULL;
                      GEN(&$$)->bits3.generic_gen5.header_present = 0;  /* ??? */
		  } else {
                      GEN(&$$)->bits3.generic.msg_target = BRW_SFID_NULL;
		  }
		}
		| SAMPLER LPAREN INTEGER COMMA INTEGER COMMA
		sampler_datatype RPAREN
		{
		  if (IS_GENp(8)) {
		      gen8_set_sfid(GEN8(&$$), BRW_SFID_SAMPLER);
		      gen8_set_header_present(GEN8(&$$), 1); /* ??? */
		      gen8_set_binding_table_index(GEN8(&$$), $3);
		      gen8_set_sampler(GEN8(&$$), $5);
		      gen8_set_sampler_simd_mode(GEN8(&$$), 2); /* SIMD16 */
		  } else if (IS_GENp(7)) {
                      GEN(&$$)->bits2.send_gen5.sfid = BRW_SFID_SAMPLER;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;   /* ??? */
                      GEN(&$$)->bits3.sampler_gen7.binding_table_index = $3;
                      GEN(&$$)->bits3.sampler_gen7.sampler = $5;
                      GEN(&$$)->bits3.sampler_gen7.simd_mode = 2; /* SIMD16, maybe we should add a new parameter */
		  } else if (IS_GENp(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid = BRW_SFID_SAMPLER;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;   /* ??? */
                      GEN(&$$)->bits3.sampler_gen5.binding_table_index = $3;
                      GEN(&$$)->bits3.sampler_gen5.sampler = $5;
                      GEN(&$$)->bits3.sampler_gen5.simd_mode = 2; /* SIMD16, maybe we should add a new parameter */
		  } else {
                      GEN(&$$)->bits3.generic.msg_target = BRW_SFID_SAMPLER;
                      GEN(&$$)->bits3.sampler.binding_table_index = $3;
                      GEN(&$$)->bits3.sampler.sampler = $5;
                      switch ($7) {
                      case TYPE_F:
                          GEN(&$$)->bits3.sampler.return_format =
                              BRW_SAMPLER_RETURN_FORMAT_FLOAT32;
                          break;
                      case TYPE_UD:
                          GEN(&$$)->bits3.sampler.return_format =
                              BRW_SAMPLER_RETURN_FORMAT_UINT32;
                          break;
                      case TYPE_D:
                          GEN(&$$)->bits3.sampler.return_format =
                              BRW_SAMPLER_RETURN_FORMAT_SINT32;
                          break;
                      }
		  }
		}
		| MATH math_function saturate math_signed math_scalar
		{
		  if (IS_GENp(6)) {
                      error (&@1, "Gen6+ doesn't have math function\n");
		  } else if (IS_GENx(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid = BRW_SFID_MATH;
                      GEN(&$$)->bits3.generic_gen5.header_present = 0;
                      GEN(&$$)->bits3.math_gen5.function = $2;
		      set_instruction_saturate(&$$, $3);
                      GEN(&$$)->bits3.math_gen5.int_type = $4;
                      GEN(&$$)->bits3.math_gen5.precision = BRW_MATH_PRECISION_FULL;
                      GEN(&$$)->bits3.math_gen5.data_type = $5;
		  } else {
                      GEN(&$$)->bits3.generic.msg_target = BRW_SFID_MATH;
                      GEN(&$$)->bits3.math.function = $2;
		      set_instruction_saturate(&$$, $3);
                      GEN(&$$)->bits3.math.int_type = $4;
                      GEN(&$$)->bits3.math.precision = BRW_MATH_PRECISION_FULL;
                      GEN(&$$)->bits3.math.data_type = $5;
		  }
		}
		| GATEWAY
		{
		  if (IS_GENp(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid = BRW_SFID_MESSAGE_GATEWAY;
                      GEN(&$$)->bits3.generic_gen5.header_present = 0;  /* ??? */
		  } else {
                      GEN(&$$)->bits3.generic.msg_target = BRW_SFID_MESSAGE_GATEWAY;
		  }
		}
		| READ  LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA
                INTEGER RPAREN
		{
		  if (IS_GENp(8)) {
                      gen8_set_sfid(GEN8(&$$), GEN6_SFID_DATAPORT_SAMPLER_CACHE);
                      gen8_set_header_present(GEN8(&$$), 1);
                      gen8_set_dp_binding_table_index(GEN8(&$$), $3);
                      gen8_set_dp_message_control(GEN8(&$$), $7);
                      gen8_set_dp_message_type(GEN8(&$$), $9);
                      gen8_set_dp_category(GEN8(&$$), 0);
		  } else if (IS_GENx(7)) {
                      GEN(&$$)->bits2.send_gen5.sfid =
                          GEN6_SFID_DATAPORT_SAMPLER_CACHE;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
                      GEN(&$$)->bits3.gen7_dp.binding_table_index = $3;
                      GEN(&$$)->bits3.gen7_dp.msg_control = $7;
                      GEN(&$$)->bits3.gen7_dp.msg_type = $9;
		  } else if (IS_GENx(6)) {
                      GEN(&$$)->bits2.send_gen5.sfid =
                          GEN6_SFID_DATAPORT_SAMPLER_CACHE;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
                      GEN(&$$)->bits3.gen6_dp_sampler_const_cache.binding_table_index = $3;
                      GEN(&$$)->bits3.gen6_dp_sampler_const_cache.msg_control = $7;
                      GEN(&$$)->bits3.gen6_dp_sampler_const_cache.msg_type = $9;
		  } else if (IS_GENx(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid =
                          BRW_SFID_DATAPORT_READ;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
                      GEN(&$$)->bits3.dp_read_gen5.binding_table_index = $3;
                      GEN(&$$)->bits3.dp_read_gen5.target_cache = $5;
                      GEN(&$$)->bits3.dp_read_gen5.msg_control = $7;
                      GEN(&$$)->bits3.dp_read_gen5.msg_type = $9;
		  } else {
                      GEN(&$$)->bits3.generic.msg_target =
                          BRW_SFID_DATAPORT_READ;
                      GEN(&$$)->bits3.dp_read.binding_table_index = $3;
                      GEN(&$$)->bits3.dp_read.target_cache = $5;
                      GEN(&$$)->bits3.dp_read.msg_control = $7;
                      GEN(&$$)->bits3.dp_read.msg_type = $9;
		  }
		}
		| WRITE LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA
		INTEGER RPAREN
		{
		  if (IS_GENp(8)) {
                      if ($9 != 0 &&
			  $9 != GEN6_SFID_DATAPORT_RENDER_CACHE &&
			  $9 != GEN7_SFID_DATAPORT_DATA_CACHE &&
			  $9 != HSW_SFID_DATAPORT_DATA_CACHE1) {
			  error (&@9, "error: wrong cache type\n");
		      }

		      if ($9 == 0)
			  gen8_set_sfid(GEN8(&$$), GEN6_SFID_DATAPORT_RENDER_CACHE);
		      else
			  gen8_set_sfid(GEN8(&$$), $9);

                      gen8_set_header_present(GEN8(&$$), 1);
                      gen8_set_dp_binding_table_index(GEN8(&$$), $3);
                      gen8_set_dp_message_control(GEN8(&$$), $5);
                      gen8_set_dp_message_type(GEN8(&$$), $7);
                      gen8_set_dp_category(GEN8(&$$), 0);
		  } else if (IS_GENx(7)) {
                      GEN(&$$)->bits2.send_gen5.sfid = GEN6_SFID_DATAPORT_RENDER_CACHE;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
                      GEN(&$$)->bits3.gen7_dp.binding_table_index = $3;
                      GEN(&$$)->bits3.gen7_dp.msg_control = $5;
                      GEN(&$$)->bits3.gen7_dp.msg_type = $7;
                  } else if (IS_GENx(6)) {
                      GEN(&$$)->bits2.send_gen5.sfid = GEN6_SFID_DATAPORT_RENDER_CACHE;
                      /* Sandybridge supports headerlesss message for render target write.
                       * Currently the GFX assembler doesn't support it. so the program must provide 
                       * message header
                       */
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
                      GEN(&$$)->bits3.gen6_dp.binding_table_index = $3;
                      GEN(&$$)->bits3.gen6_dp.msg_control = $5;
                     GEN(&$$)->bits3.gen6_dp.msg_type = $7;
                      GEN(&$$)->bits3.gen6_dp.send_commit_msg = $9;
		  } else if (IS_GENx(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid =
                          BRW_SFID_DATAPORT_WRITE;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
                      GEN(&$$)->bits3.dp_write_gen5.binding_table_index = $3;
                      GEN(&$$)->bits3.dp_write_gen5.last_render_target = ($5 & 0x8) >> 3;
                      GEN(&$$)->bits3.dp_write_gen5.msg_control = $5 & 0x7;
                      GEN(&$$)->bits3.dp_write_gen5.msg_type = $7;
                      GEN(&$$)->bits3.dp_write_gen5.send_commit_msg = $9;
		  } else {
                      GEN(&$$)->bits3.generic.msg_target =
                          BRW_SFID_DATAPORT_WRITE;
                      GEN(&$$)->bits3.dp_write.binding_table_index = $3;
                      /* The msg control field of brw_struct.h is split into
                       * msg control and last_render_target, even though
                       * last_render_target isn't common to all write messages.
                       */
                      GEN(&$$)->bits3.dp_write.last_render_target = ($5 & 0x8) >> 3;
                      GEN(&$$)->bits3.dp_write.msg_control = $5 & 0x7;
                      GEN(&$$)->bits3.dp_write.msg_type = $7;
                      GEN(&$$)->bits3.dp_write.send_commit_msg = $9;
		  }
		}
		| WRITE LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA
		INTEGER COMMA INTEGER RPAREN
		{
		  if (IS_GENp(8)) {
                      if ($9 != 0 &&
			  $9 != GEN6_SFID_DATAPORT_RENDER_CACHE &&
			  $9 != GEN7_SFID_DATAPORT_DATA_CACHE &&
			  $9 != HSW_SFID_DATAPORT_DATA_CACHE1) {
			  error (&@9, "error: wrong cache type\n");
		      }

		      if ($9 == 0)
			  gen8_set_sfid(GEN8(&$$), GEN6_SFID_DATAPORT_RENDER_CACHE);
		      else
			  gen8_set_sfid(GEN8(&$$), $9);

                      gen8_set_header_present(GEN8(&$$), ($11 != 0));
                      gen8_set_dp_binding_table_index(GEN8(&$$), $3);
                      gen8_set_dp_message_control(GEN8(&$$), $5);
                      gen8_set_dp_message_type(GEN8(&$$), $7);
                      gen8_set_dp_category(GEN8(&$$), 0);
		  } else if (IS_GENx(7)) {
                      GEN(&$$)->bits2.send_gen5.sfid = GEN6_SFID_DATAPORT_RENDER_CACHE;
                      GEN(&$$)->bits3.generic_gen5.header_present = ($11 != 0);
                      GEN(&$$)->bits3.gen7_dp.binding_table_index = $3;
                      GEN(&$$)->bits3.gen7_dp.msg_control = $5;
                      GEN(&$$)->bits3.gen7_dp.msg_type = $7;
		  } else if (IS_GENx(6)) {
                      GEN(&$$)->bits2.send_gen5.sfid = GEN6_SFID_DATAPORT_RENDER_CACHE;
                      GEN(&$$)->bits3.generic_gen5.header_present = ($11 != 0);
                      GEN(&$$)->bits3.gen6_dp.binding_table_index = $3;
                      GEN(&$$)->bits3.gen6_dp.msg_control = $5;
                     GEN(&$$)->bits3.gen6_dp.msg_type = $7;
                      GEN(&$$)->bits3.gen6_dp.send_commit_msg = $9;
		  } else if (IS_GENx(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid =
                          BRW_SFID_DATAPORT_WRITE;
                      GEN(&$$)->bits3.generic_gen5.header_present = ($11 != 0);
                      GEN(&$$)->bits3.dp_write_gen5.binding_table_index = $3;
                      GEN(&$$)->bits3.dp_write_gen5.last_render_target = ($5 & 0x8) >> 3;
                      GEN(&$$)->bits3.dp_write_gen5.msg_control = $5 & 0x7;
                      GEN(&$$)->bits3.dp_write_gen5.msg_type = $7;
                      GEN(&$$)->bits3.dp_write_gen5.send_commit_msg = $9;
		  } else {
                      GEN(&$$)->bits3.generic.msg_target =
                          BRW_SFID_DATAPORT_WRITE;
                      GEN(&$$)->bits3.dp_write.binding_table_index = $3;
                      /* The msg control field of brw_struct.h is split into
                       * msg control and last_render_target, even though
                       * last_render_target isn't common to all write messages.
                       */
                      GEN(&$$)->bits3.dp_write.last_render_target = ($5 & 0x8) >> 3;
                      GEN(&$$)->bits3.dp_write.msg_control = $5 & 0x7;
                      GEN(&$$)->bits3.dp_write.msg_type = $7;
                      GEN(&$$)->bits3.dp_write.send_commit_msg = $9;
		  }
		}
		| URB INTEGER urb_swizzle urb_allocate urb_used urb_complete
		{
		  GEN(&$$)->bits3.generic.msg_target = BRW_SFID_URB;
		  if (IS_GENp(5)) {
                      GEN(&$$)->bits2.send_gen5.sfid = BRW_SFID_URB;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
		      set_instruction_opcode(&$$, BRW_URB_OPCODE_WRITE);
                      GEN(&$$)->bits3.urb_gen5.offset = $2;
                      GEN(&$$)->bits3.urb_gen5.swizzle_control = $3;
                      GEN(&$$)->bits3.urb_gen5.pad = 0;
                      GEN(&$$)->bits3.urb_gen5.allocate = $4;
                      GEN(&$$)->bits3.urb_gen5.used = $5;
                      GEN(&$$)->bits3.urb_gen5.complete = $6;
		  } else {
                      GEN(&$$)->bits3.generic.msg_target = BRW_SFID_URB;
		      set_instruction_opcode(&$$, BRW_URB_OPCODE_WRITE);
                      GEN(&$$)->bits3.urb.offset = $2;
                      GEN(&$$)->bits3.urb.swizzle_control = $3;
                      GEN(&$$)->bits3.urb.pad = 0;
                      GEN(&$$)->bits3.urb.allocate = $4;
                      GEN(&$$)->bits3.urb.used = $5;
                      GEN(&$$)->bits3.urb.complete = $6;
		  }
		}
		| THREAD_SPAWNER  LPAREN INTEGER COMMA INTEGER COMMA
                        INTEGER RPAREN
		{
		  if (IS_GENp(8)) {
                      gen8_set_sfid(GEN8(&$$), BRW_SFID_THREAD_SPAWNER);
                      gen8_set_header_present(GEN8(&$$), 0); /* Must be 0 */
                      gen8_set_ts_opcode(GEN8(&$$), $3);
                      gen8_set_ts_request_type(GEN8(&$$), $5);
                      gen8_set_ts_resource_select(GEN8(&$$), $7);
		  } else {
                      GEN(&$$)->bits3.generic.msg_target =
                          BRW_SFID_THREAD_SPAWNER;
                      if (IS_GENp(5)) {
                          GEN(&$$)->bits2.send_gen5.sfid =
                              BRW_SFID_THREAD_SPAWNER;
                          GEN(&$$)->bits3.generic_gen5.header_present = 0;
                          GEN(&$$)->bits3.thread_spawner_gen5.opcode = $3;
                          GEN(&$$)->bits3.thread_spawner_gen5.requester_type  = $5;
                          GEN(&$$)->bits3.thread_spawner_gen5.resource_select = $7;
                      } else {
                          GEN(&$$)->bits3.generic.msg_target =
                              BRW_SFID_THREAD_SPAWNER;
                          GEN(&$$)->bits3.thread_spawner.opcode = $3;
                          GEN(&$$)->bits3.thread_spawner.requester_type  = $5;
                          GEN(&$$)->bits3.thread_spawner.resource_select = $7;
                      }
		  }
		}
		| VME  LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA INTEGER RPAREN
		{
		  GEN(&$$)->bits3.generic.msg_target = GEN6_SFID_VME;

		  if (IS_GENp(8)) {
                      gen8_set_sfid(GEN8(&$$), GEN6_SFID_VME);
                      gen8_set_header_present(GEN8(&$$), 1); /* Must be 1 */
                      gen8_set_vme_binding_table_index(GEN8(&$$), $3);
                      gen8_set_vme_message_type(GEN8(&$$), $9);
		  } else if (IS_GENp(6)) {
                      GEN(&$$)->bits2.send_gen5.sfid = GEN6_SFID_VME;
                      GEN(&$$)->bits3.vme_gen6.binding_table_index = $3;
                      GEN(&$$)->bits3.vme_gen6.search_path_index = $5;
                      GEN(&$$)->bits3.vme_gen6.lut_subindex = $7;
                      GEN(&$$)->bits3.vme_gen6.message_type = $9;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
		  } else {
                      error (&@1, "Gen6- doesn't have vme function\n");
		  }    
		} 
		| CRE LPAREN INTEGER COMMA INTEGER RPAREN
		{
		  if (IS_GENp(8)) {
                      gen8_set_sfid(GEN8(&$$), HSW_SFID_CRE);
                      gen8_set_header_present(GEN8(&$$), 1); /* Must be 1 */
                      gen8_set_cre_binding_table_index(GEN8(&$$), $3);
                      gen8_set_cre_message_type(GEN8(&$$), $5);
		  } else {
                      if (gen_level < 75)
                          error (&@1, "Below Gen7.5 doesn't have CRE function\n");

                      GEN(&$$)->bits3.generic.msg_target = HSW_SFID_CRE;

                      GEN(&$$)->bits2.send_gen5.sfid = HSW_SFID_CRE;
                      GEN(&$$)->bits3.cre_gen75.binding_table_index = $3;
                      GEN(&$$)->bits3.cre_gen75.message_type = $5;
                      GEN(&$$)->bits3.generic_gen5.header_present = 1;
		  }
		}

		| DATA_PORT LPAREN INTEGER COMMA INTEGER COMMA INTEGER COMMA 
                INTEGER COMMA INTEGER COMMA INTEGER RPAREN
		{
		  if (IS_GENp(8)) {
                      if ($3 != GEN6_SFID_DATAPORT_SAMPLER_CACHE &&
                          $3 != GEN6_SFID_DATAPORT_RENDER_CACHE &&
                          $3 != GEN6_SFID_DATAPORT_CONSTANT_CACHE &&
                          $3 != GEN7_SFID_DATAPORT_DATA_CACHE &&
                          $3 != HSW_SFID_DATAPORT_DATA_CACHE1) {
                          error (&@3, "error: wrong cache type\n");
                      }

                      gen8_set_sfid(GEN8(&$$), $3);
                      gen8_set_header_present(GEN8(&$$), ($13 != 0));
                      gen8_set_dp_binding_table_index(GEN8(&$$), $9);
                      gen8_set_dp_message_control(GEN8(&$$), $7);
                      gen8_set_dp_message_type(GEN8(&$$), $5);
                      gen8_set_dp_category(GEN8(&$$), $11);
		  } else {
                      GEN(&$$)->bits2.send_gen5.sfid = $3;
                      GEN(&$$)->bits3.generic_gen5.header_present = ($13 != 0);

                      if (IS_GENp(7)) {
                          if ($3 != GEN6_SFID_DATAPORT_SAMPLER_CACHE &&
                              $3 != GEN6_SFID_DATAPORT_RENDER_CACHE &&
                              $3 != GEN6_SFID_DATAPORT_CONSTANT_CACHE &&
                              $3 != GEN7_SFID_DATAPORT_DATA_CACHE) {
                              error (&@3, "error: wrong cache type\n");
                          }

                          GEN(&$$)->bits3.gen7_dp.category = $11;
                          GEN(&$$)->bits3.gen7_dp.binding_table_index = $9;
                          GEN(&$$)->bits3.gen7_dp.msg_control = $7;
                          GEN(&$$)->bits3.gen7_dp.msg_type = $5;
                      } else if (IS_GENx(6)) {
                          if ($3 != GEN6_SFID_DATAPORT_SAMPLER_CACHE &&
                              $3 != GEN6_SFID_DATAPORT_RENDER_CACHE &&
                              $3 != GEN6_SFID_DATAPORT_CONSTANT_CACHE) {
                              error (&@3, "error: wrong cache type\n");
                          }

                          GEN(&$$)->bits3.gen6_dp.send_commit_msg = $11;
                          GEN(&$$)->bits3.gen6_dp.binding_table_index = $9;
                          GEN(&$$)->bits3.gen6_dp.msg_control = $7;
                          GEN(&$$)->bits3.gen6_dp.msg_type = $5;
                      } else if (!IS_GENp(5)) {
                          error (&@1, "Gen6- doesn't support data port for sampler/render/constant/data cache\n");
                      }
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

dstoperand:	symbol_reg dstregion
		{
		  $$ = $1.reg;
	          $$.hstride = resolve_dst_region(&$1, $2);
		}
		| dstreg dstregion writemask regtype
		{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  $$ = $1;
	          $$.hstride = resolve_dst_region(NULL, $2);
		  $$.dw1.bits.writemask = $3.dw1.bits.writemask;
		  $$.type = $4.type;
		}
;

/* The dstoperandex returns an instruction with just the destination register
 * filled in.
 */
dstoperandex:	dstoperandex_typed dstregion regtype
		{
		  $$ = $1;
	          $$.hstride = resolve_dst_region(NULL, $2);
		  $$.type = $3.type;
		}
		| maskstackreg
		{
		  $$ = $1;
		  $$.hstride = 1;
		  $$.type = BRW_REGISTER_TYPE_UW;
		}
		| controlreg
		{
		  $$ = $1;
		  $$.hstride = 1;
		  $$.type = BRW_REGISTER_TYPE_UD;
		}
		| ipreg
		{
		  $$ = $1;
		  $$.hstride = 1;
		  $$.type = BRW_REGISTER_TYPE_UD;
		}
		| nullreg dstregion regtype
		{
		  $$ = $1;
	          $$.hstride = resolve_dst_region(NULL, $2);
		  $$.type = $3.type;
		}
;

dstoperandex_typed: accreg | flagreg | addrreg | maskreg
;

symbol_reg:	STRING %prec STR_SYMBOL_REG 
		{
		    struct declared_register *dcl_reg = find_register($1);

		    if (dcl_reg == NULL)
			error(&@1, "can't find register %s\n", $1);

		    memcpy(&$$, dcl_reg, sizeof(*dcl_reg));
		    free($1); // $1 has been malloc'ed by strdup
		}
		| symbol_reg_p 
		{
			$$=$1;
		}
;

symbol_reg_p: STRING LPAREN exp RPAREN 
		{
		    struct declared_register *dcl_reg = find_register($1);	

		    if (dcl_reg == NULL)
			error(&@1, "can't find register %s\n", $1);

		    memcpy(&$$, dcl_reg, sizeof(*dcl_reg));
		    $$.reg.nr += $3;
		    free($1);
		}
		| STRING LPAREN exp COMMA exp RPAREN
		{
		    struct declared_register *dcl_reg = find_register($1);	

		    if (dcl_reg == NULL)
			error(&@1, "can't find register %s\n", $1);

		    memcpy(&$$, dcl_reg, sizeof(*dcl_reg));
		    $$.reg.nr += $3;
		    if(advanced_flag) {
			int size = get_type_size(dcl_reg->reg.type);
		        $$.reg.nr += ($$.reg.subnr + $5) / (32 / size);
		        $$.reg.subnr = ($$.reg.subnr + $5) % (32 / size);
		    } else {
		        $$.reg.nr += ($$.reg.subnr + $5) / 32;
		        $$.reg.subnr = ($$.reg.subnr + $5) % 32;
		    }
		    free($1);
		}
;
/* Returns a partially complete destination register consisting of the
 * direct or indirect register addressing fields, but not stride or writemask.
 */
dstreg:		directgenreg
		{
		  $$ = $1;
		  $$.address_mode = BRW_ADDRESS_DIRECT;
		}
		| directmsgreg
		{
		  $$ = $1;
		  $$.address_mode = BRW_ADDRESS_DIRECT;
		}
		| indirectgenreg
		{
		  $$ = $1;
		  $$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		}
		| indirectmsgreg
		{
		  $$ = $1;
		  $$.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		}
;

/* 1.4.3: Source register */
srcaccimm:	srcacc | imm32reg
;

srcacc:		directsrcaccoperand | indirectsrcoperand
;

srcimm:		directsrcoperand | indirectsrcoperand| imm32reg
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
		  case BRW_REGISTER_TYPE_VF:
		    switch ($1.r) {
		    case imm32_d:
		      d = $1.u.d;
		      break;
		    default:
		      error (&@2, "non-int D/UD/V/VF representation: %d,type=%d\n", $1.r, $2);
		    }
		    break;
		  case BRW_REGISTER_TYPE_UW:
		  case BRW_REGISTER_TYPE_W:
		    switch ($1.r) {
		    case imm32_d:
		      d = $1.u.d;
		      break;
		    default:
		      error (&@2, "non-int W/UW representation\n");
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
		      error (&@2, "non-float F representation\n");
		    }
		    d = intfloat.i;
		    break;
#if 0
		  case BRW_REGISTER_TYPE_VF:
		    fprintf (stderr, "Immediate type VF not supported yet\n");
		    YYERROR;
#endif
		  default:
		    error(&@2, "unknown immediate type %d\n", $2);
		  }
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.file = BRW_IMMEDIATE_VALUE;
		  $$.reg.type = $2;
		  $$.reg.dw1.ud = d;
		}
;

directsrcaccoperand:	directsrcoperand
		| accreg region regtype
		{
		  set_direct_src_operand(&$$, &$1, $3.type);
		  $$.reg.vstride = $2.vert_stride;
		  $$.reg.width = $2.width;
		  $$.reg.hstride = $2.horiz_stride;
		  $$.default_region = $2.is_default;
		}
;

/* Returns a source operand in the src0 fields of an instruction. */
srcarchoperandex: srcarchoperandex_typed region regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.file = $1.file;
		  $$.reg.type = $3.type;
		  $$.reg.subnr = $1.subnr;
		  $$.reg.nr = $1.nr;
		  $$.reg.vstride = $2.vert_stride;
		  $$.reg.width = $2.width;
		  $$.reg.hstride = $2.horiz_stride;
		  $$.default_region = $2.is_default;
		  $$.reg.negate = 0;
		  $$.reg.abs = 0;
		}
		| maskstackreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UB);
		}
		| controlreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
/*		| statereg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}*/
		| notifyreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| ipreg
		{
		  set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		}
		| nullreg region regtype
		{
		  if ($3.is_default) {
		    set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		  } else {
		    set_direct_src_operand(&$$, &$1, $3.type);
		  }
		  $$.default_region = 1;
		}
;

srcarchoperandex_typed: flagreg | addrreg | maskreg
;

sendleadreg: symbol_reg
             {
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = $1.reg.file;
		  $$.nr = $1.reg.nr;
		  $$.subnr = $1.reg.subnr;
             }
             | directgenreg | directmsgreg
;

src:		directsrcoperand | indirectsrcoperand
;

directsrcoperand:	negate abs symbol_reg region regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.address_mode = BRW_ADDRESS_DIRECT;
		  $$.reg.file = $3.reg.file;
		  $$.reg.nr = $3.reg.nr;
		  $$.reg.subnr = $3.reg.subnr;
		  if ($5.is_default) {
		    $$.reg.type = $3.reg.type;
		  } else {
		    $$.reg.type = $5.type;
		  }
		  if ($4.is_default) {
		    $$.reg.vstride = $3.src_region.vert_stride;
		    $$.reg.width = $3.src_region.width;
		    $$.reg.hstride = $3.src_region.horiz_stride;
		  } else {
		    $$.reg.vstride = $4.vert_stride;
		    $$.reg.width = $4.width;
		    $$.reg.hstride = $4.horiz_stride;
		  }
		  $$.reg.negate = $1;
		  $$.reg.abs = $2;
		} 
		| statereg region regtype 
		{
		  if($2.is_default ==1 && $3.is_default == 1)
		  {
		    set_direct_src_operand(&$$, &$1, BRW_REGISTER_TYPE_UD);
		  }
		  else{
		    memset (&$$, '\0', sizeof ($$));
		    $$.reg.address_mode = BRW_ADDRESS_DIRECT;
		    $$.reg.file = $1.file;
		    $$.reg.nr = $1.nr;
		    $$.reg.subnr = $1.subnr;
		    $$.reg.vstride = $2.vert_stride;
		    $$.reg.width = $2.width;
		    $$.reg.hstride = $2.horiz_stride;
		    $$.reg.type = $3.type;
		  }
		}
		| negate abs directgenreg region swizzle regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.address_mode = BRW_ADDRESS_DIRECT;
		  $$.reg.file = $3.file;
		  $$.reg.nr = $3.nr;
		  $$.reg.subnr = $3.subnr;
		  $$.reg.type = $6.type;
		  $$.reg.vstride = $4.vert_stride;
		  $$.reg.width = $4.width;
		  $$.reg.hstride = $4.horiz_stride;
		  $$.default_region = $4.is_default;
		  $$.reg.negate = $1;
		  $$.reg.abs = $2;
		  $$.reg.dw1.bits.swizzle = $5.reg.dw1.bits.swizzle;
		}
		| srcarchoperandex
;

indirectsrcoperand:
		negate abs indirectgenreg indirectregion regtype swizzle
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		  $$.reg.file = $3.file;
		  $$.reg.subnr = $3.subnr;
		  $$.reg.dw1.bits.indirect_offset = $3.dw1.bits.indirect_offset;
		  $$.reg.type = $5.type;
		  $$.reg.vstride = $4.vert_stride;
		  $$.reg.width = $4.width;
		  $$.reg.hstride = $4.horiz_stride;
		  $$.reg.negate = $1;
		  $$.reg.abs = $2;
		  $$.reg.dw1.bits.swizzle = $6.reg.dw1.bits.swizzle;
		}
;

/* 1.4.4: Address Registers */
/* Returns a partially-completed struct brw_reg consisting of the address
 * register fields for register-indirect access.
 */
addrparam:	addrreg COMMA immaddroffset
		{
		  if ($3 < -512 || $3 > 511)
		    error(&@3, "Address immediate offset %d out of range\n", $3);
		  memset (&$$, '\0', sizeof ($$));
		  $$.subnr = $1.subnr;
		  $$.dw1.bits.indirect_offset = $3;
		}
		| addrreg 
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.subnr = $1.subnr;
		  $$.dw1.bits.indirect_offset = 0;
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
		|  %prec SUBREGNUM
		{
		  /* Default to subreg 0 if unspecified. */
		  $$ = 0;
		}
;

directgenreg:	GENREG subregnum
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_GENERAL_REGISTER_FILE;
		  $$.nr = $1;
		  $$.subnr = $2;
		}
;

indirectgenreg: GENREGFILE LSQUARE addrparam RSQUARE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_GENERAL_REGISTER_FILE;
		  $$.subnr = $3.subnr;
		  $$.dw1.bits.indirect_offset = $3.dw1.bits.indirect_offset;
		}
;

directmsgreg:	MSGREG subregnum
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_MESSAGE_REGISTER_FILE;
		  $$.nr = $1;
		  $$.subnr = $2;
		}
;

indirectmsgreg: MSGREGFILE LSQUARE addrparam RSQUARE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_MESSAGE_REGISTER_FILE;
		  $$.subnr = $3.subnr;
		  $$.dw1.bits.indirect_offset = $3.dw1.bits.indirect_offset;
		}
;

addrreg:	ADDRESSREG subregnum
		{
		  if ($1 != 0)
		    error(&@2, "address register number %d out of range", $1);

		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_ADDRESS | $1;
		  $$.subnr = $2;
		}
;

accreg:		ACCREG subregnum
		{
		  if ($1 > 1)
		    error(&@1, "accumulator register number %d out of range", $1);
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_ACCUMULATOR | $1;
		  $$.subnr = $2;
		}
;

flagreg:	FLAGREG subregnum
		{
		  if ((!IS_GENp(7) && $1 > 0) ||
		      (IS_GENp(7) && $1 > 1)) {
                    error(&@2, "flag register number %d out of range\n", $1);
		  }

		  if ($2 > 1)
		    error(&@2, "flag subregister number %d out of range\n", $1);

		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_FLAG | $1;
		  $$.subnr = $2;
		}
;

maskreg:	MASKREG subregnum
		{
		  if ($1 > 0)
		    error(&@1, "mask register number %d out of range", $1);

		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_MASK;
		  $$.subnr = $2;
		}
		| mask_subreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_MASK;
		  $$.subnr = $1;
		}
;

mask_subreg:	AMASK | IMASK | LMASK | CMASK
;

maskstackreg:	MASKSTACKREG subregnum
		{
		  if ($1 > 0)
		    error(&@1, "mask stack register number %d out of range", $1);
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_MASK_STACK;
		  $$.subnr = $2;
		}
		| maskstack_subreg
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_MASK_STACK;
		  $$.subnr = $1;
		}
;

maskstack_subreg: IMS | LMS
;

/*
maskstackdepthreg: MASKSTACKDEPTHREG subregnum
		{
		  if ($1 > 0)
		    error(&@1, "mask stack register number %d out of range", $1);
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

notifyreg:	NOTIFYREG regtype
		{
		  int num_notifyreg = (IS_GENp(6)) ? 3 : 2;

		  if ($1 > num_notifyreg)
		    error(&@1, "notification register number %d out of range",
			  $1);

		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;

                  if (IS_GENp(6)) {
		    $$.nr = BRW_ARF_NOTIFICATION_COUNT;
                    $$.subnr = $1;
                  } else {
		    $$.nr = BRW_ARF_NOTIFICATION_COUNT | $1;
                    $$.subnr = 0;
                  }
		}
/*
		| NOTIFYREG regtype
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
*/
;

statereg:	STATEREG subregnum
		{
		  if ($1 > 0)
		    error(&@1, "state register number %d out of range", $1);

		  if ($2 > 1)
		    error(&@2, "state subregister number %d out of range", $1);

		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_STATE | $1;
		  $$.subnr = $2;
		}
;

controlreg:	CONTROLREG subregnum
		{
		  if ($1 > 0)
		    error(&@1, "control register number %d out of range", $1);

		  if ($2 > 2)
		    error(&@2, "control subregister number %d out of range", $1);
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_CONTROL | $1;
		  $$.subnr = $2;
		}
;

ipreg:		IPREG regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_IP;
		  $$.subnr = 0;
		}
;

nullreg:	NULL_TOKEN
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.file = BRW_ARCHITECTURE_REGISTER_FILE;
		  $$.nr = BRW_ARF_NULL;
		  $$.subnr = 0;
		}
;

/* 1.4.6: Relative locations */
relativelocation:
		simple_int
		{
		  if (($1 > 32767) || ($1 < -32768))
		    error(&@1, "error: relative offset %d out of range \n", $1);

		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.file = BRW_IMMEDIATE_VALUE;
		  $$.reg.type = BRW_REGISTER_TYPE_D;
		  $$.imm32 = $1 & 0x0000ffff;
		}
		| STRING
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.file = BRW_IMMEDIATE_VALUE;
		  $$.reg.type = BRW_REGISTER_TYPE_D;
		  $$.reloc_target = $1;
		}
;

relativelocation2:
		  STRING
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.file = BRW_IMMEDIATE_VALUE;
		  $$.reg.type = BRW_REGISTER_TYPE_D;
		  $$.reloc_target = $1;
		}
		| exp
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.file = BRW_IMMEDIATE_VALUE;
		  $$.reg.type = BRW_REGISTER_TYPE_D;
		  $$.imm32 = $1;
		}
		| directgenreg region regtype
		{
		  set_direct_src_operand(&$$, &$1, $3.type);
		  $$.reg.vstride = $2.vert_stride;
		  $$.reg.width = $2.width;
		  $$.reg.hstride = $2.horiz_stride;
		  $$.default_region = $2.is_default;
		}
		| symbol_reg_p
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.address_mode = BRW_ADDRESS_DIRECT;
		  $$.reg.file = $1.reg.file;
		  $$.reg.nr = $1.reg.nr;
		  $$.reg.subnr = $1.reg.subnr;
		  $$.reg.type = $1.reg.type;
		  $$.reg.vstride = $1.src_region.vert_stride;
		  $$.reg.width = $1.src_region.width;
		  $$.reg.hstride = $1.src_region.horiz_stride;
		}
		| indirectgenreg indirectregion regtype
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.reg.address_mode = BRW_ADDRESS_REGISTER_INDIRECT_REGISTER;
		  $$.reg.file = $1.file;
		  $$.reg.subnr = $1.subnr;
		  $$.reg.dw1.bits.indirect_offset = $1.dw1.bits.indirect_offset;
		  $$.reg.type = $3.type;
		  $$.reg.vstride = $2.vert_stride;
		  $$.reg.width = $2.width;
		  $$.reg.hstride = $2.horiz_stride;
		}
;

/* 1.4.7: Regions */
dstregion:	/* empty */
		{
		  $$ = DEFAULT_DSTREGION;
		}
		|LANGLE exp RANGLE
		{
		  /* Returns a value for a horiz_stride field of an
		   * instruction.
		   */
		  if ($2 != 1 && $2 != 2 && $2 != 4)
		    error(&@2, "Invalid horiz size %d\n", $2);

		  $$ = ffs($2);
		}
;

region:		/* empty */
		{
		  /* XXX is this default value correct?*/
		  memset (&$$, '\0', sizeof ($$));
		  $$.vert_stride = ffs(0);
		  $$.width = BRW_WIDTH_1;
		  $$.horiz_stride = ffs(0);
		  $$.is_default = 1;
		}
		|LANGLE exp RANGLE
		{
		  /* XXX is this default value correct for accreg?*/
		  memset (&$$, '\0', sizeof ($$));
		  $$.vert_stride = ffs($2);
		  $$.width = BRW_WIDTH_1;
		  $$.horiz_stride = ffs(0);
		}
		|LANGLE exp COMMA exp COMMA exp RANGLE
		{
		  memset (&$$, '\0', sizeof ($$));
		  $$.vert_stride = ffs($2);
		  $$.width = ffs($4) - 1;
		  $$.horiz_stride = ffs($6);
		}
		| LANGLE exp SEMICOLON exp COMMA exp RANGLE
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
regtype:	/* empty */
		{ $$.type = program_defaults.register_type;$$.is_default = 1;}
		| TYPE_F { $$.type = BRW_REGISTER_TYPE_F;$$.is_default = 0; }
		| TYPE_UD { $$.type = BRW_REGISTER_TYPE_UD;$$.is_default = 0; }
		| TYPE_D { $$.type = BRW_REGISTER_TYPE_D;$$.is_default = 0; }
		| TYPE_UW { $$.type = BRW_REGISTER_TYPE_UW;$$.is_default = 0; }
		| TYPE_W { $$.type = BRW_REGISTER_TYPE_W;$$.is_default = 0; }
		| TYPE_UB { $$.type = BRW_REGISTER_TYPE_UB;$$.is_default = 0; }
		| TYPE_B { $$.type = BRW_REGISTER_TYPE_B;$$.is_default = 0; }
;

srcimmtype:	/* empty */
		{
		    /* XXX change to default when pragma parse is done */
		   $$ = BRW_REGISTER_TYPE_D;
		}
		|TYPE_F { $$ = BRW_REGISTER_TYPE_F; }
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
		  $$.reg.dw1.bits.swizzle = BRW_SWIZZLE_NOOP;
		}
		| DOT chansel
		{
		  $$.reg.dw1.bits.swizzle = BRW_SWIZZLE4($2, $2, $2, $2);
		}
		| DOT chansel chansel chansel chansel
		{
		  $$.reg.dw1.bits.swizzle = BRW_SWIZZLE4($2, $3, $4, $5);
		}
;

chansel:	X | Y | Z | W
;

/* 1.4.9: Write mask */
/* Returns a partially completed struct brw_reg, with just the writemask bits
 * filled out.
 */
writemask:	/* empty */
		{
		  $$.dw1.bits.writemask = BRW_WRITEMASK_XYZW;
		}
		| DOT writemask_x writemask_y writemask_z writemask_w
		{
		  $$.dw1.bits.writemask = $2 | $3 | $4 | $5;
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
		| NUMBER { $$.r = imm32_f; $$.u.f = $1; }
;

/* 1.4.12: Predication and modifiers */
predicate:	/* empty */
		{
		  $$.pred_control = BRW_PREDICATE_NONE;
		  $$.flag_reg_nr = 0;
		  $$.flag_subreg_nr = 0;
		  $$.pred_inverse = 0;
		}
		| LPAREN predstate flagreg predctrl RPAREN
		{
		  $$.pred_control = $4;
		  $$.flag_reg_nr = $3.nr;
		  $$.flag_subreg_nr = $3.subnr;
		  $$.pred_inverse = $2;
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

execsize:	/* empty */ %prec EMPTEXECSIZE
		{
		  $$ = ffs(program_defaults.execute_size) - 1;
		}
		|LPAREN exp RPAREN
		{
		  /* Returns a value for the execution_size field of an
		   * instruction.
		   */
		  if ($2 != 1 && $2 != 2 && $2 != 4 && $2 != 8 && $2 != 16 &&
		      $2 != 32)
		    error(&@2, "Invalid execution size %d\n", $2);

		  $$ = ffs($2) - 1;
		}
;

saturate:	/* empty */ { $$ = BRW_INSTRUCTION_NORMAL; }
		| SATURATE { $$ = BRW_INSTRUCTION_SATURATE; }
;
conditionalmodifier: condition 
		{
		    $$.cond = $1;
		    $$.flag_reg_nr = 0;
		    $$.flag_subreg_nr = -1;
		}
		| condition DOT flagreg
		{
		    $$.cond = $1;
		    $$.flag_reg_nr = ($3.nr & 0xF);
		    $$.flag_subreg_nr = $3.subnr;
		}

condition: /* empty */    { $$ = BRW_CONDITIONAL_NONE; }
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
instoptions:	/* empty */
		{ memset(&$$, 0, sizeof($$)); }
		| LCURLY instoption_list RCURLY
		{ $$ = $2; }
;

instoption_list:instoption_list COMMA instoption
		{
		  $$ = $1;
		  add_option(&$$, $3);
		}
		| instoption_list instoption
		{
		  $$ = $1;
		  add_option(&$$, $2);
		}
		| /* empty, header defaults to zeroes. */
		{
		  memset(&$$, 0, sizeof($$));
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
		| ACCWRCTRL { $$ = ACCWRCTRL; }
		| EOT { $$ = EOT; }
;

%%
extern int yylineno;

void yyerror (char *msg)
{
	fprintf(stderr, "%s: %d: %s at \"%s\"\n",
		input_filename, yylineno, msg, lex_text());
	++errors;
}

static int get_type_size(unsigned type)
{
    int size = 1;

    switch (type) {
    case BRW_REGISTER_TYPE_F:
    case BRW_REGISTER_TYPE_UD:
    case BRW_REGISTER_TYPE_D:
        size = 4;
        break;

    case BRW_REGISTER_TYPE_UW:
    case BRW_REGISTER_TYPE_W:
        size = 2;
        break;

    case BRW_REGISTER_TYPE_UB:
    case BRW_REGISTER_TYPE_B:
        size = 1;
        break;

    default:
        assert(0);
        size = 1;
        break;
    }

    return size;
}

static void reset_instruction_src_region(struct brw_instruction *instr, 
                                         struct src_operand *src)
{
    if (IS_GENp(8))
	return;

    if (!src->default_region)
        return;

    if (src->reg.file == BRW_ARCHITECTURE_REGISTER_FILE && 
        ((src->reg.nr & 0xF0) == BRW_ARF_ADDRESS)) {
        src->reg.vstride = ffs(0);
        src->reg.width = BRW_WIDTH_1;
        src->reg.hstride = ffs(0);
    } else if (src->reg.file == BRW_ARCHITECTURE_REGISTER_FILE &&
               ((src->reg.nr & 0xF0) == BRW_ARF_ACCUMULATOR)) {
        int horiz_stride = 1, width, vert_stride;
        if (instr->header.compression_control == BRW_COMPRESSION_COMPRESSED) {
            width = 16;
        } else {
            width = 8;
        }

        if (width > (1 << instr->header.execution_size))
            width = (1 << instr->header.execution_size);

        vert_stride = horiz_stride * width;
        src->reg.vstride = ffs(vert_stride);
        src->reg.width = ffs(width) - 1;
        src->reg.hstride = ffs(horiz_stride);
    } else if ((src->reg.file == BRW_ARCHITECTURE_REGISTER_FILE) &&
               (src->reg.nr == BRW_ARF_NULL) &&
               (instr->header.opcode == BRW_OPCODE_SEND)) {
        src->reg.vstride = ffs(8);
        src->reg.width = BRW_WIDTH_8;
        src->reg.hstride = ffs(1);
    } else {

        int horiz_stride = 1, width, vert_stride;

        if (instr->header.execution_size == 0) { /* scalar */
            horiz_stride = 0;
            width = 1;
            vert_stride = 0;
        } else {
            if ((instr->header.opcode == BRW_OPCODE_MUL) ||
                (instr->header.opcode == BRW_OPCODE_MAC) ||
                (instr->header.opcode == BRW_OPCODE_CMP) ||
                (instr->header.opcode == BRW_OPCODE_ASR) ||
                (instr->header.opcode == BRW_OPCODE_ADD) ||
				(instr->header.opcode == BRW_OPCODE_SHL)) {
                horiz_stride = 0;
                width = 1;
                vert_stride = 0;
            } else {
                width = (1 << instr->header.execution_size) / horiz_stride;
                vert_stride = horiz_stride * width;

                if (get_type_size(src->reg.type) * (width + src->reg.subnr) > 32) {
                    horiz_stride = 0;
                    width = 1;
                    vert_stride = 0;
                }
            }
        }

        src->reg.vstride = ffs(vert_stride);
        src->reg.width = ffs(width) - 1;
        src->reg.hstride = ffs(horiz_stride);
    }
}

static void set_instruction_opcode(struct brw_program_instruction *instr,
				  unsigned opcode)
{
    if (IS_GENp(8))
	gen8_set_opcode(GEN8(instr), opcode);
    else
	GEN(instr)->header.opcode = opcode;
}

/**
 * Fills in the destination register information in instr from the bits in dst.
 */
static int set_instruction_dest(struct brw_program_instruction *instr,
				struct brw_reg *dest)
{
	if (!validate_dst_reg(instr, dest))
		return 1;

	/* the assembler support expressing subnr in bytes or in number of
	 * elements. */
	resolve_subnr(dest);

	if (IS_GENp(8)) {
		gen8_set_exec_size(GEN8(instr), dest->width);
		gen8_set_dst(GEN8(instr), *dest);
	} else {
		brw_set_dest(&genasm_compile, GEN(instr), *dest);
	}

	return 0;
}

/* Sets the first source operand for the instruction.  Returns 0 on success. */
static int set_instruction_src0(struct brw_program_instruction *instr,
				struct src_operand *src,
				YYLTYPE *location)
{

	if (advanced_flag)
		reset_instruction_src_region(GEN(instr), src);

	if (!validate_src_reg(instr, src->reg, location))
		return 1;

	/* the assembler support expressing subnr in bytes or in number of
	 * elements. */
	resolve_subnr(&src->reg);

	if (IS_GENp(8))
		gen8_set_src0(GEN8(instr), src->reg);
	else
		brw_set_src0(&genasm_compile, GEN(instr), src->reg);

	return 0;
}

/* Sets the second source operand for the instruction.  Returns 0 on success.
 */
static int set_instruction_src1(struct brw_program_instruction *instr,
				struct src_operand *src,
				YYLTYPE *location)
{
	if (advanced_flag)
		reset_instruction_src_region(GEN(instr), src);

	if (!validate_src_reg(instr, src->reg, location))
		return 1;

	/* the assembler support expressing subnr in bytes or in number of
	 * elements. */
	resolve_subnr(&src->reg);

	if (IS_GENp(8))
		gen8_set_src1(GEN8(instr), src->reg);
	else
		brw_set_src1(&genasm_compile, GEN(instr), src->reg);

	return 0;
}

static int set_instruction_dest_three_src(struct brw_program_instruction *instr,
					  struct brw_reg *dest)
{
    resolve_subnr(dest);
    brw_set_3src_dest(&genasm_compile, GEN(instr), *dest);
    return 0;
}

static int set_instruction_src0_three_src(struct brw_program_instruction *instr,
					  struct src_operand *src)
{
    if (advanced_flag)
	reset_instruction_src_region(GEN(instr), src);

    resolve_subnr(&src->reg);

    // TODO: src0 modifier, src0 rep_ctrl
    brw_set_3src_src0(&genasm_compile, GEN(instr), src->reg);
    return 0;
}

static int set_instruction_src1_three_src(struct brw_program_instruction *instr,
					  struct src_operand *src)
{
    if (advanced_flag)
	reset_instruction_src_region(GEN(instr), src);

    resolve_subnr(&src->reg);

    // TODO: src1 modifier, src1 rep_ctrl
    brw_set_3src_src1(&genasm_compile, GEN(instr), src->reg);
    return 0;
}

static int set_instruction_src2_three_src(struct brw_program_instruction *instr,
					  struct src_operand *src)
{
    if (advanced_flag)
	reset_instruction_src_region(GEN(instr), src);

    resolve_subnr(&src->reg);

    // TODO: src2 modifier, src2 rep_ctrl
    brw_set_3src_src2(&genasm_compile, GEN(instr), src->reg);
    return 0;
}

static void set_instruction_saturate(struct brw_program_instruction *instr,
				     int saturate)
{
    if (IS_GENp(8))
	gen8_set_saturate(GEN8(instr), saturate);
    else
	GEN(instr)->header.saturate = saturate;
}

static void set_instruction_options(struct brw_program_instruction *instr,
				    struct options options)
{
    if (IS_GENp(8)) {
	gen8_set_access_mode(GEN8(instr), options.access_mode);
	gen8_set_thread_control(GEN8(instr), options.thread_control);
	gen8_set_dep_control(GEN8(instr), options.dependency_control);
	gen8_set_mask_control(GEN8(instr), options.mask_control);
	gen8_set_debug_control(GEN8(instr), options.debug_control);
	gen8_set_acc_wr_control(GEN8(instr), options.acc_wr_control);
	gen8_set_eot(GEN8(instr), options.end_of_thread);
    } else {
	GEN(instr)->header.access_mode = options.access_mode;
	GEN(instr)->header.compression_control = options.compression_control;
	GEN(instr)->header.thread_control = options.thread_control;
	GEN(instr)->header.dependency_control = options.dependency_control;
	GEN(instr)->header.mask_control = options.mask_control;
	GEN(instr)->header.debug_control = options.debug_control;
	GEN(instr)->header.acc_wr_control = options.acc_wr_control;
	GEN(instr)->bits3.generic.end_of_thread = options.end_of_thread;
    }
}

static void set_instruction_predicate(struct brw_program_instruction *instr,
				      struct predicate *p)
{
    if (IS_GENp(8)) {
	gen8_set_pred_control(GEN8(instr), p->pred_control);
	gen8_set_pred_inv(GEN8(instr), p->pred_inverse);
	gen8_set_flag_reg_nr(GEN8(instr), p->flag_reg_nr);
	gen8_set_flag_subreg_nr(GEN8(instr), p->flag_subreg_nr);
    } else {
	GEN(instr)->header.predicate_control = p->pred_control;
	GEN(instr)->header.predicate_inverse = p->pred_inverse;
	GEN(instr)->bits2.da1.flag_reg_nr = p->flag_reg_nr;
	GEN(instr)->bits2.da1.flag_subreg_nr = p->flag_subreg_nr;
    }
}

static void set_instruction_pred_cond(struct brw_program_instruction *instr,
				      struct predicate *p,
				      struct condition *c,
				      YYLTYPE *location)
{
    set_instruction_predicate(instr, p);

    if (IS_GENp(8))
	gen8_set_cond_modifier(GEN8(instr), c->cond);
    else
	GEN(instr)->header.destreg__conditionalmod = c->cond;

    if (c->flag_subreg_nr == -1)
	return;

    if (p->pred_control != BRW_PREDICATE_NONE &&
	(p->flag_reg_nr != c->flag_reg_nr ||
	 p->flag_subreg_nr != c->flag_subreg_nr))
    {
	warn(ALWAYS, location, "must use the same flag register if both "
	     "prediction and conditional modifier are enabled\n");
    }

    if (IS_GENp(8)) {
	gen8_set_flag_reg_nr(GEN8(instr), c->flag_reg_nr);
	gen8_set_flag_subreg_nr(GEN8(instr), c->flag_subreg_nr);
    } else {
	GEN(instr)->bits2.da1.flag_reg_nr = c->flag_reg_nr;
	GEN(instr)->bits2.da1.flag_subreg_nr = c->flag_subreg_nr;
    }
}

static void set_direct_dst_operand(struct brw_reg *dst, struct brw_reg *reg,
				   int type)
{
	*dst = *reg;
	dst->address_mode = BRW_ADDRESS_DIRECT;
	dst->type = type;
	dst->hstride = 1;
	dst->dw1.bits.writemask = BRW_WRITEMASK_XYZW;
}

static void set_direct_src_operand(struct src_operand *src, struct brw_reg *reg,
				   int type)
{
	memset(src, 0, sizeof(*src));
	src->reg.address_mode = BRW_ADDRESS_DIRECT;
	src->reg.file = reg->file;
	src->reg.type = type;
	src->reg.subnr = reg->subnr;
	src->reg.nr = reg->nr;
	src->reg.vstride = 0;
	src->reg.width = 0;
	src->reg.hstride = 0;
	src->reg.negate = 0;
	src->reg.abs = 0;
	SWIZZLE(src->reg) = BRW_SWIZZLE_NOOP;
}

static inline int instruction_opcode(struct brw_program_instruction *insn)
{
    if (IS_GENp(8))
	return gen8_opcode(GEN8(insn));
    else
	return GEN(insn)->header.opcode;
}

/*
 * return the offset used in native flow control (branch) instructions
 */
static inline int branch_offset(struct brw_program_instruction *insn, int offset)
{
    /*
     * bspec: Unlike other flow control instructions, the offset used by JMPI
     * is relative to the incremented instruction pointer rather than the IP
     * value for the instruction itself.
     */
    if (instruction_opcode(insn) == BRW_OPCODE_JMPI)
        offset--;

    /*
     * Gen4- bspec: the jump distance is in number of sixteen-byte units
     * Gen5+ bspec: the jump distance is in number of eight-byte units
     * Gen7.5+: the offset is in unit of 8bits for JMPI, 64bits for other flow
     * control instructions
     */
    if (gen_level >= 75 &&
        (instruction_opcode(insn) == BRW_OPCODE_JMPI))
        offset *= 16;
    else if (gen_level >= 50)
        offset *= 2;

    return offset;
}

void set_branch_two_offsets(struct brw_program_instruction *insn, int jip_offset, int uip_offset)
{
    int jip = branch_offset(insn, jip_offset);
    int uip = branch_offset(insn, uip_offset);

    assert(instruction_opcode(insn) != BRW_OPCODE_JMPI);

    if (IS_GENp(8)) {
        gen8_set_jip(GEN8(insn), jip);
	gen8_set_uip(GEN8(insn), uip);
    } else {
        GEN(insn)->bits3.break_cont.jip = jip;
        GEN(insn)->bits3.break_cont.uip = uip;
    }
}

void set_branch_one_offset(struct brw_program_instruction *insn, int jip_offset)
{
    int jip = branch_offset(insn, jip_offset);

    if (IS_GENp(8)) {
        gen8_set_jip(GEN8(insn), jip);
    } else if (IS_GENx(7)) {
        /* Gen7 JMPI Restrictions in bspec:
         * The JIP data type must be Signed DWord
         */
        if (instruction_opcode(insn) == BRW_OPCODE_JMPI)
            GEN(insn)->bits3.JIP = jip;
        else
            GEN(insn)->bits3.break_cont.jip = jip;
    } else if (IS_GENx(6)) {
        if ((instruction_opcode(insn) == BRW_OPCODE_CALL) ||
            (instruction_opcode(insn) == BRW_OPCODE_JMPI))
            GEN(insn)->bits3.JIP = jip;
        else
            GEN(insn)->bits1.branch_gen6.jump_count = jip; // for CASE,ELSE,FORK,IF,WHILE
    } else {
        GEN(insn)->bits3.JIP = jip;

        if (instruction_opcode(insn) == BRW_OPCODE_ELSE)
            GEN(insn)->bits3.break_cont.uip = 1; // Set the istack pop count, which must always be 1.
    }
}
