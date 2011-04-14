%option yylineno
%{
#include <string.h>
#include "gen4asm.h"
#include "gram.h"
#include "brw_defines.h"

#include "string.h"
int saved_state = 0;
extern char *input_filename;
extern long int gen_level;

%}
%x BLOCK_COMMENT
%x CHANNEL
%x LINENUMBER
%x FILENAME

%%
\/\/.*[\r\n] { } /* eat up single-line comments */
"\.kernel".*[\r\n] { }
"\.end_kernel".*[\r\n] { }
"\.code".*[\r\n] { }
"\.end_code".*[\r\n] { }

 /* eat up multi-line comments, non-nesting. */
\/\* {
	saved_state = YYSTATE;
	BEGIN(BLOCK_COMMENT);
}
<BLOCK_COMMENT>\*\/ {
	BEGIN(saved_state);
}
<BLOCK_COMMENT>. { }
<BLOCK_COMMENT>[\r\n] { }
"#line"" "* { 
	saved_state = YYSTATE;
	BEGIN(LINENUMBER);
}
<LINENUMBER>[0-9]+" "* {
	yylineno = atoi (yytext) - 1;
	BEGIN(FILENAME);
}
<FILENAME>\"[^\"]+\" {
	char *name = malloc (yyleng - 1);
	memmove (name, yytext + 1, yyleng - 2);
	name[yyleng-1] = '\0';
	input_filename = name;
	BEGIN(saved_state);
}

<CHANNEL>"x" {
	yylval.integer = BRW_CHANNEL_X;
	return X;
}
<CHANNEL>"y" {
	yylval.integer = BRW_CHANNEL_Y;
	return Y;
}
<CHANNEL>"z" {
	yylval.integer = BRW_CHANNEL_Z;
	return Z;
}
<CHANNEL>"w" {
yylval.integer = BRW_CHANNEL_W;
	return W;
}
<CHANNEL>. {
	yyless(0);
	BEGIN(INITIAL);
}

 /* used for both null send and null register. */
"null" { return NULL_TOKEN; }

 /* opcodes */
"mov" { yylval.integer = BRW_OPCODE_MOV; return MOV; }
"frc" { yylval.integer = BRW_OPCODE_FRC; return FRC; }
"rndu" { yylval.integer = BRW_OPCODE_RNDU; return RNDU; }
"rndd" { yylval.integer = BRW_OPCODE_RNDD; return RNDD; }
"rnde" { yylval.integer = BRW_OPCODE_RNDE; return RNDE; }
"rndz" { yylval.integer = BRW_OPCODE_RNDZ; return RNDZ; }
"not" { yylval.integer = BRW_OPCODE_NOT; return NOT; }
"lzd" { yylval.integer = BRW_OPCODE_LZD; return LZD; }

"mul" { yylval.integer = BRW_OPCODE_MUL; return MUL; }
"mac" { yylval.integer = BRW_OPCODE_MAC; return MAC; }
"mach" { yylval.integer = BRW_OPCODE_MACH; return MACH; }
"line" { yylval.integer = BRW_OPCODE_LINE; return LINE; }
"sad2" { yylval.integer = BRW_OPCODE_SAD2; return SAD2; }
"sada2" { yylval.integer = BRW_OPCODE_SADA2; return SADA2; }
"dp4" { yylval.integer = BRW_OPCODE_DP4; return DP4; }
"dph" { yylval.integer = BRW_OPCODE_DPH; return DPH; }
"dp3" { yylval.integer = BRW_OPCODE_DP3; return DP3; }
"dp2" { yylval.integer = BRW_OPCODE_DP2; return DP2; }

