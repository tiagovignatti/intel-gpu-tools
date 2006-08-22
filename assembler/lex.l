%option yylineno
%{
#include "gen4asm.h"
#include "y.tab.h"
%}
%start IN_REG

%%
\/\/.*[\r\n] { } /* eat up single-line comments */

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

"send" { return SEND; }
"mlen" { return MSGLEN; }
"rlen" { return RETURNLEN; }
"null" { return NULL_TOKEN; }
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
"m" { BEGIN(IN_REG); return MSGREGFILE; }
[gr] { BEGIN(IN_REG); return GENREGFILE; }

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

"align1" { return ALIGN1; }

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
yywrap() { return 1; }
#endif

