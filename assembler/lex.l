%option yylineno
%{
#include "gen4asm.h"
#include "y.tab.h"
#include "brw_defines.h"

int saved_state = INITIAL;

%}
%x BLOCK_COMMENT

%%
\/\/.*[\r\n] { } /* eat up single-line comments */

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
"while" { yylval.integer = BRW_OPCODE_NOP; return NOP; }
"send" { yylval.integer = BRW_OPCODE_SEND; return SEND; }
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

 /* send argument tokens */
"mlen" { return MSGLEN; }
"rlen" { return RETURNLEN; }
"math" { return MATH; }
"sampler" { return SAMPLER; }
"gateway" { return GATEWAY; }
"read" { return READ; }
"write" { return WRITE; }
"urb" { return URB; }
"thread_spawner" { return THREAD_SPAWNER; }

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
"," { return COMMA; }
"." { return DOT; }
"-" { return MINUS; }
"(abs)" { return ABS; }

"acc"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
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
"mask"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return MASKREG;
}
"ms"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return MASKSTACKREG;
}
"msd"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return MASKSTACKDEPTHREG;
}
"n"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return NOTIFYREG;
}
"f"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return FLAGREG;
}
[gr][0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return GENREG;
}
"cr"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
	return CONTROLREG;
}
"sr"[0-9]+ {
	yylval.integer = atoi(yytext + 1);
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
"D" { return TYPE_D; }
"UW" { return TYPE_UW; }
"W" { return TYPE_W; }
"UB" { return TYPE_UB; }
"B" { return TYPE_B; }
"F" { return TYPE_F; }

"sat" { return SATURATE; }
"align1" { return ALIGN1; }
"align16" { return ALIGN16; }
"mask_disable" { return MASK_DISABLE; }
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

[0-9]* {
	yylval.integer = atoi(yytext);
	return INTEGER;
}

<INITIAL>[-]?[0-9]+"."[0-9]+ {
	yylval.number = strtod(yytext, NULL);
	return NUMBER;
}

[ \t\n]+ { } /* eat up whitespace */

. {
	printf("parse error at line %d: unexpected \"%s\"\n",
	       yylineno, yytext);
	exit(1);
}
%%

char *
lex_text(void)
{
	return yytext;
}

#ifndef yywrap
int yywrap() { return 1; }
#endif