"avg" { yylval.integer = BRW_OPCODE_AVG; return AVG; }
"add" { yylval.integer = BRW_OPCODE_ADD; return ADD; }
"sel" { yylval.integer = BRW_OPCODE_SEL; return SEL; }
"and" { yylval.integer = BRW_OPCODE_AND; return AND; }
"or" { yylval.integer = BRW_OPCODE_OR; return OR; }
"xor" { yylval.integer = BRW_OPCODE_XOR; return XOR; }
"shr" { yylval.integer = BRW_OPCODE_SHR; return SHR; }
"shl" { yylval.integer = BRW_OPCODE_SHL; return SHL; }
"asr" { yylval.integer = BRW_OPCODE_ASR; return ASR; }
"cmp" { yylval.integer = BRW_OPCODE_CMP; return CMP; }
"cmpn" { yylval.integer = BRW_OPCODE_CMPN; return CMPN; }

"send" { yylval.integer = BRW_OPCODE_SEND; return SEND; }
"nop" { yylval.integer = BRW_OPCODE_NOP; return NOP; }
"jmpi" { yylval.integer = BRW_OPCODE_JMPI; return JMPI; }
"if" { yylval.integer = BRW_OPCODE_IF; return IF; }
"iff" { yylval.integer = BRW_OPCODE_IFF; return IFF; }
"while" { yylval.integer = BRW_OPCODE_WHILE; return WHILE; }
"else" { yylval.integer = BRW_OPCODE_ELSE; return ELSE; }
"break" { yylval.integer = BRW_OPCODE_BREAK; return BREAK; }
"cont" { yylval.integer = BRW_OPCODE_CONTINUE; return CONT; }
"halt" { yylval.integer = BRW_OPCODE_HALT; return HALT; }
"msave" { yylval.integer = BRW_OPCODE_MSAVE; return MSAVE; }
"push" { yylval.integer = BRW_OPCODE_PUSH; return PUSH; }
"mrest" { yylval.integer = BRW_OPCODE_MRESTORE; return MREST; }
"pop" { yylval.integer = BRW_OPCODE_POP; return POP; }
"wait" { yylval.integer = BRW_OPCODE_WAIT; return WAIT; }
"do" { yylval.integer = BRW_OPCODE_DO; return DO; }
"endif" { yylval.integer = BRW_OPCODE_ENDIF; return ENDIF; }

"pln" { yylval.integer = BRW_OPCODE_PLN; return PLN; }

 /* send argument tokens */
"mlen" { return MSGLEN; }
"rlen" { return RETURNLEN; }
"math" { if (gen_level >= 6) { yylval.integer = BRW_OPCODE_MATH; return MATH_INST; } else return MATH; }
"sampler" { return SAMPLER; }
"gateway" { return GATEWAY; }
"read" { return READ; }
"write" { return WRITE; }
"urb" { return URB; }
"thread_spawner" { return THREAD_SPAWNER; }
"vme" { return VME; }

"allocate" { return ALLOCATE; }
"used" { return USED; }
"complete" { return COMPLETE; }
"transpose" { return TRANSPOSE; }
"interleave" { return INTERLEAVE; }

";" { return SEMICOLON; }
"(" { return LPAREN; }
")" { return RPAREN; }
"<" { return LANGLE; }
">" { return RANGLE; }
"{" { return LCURLY; }
"}" { return RCURLY; }
"[" { return LSQUARE; }
"]" { return RSQUARE; }
"," { return COMMA; }
"." { BEGIN(CHANNEL); return DOT; }
"+" { return PLUS; }
"-" { return MINUS; }
"*" { return MULTIPLY;}
"/" { return DIVIDE; }
":" { return COLON; }
"=" { return EQ; }
"(abs)" { return ABS; }

 /* Most register accesses are lexed as REGFILE[0-9]+, to prevent the register
  * with subreg from being lexed as REGFILE NUMBER instead of
  * REGISTER INTEGER DOT INTEGER like we want.  The alternative was to use a
  * start condition, which wasn't very clean-looking.
  *
  * However, this means we need to lex the general and message register file
  * characters as well, for register-indirect access which is formatted
  * like g[a#.#] or m[a#.#].
  */
