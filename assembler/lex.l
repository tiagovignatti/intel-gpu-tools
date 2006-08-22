%option yylineno
%{
#include "gen4asm.h"
#include "y.tab.h"

int saved_state = INITIAL;

%}
%s IN_REG
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
"mov" { return MOV; }

"mul" { return MUL; }
"mac" { return MAC; }
"mach" { return MACH; }
"line" { return LINE; }
"sad2" { return SAD2; }
"sada2" { return SADA2; }
"dp4" { return DP4; }
"dph" { return DPH; }
"dp3" { return DP3; }
"dp2" { return DP2; }

"add" { return ADD; }

"nop" { return NOP; }

"send" { return SEND; }
"mlen" { return MSGLEN; }
"rlen" { return RETURNLEN; }
"math" { return MATH; }
"sampler" { return SAMPLER; }
"gateway" { return GATEWAY; }
"read" { return READ; }
"write" { return WRITE; }
"urb" { return URB; }
"thread_spawner" { return THREAD_SPAWNER; }

";" { return SEMICOLON; }
"(" { return LPAREN; }
")" { return RPAREN; }
"<" { return LANGLE; }
">" { return RANGLE; }
"{" { return LCURLY; }
"}" { return RCURLY; }
"," { return COMMA; }
"." { return DOT; }

 /* XXX: this lexing of register files is shady */
"acc" {
	BEGIN(IN_REG);
	return ACCREG;
}
"a" {
	BEGIN(IN_REG);
	return ADDRESSREG;
}
"m" {
	BEGIN(IN_REG);
	return MSGREG;
}
"f" {
	BEGIN(IN_REG);
	return FLAGREG;
}
[gr] {
	BEGIN(IN_REG);
	return GENREG;
}
"cr" {
	BEGIN(IN_REG);
	return CONTROLREG;
}
"ip" {
	BEGIN(IN_REG);
	return IPREG;
}

 /*
  * Lexing of register types should probably require the ":" symbol specified
  * in the BNF of the assembly, but our existing source didn't use that syntax.
  */
"UD" { BEGIN(INITIAL); return TYPE_UD; }
"D" { BEGIN(INITIAL); return TYPE_D; }
"UW" { BEGIN(INITIAL); return TYPE_UW; }
"W" { BEGIN(INITIAL); return TYPE_W; }
"UB" { BEGIN(INITIAL); return TYPE_UB; }
"B" { BEGIN(INITIAL); return TYPE_B; }
"F" { BEGIN(INITIAL); return TYPE_F; }

"sat" { return SATURATE; }
"align1" { return ALIGN1; }
"align16" { return ALIGN16; }
"mask_disable" { return MASK_DISABLE; }
"EOT" { return EOT; }

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