"acc"[0-9]+ {
	yylval.integer = atoi(yytext + 3);
	return ACCREG;
}
"a"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return ADDRESSREG;
}
"m"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return MSGREG;
}
"m" {
	return MSGREGFILE;
}
"mask"[0-9]+ {
	yylval.integer = atoi(yytext + 4);
	return MASKREG;
}
"ms"[0-9]+ {
	yylval.integer = atoi(yytext + 2);
	return MASKSTACKREG;
}
"msd"[0-9]+ {
	yylval.integer = atoi(yytext + 3);
	return MASKSTACKDEPTHREG;
}

"n0."[0-9]+ {
	yylval.integer = atoi(yytext + 3);
	return NOTIFYREG;
}

"n"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return NOTIFYREG;
}
 /* Unlike other registers, flagreg returns the subreg number in the lvalue
  * rather than the reg number, to avoid a shift/reduce conflict in the
  * predicate control.
  */
"f0."[0-9]+ {
	yylval.integer = atoi(yytext + 3);
	return FLAGREG;
}
"f0" {
	yylval.integer = 0;
	return FLAGREG;
}
[gr][0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return GENREG;
}
[gr] {
	return GENREGFILE;
}
"cr"[0-9]+ {
	yylval.integer = atoi(yytext + 2);
	return CONTROLREG;
}
"sr"[0-9]+ {
	yylval.integer = atoi(yytext + 2);
	return STATEREG;
}
"ip" {
	return IPREG;
}
"amask" {
	yylval.integer = BRW_AMASK;
	return AMASK;
}
"imask" {
	yylval.integer = BRW_IMASK;
	return IMASK;
}
"lmask" {
	yylval.integer = BRW_LMASK;
	return LMASK;
}
"cmask" {
	yylval.integer = BRW_CMASK;
	return CMASK;
}
"imsd" {
	yylval.integer = 0;
	return IMSD;
}
"lmsd" {
	yylval.integer = 1;
	return LMSD;
}
"ims" {
	yylval.integer = 0;
	return IMS;
}
"lms" {
	yylval.integer = 16;
	return LMS;
}

 /*
  * Lexing of register types should probably require the ":" symbol specified
  * in the BNF of the assembly, but our existing source didn't use that syntax.
  */
"UD" { return TYPE_UD; }
":UD" { return TYPE_UD; }
"D" { return TYPE_D; }
":D" { return TYPE_D; }
"UW" { return TYPE_UW; }
":UW" { return TYPE_UW; }
"W" { return TYPE_W; }
":W" { return TYPE_W; }
"UB" { return TYPE_UB; }
":UB" { return TYPE_UB; }
"B" { return TYPE_B; }
":B" { return TYPE_B; }
"F" { return TYPE_F; }
":F" { return TYPE_F; }
"VF" {return TYPE_VF; }
":VF" {return TYPE_VF; }
"V" { return TYPE_V; }
":V" { return TYPE_V; }

#".kernel" { return KERNEL_PRAGMA;}
#".end_kernel" { return END_KERNEL_PRAGMA;}
#".code" { return CODE_PRAGMA;}
#".end_code" { return END_CODE_PRAGMA;}
".reg_count_payload" { return REG_COUNT_PAYLOAD_PRAGMA; }
".reg_count_total" { return REG_COUNT_TOTAL_PRAGMA; }
".default_execution_size" { return DEFAULT_EXEC_SIZE_PRAGMA; }
".default_register_type" { return DEFAULT_REG_TYPE_PRAGMA; }
".declare" { return DECLARE_PRAGMA; }
"Base" { return BASE; }
"ElementSize" { return ELEMENTSIZE; }
"SrcRegion" { return SRCREGION; }
"DstRegion" { return DSTREGION; }
"Type" { return TYPE; }


".sat" { return SATURATE; }
"align1" { return ALIGN1; }
"align16" { return ALIGN16; }
"sechalf" { return SECHALF; }
"compr" { return COMPR; }
"switch" { return SWITCH; }
"atomic" { return ATOMIC; }
"noddchk" { return NODDCHK; }
"noddclr" { return NODDCLR; }
"mask_disable" { return MASK_DISABLE; }
"nomask" { return MASK_DISABLE; }
"breakpoint" { return BREAKPOINT; }
"accwrctrl" { return ACCWRCTRL; }
"EOT" { return EOT; }

 /* extended math functions */
"inv" { yylval.integer = BRW_MATH_FUNCTION_INV; return SIN; }
"log" { yylval.integer = BRW_MATH_FUNCTION_LOG; return LOG; }
"exp" { yylval.integer = BRW_MATH_FUNCTION_EXP; return EXP; }
"sqrt" { yylval.integer = BRW_MATH_FUNCTION_SQRT; return SQRT; }
"rsq" { yylval.integer = BRW_MATH_FUNCTION_RSQ; return RSQ; }
"pow" { yylval.integer = BRW_MATH_FUNCTION_POW; return POW; }
"sin" { yylval.integer = BRW_MATH_FUNCTION_SIN; return SIN; }
"cos" { yylval.integer = BRW_MATH_FUNCTION_COS; return COS; }
"sincos" { yylval.integer = BRW_MATH_FUNCTION_SINCOS; return SINCOS; }
"intdiv" {
	yylval.integer = BRW_MATH_FUNCTION_INT_DIV_QUOTIENT;
	return INTDIV;
}
"intmod" {
	yylval.integer = BRW_MATH_FUNCTION_INT_DIV_REMAINDER;
	return INTMOD;
}
"intdivmod" {
	yylval.integer = BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER;
	return INTDIVMOD;
}

"signed" { return SIGNED; }
"scalar" { return SCALAR; }

 /* predicate control */
".anyv" { return ANYV; }
".allv" { return ALLV; }
".any2h" { return ANY2H; }
".all2h" { return ALL2H; }
".any4h" { return ANY4H; }
".all4h" { return ALL4H; }
".any8h" { return ANY8H; }
".all8h" { return ALL8H; }
".any16h" { return ANY16H; }
".all16h" { return ALL16H; }

".z" { yylval.integer = BRW_CONDITIONAL_Z; return ZERO; }
".e" { yylval.integer = BRW_CONDITIONAL_Z; return EQUAL; }
".nz" { yylval.integer = BRW_CONDITIONAL_NZ; return NOT_ZERO; }
".ne" { yylval.integer = BRW_CONDITIONAL_NZ; return NOT_EQUAL; }
".g" { yylval.integer = BRW_CONDITIONAL_G; return GREATER; }
".ge" { yylval.integer = BRW_CONDITIONAL_GE; return GREATER_EQUAL; }
".l" { yylval.integer = BRW_CONDITIONAL_L; return LESS; }
".le" { yylval.integer = BRW_CONDITIONAL_LE; return LESS_EQUAL; }
".r" { yylval.integer = BRW_CONDITIONAL_R; return ROUND_INCREMENT; }
".o" { yylval.integer = BRW_CONDITIONAL_O; return OVERFLOW; }
".u" { yylval.integer = BRW_CONDITIONAL_U; return UNORDERED; }

[a-zA-Z_][0-9a-zA-Z_]* {
           yylval.string = strdup(yytext);
           return STRING;
}

0x[0-9a-fA-F][0-9a-fA-F]* {
	yylval.integer = strtoul(yytext + 2, NULL, 16);
	return INTEGER;
}
[0-9][0-9]* {
	yylval.integer = strtoul(yytext, NULL, 10);
	return INTEGER;
}

<INITIAL>[-]?[0-9]+"."[0-9]+ {
	yylval.number = strtod(yytext, NULL);
	return NUMBER;
}

[ \t\n]+ { } /* eat up whitespace */

. {
	fprintf(stderr, "%s: %d: %s at \"%s\"\n",
		input_filename, yylineno, "unexpected token", lex_text());
  }
%%

char *
lex_text(void)
{
	return yytext;
  (void) yyunput;
}

#ifndef yywrap
int yywrap() { return 1; }
#endif

