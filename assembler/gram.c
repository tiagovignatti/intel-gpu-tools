#include <stdlib.h>
#ifndef lint
#ifdef __unused
__unused
#endif
static char const 
yyrcsid[] = "$FreeBSD: src/usr.bin/yacc/skeleton.c,v 1.37 2003/02/12 18:03:55 davidc Exp $";
#endif
#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define YYLEX yylex()
#define YYEMPTY -1
#define yyclearin (yychar=(YYEMPTY))
#define yyerrok (yyerrflag=0)
#define YYRECOVERING() (yyerrflag!=0)
#if defined(__cplusplus) || __STDC__
static int yygrowstack(void);
#else
static int yygrowstack();
#endif
#define YYPREFIX "yy"
#line 2 "gram.y"
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

#line 37 "gram.y"
typedef union {
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
} YYSTYPE;
#line 71 "y.tab.c"
#define YYERRCODE 256
#define SEMICOLON 257
#define LPAREN 258
#define RPAREN 259
#define LANGLE 260
#define RANGLE 261
#define LCURLY 262
#define RCURLY 263
#define COMMA 264
#define DOT 265
#define TYPE_UD 266
#define TYPE_D 267
#define TYPE_UW 268
#define TYPE_W 269
#define TYPE_UB 270
#define TYPE_B 271
#define TYPE_VF 272
#define TYPE_HF 273
#define TYPE_V 274
#define TYPE_F 275
#define ALIGN1 276
#define GENREGFILE 277
#define MSGREGFILE 278
#define MOV 279
#define MUL 280
#define MAC 281
#define MACH 282
#define LINE 283
#define SAD2 284
#define SADA2 285
#define DP4 286
#define DPH 287
#define DP3 288
#define DP2 289
#define ADD 290
#define SEND 291
#define NULL_TOKEN 292
#define MATH 293
#define SAMPLER 294
#define GATEWAY 295
#define READ 296
#define WRITE 297
#define URB 298
#define THREAD_SPAWNER 299
#define MSGLEN 300
#define RETURNLEN 301
#define INTEGER 302
#define NUMBER 303
#define accreg 304
#define triop 305
const short yylhs[] = {                                        -1,
    0,   16,   16,   16,    1,    1,    1,    1,    2,   17,
    3,   18,    4,   19,    5,    6,   34,   35,   26,   26,
   26,   26,   26,   26,   26,   26,   32,    7,    8,    9,
    9,   15,   15,   14,   13,   13,   10,   11,   12,   28,
   24,   24,   29,   25,   23,   27,   20,   20,   20,   20,
   20,   20,   20,   21,   30,   30,   31,   22,   33,   36,
};
const short yylen[] = {                                         2,
    1,    3,    2,    3,    1,    1,    1,    1,    6,    1,
    7,    1,    7,    1,    1,   12,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    3,    3,    1,
    1,    1,    2,    1,    1,    2,    1,    1,    3,    3,
    2,    0,    3,    1,    3,    7,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    0,    3,    3,    1,
};
const short yydefred[] = {                                      0,
    0,    0,    0,    5,    6,    7,    8,   15,    1,    0,
    0,    0,   10,   12,   14,    0,    0,    0,    0,    4,
    2,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   27,    0,   30,   31,    0,    0,    0,   17,    0,   58,
    0,    0,    0,    0,   55,   56,   34,   37,   32,    0,
    0,    0,    0,   38,    0,   18,    0,    0,   40,   44,
   43,    0,   48,   49,   50,   51,   52,   53,   47,   28,
    0,    9,    0,    0,   54,   33,   35,    0,    0,    0,
   19,   21,   20,   22,   23,   24,   25,   26,    0,   41,
   45,   60,    0,    0,   39,   11,   36,   13,    0,   59,
    0,    0,    0,    0,    0,    0,    0,   16,   46,
};
const short yydgoto[] = {                                       2,
    3,    4,    5,    6,    7,    8,   31,    0,   32,   47,
   53,   48,   78,   49,   50,    9,   17,   18,   19,   75,
   76,   24,   44,   59,   61,   89,   74,   51,   34,   79,
   10,   35,   72,   39,   57,   93,
};
const short yysindex[] = {                                   -246,
 -251,    0, -239,    0,    0,    0,    0,    0,    0, -271,
 -246, -246,    0,    0,    0, -280, -232, -232, -232,    0,
    0, -232, -273, -261, -261, -261, -261, -219, -245, -244,
    0, -204,    0,    0, -275, -218, -218,    0, -218,    0,
 -205, -205, -241, -234,    0,    0,    0,    0,    0, -200,
 -197, -234, -275,    0, -275,    0, -250, -238,    0,    0,
    0, -196,    0,    0,    0,    0,    0,    0,    0,    0,
 -210,    0, -235, -234,    0,    0,    0, -200, -234, -200,
    0,    0,    0,    0,    0,    0,    0,    0, -231,    0,
    0,    0, -195, -194,    0,    0,    0,    0, -230,    0,
 -229, -227, -193, -226, -225, -200, -186,    0,    0,
};
const short yyrindex[] = {                                   -266,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
 -266,    1,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
 -182, -182,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,
};
const short yygindex[] = {                                      0,
    0,    0,    0,    0,    0,    0,   52,    0,    0,    0,
    0,  -32,   25,   44,    0,   27,    0,    0,    0,  -44,
    3,   -7,    0,   41,    0,    0,    0,   26,    0,   49,
    0,   29,  -75,    0,    0,    0,
};
#define YYTABLESIZE 292
const short yytable[] = {                                      70,
    3,   29,   96,   54,   98,   11,   56,   13,   14,    1,
   25,   26,   57,   57,   27,   29,   30,   12,   15,   16,
   77,   22,   77,   57,   57,   23,   45,   46,   28,   95,
  108,   63,   64,   65,   66,   67,   68,   20,   21,   40,
   69,   81,   82,   83,   84,   85,   86,   87,   88,   33,
   33,   33,   33,   36,   37,   43,   41,   42,   29,   58,
   62,   71,   73,   90,   91,   92,   94,  100,   99,  101,
  105,  102,  103,  104,  109,  106,  107,   42,   38,   80,
   55,   97,   60,   52,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,   57,
   57,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   57,   57,
};
const short yycheck[] = {                                      44,
    0,  277,   78,   36,   80,  257,   39,  279,  280,  256,
   18,   19,  279,  280,   22,  277,  278,  257,  290,  291,
   53,  302,   55,  290,  291,  258,  302,  303,  302,   74,
  106,  266,  267,  268,  269,  270,  271,   11,   12,  259,
  275,  292,  293,  294,  295,  296,  297,  298,  299,   24,
   25,   26,   27,   25,   26,  260,  302,  302,  277,  265,
  302,  262,  260,  302,  261,  276,  302,  263,  300,  264,
  264,  302,  302,  301,  261,  302,  302,  260,   27,   55,
   37,   79,   42,   35,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,  279,
  280,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
  290,  291,
};
#define YYFINAL 2
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#define YYMAXTOKEN 305
#if YYDEBUG
const char * const yyname[] = {
"end-of-file",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"SEMICOLON","LPAREN","RPAREN",
"LANGLE","RANGLE","LCURLY","RCURLY","COMMA","DOT","TYPE_UD","TYPE_D","TYPE_UW",
"TYPE_W","TYPE_UB","TYPE_B","TYPE_VF","TYPE_HF","TYPE_V","TYPE_F","ALIGN1",
"GENREGFILE","MSGREGFILE","MOV","MUL","MAC","MACH","LINE","SAD2","SADA2","DP4",
"DPH","DP3","DP2","ADD","SEND","NULL_TOKEN","MATH","SAMPLER","GATEWAY","READ",
"WRITE","URB","THREAD_SPAWNER","MSGLEN","RETURNLEN","INTEGER","NUMBER","accreg",
"triop",
};
const char * const yyrule[] = {
"$accept : ROOT",
"ROOT : instrseq",
"instrseq : instruction SEMICOLON instrseq",
"instrseq : instruction SEMICOLON",
"instrseq : error SEMICOLON instrseq",
"instruction : unaryinstruction",
"instruction : binaryinstruction",
"instruction : binaryaccinstruction",
"instruction : triinstruction",
"unaryinstruction : predicate unaryop execsize dst srcaccimm instoptions",
"unaryop : MOV",
"binaryinstruction : predicate binaryop execsize dst src srcimm instoptions",
"binaryop : MUL",
"binaryaccinstruction : predicate binaryaccop execsize dst srcacc srcimm instoptions",
"binaryaccop : ADD",
"triinstruction : sendinstruction",
"sendinstruction : predicate SEND INTEGER execsize postdst curdst msgtarget MSGLEN INTEGER RETURNLEN INTEGER instoptions",
"postdst : dstoperand",
"curdst : directsrcoperand",
"msgtarget : NULL_TOKEN",
"msgtarget : SAMPLER",
"msgtarget : MATH",
"msgtarget : GATEWAY",
"msgtarget : READ",
"msgtarget : WRITE",
"msgtarget : URB",
"msgtarget : THREAD_SPAWNER",
"dst : dstoperand",
"dstoperand : dstreg dstregion regtype",
"dstoperandex : accreg dstregion regtype",
"dstreg : directgenreg",
"dstreg : directmsgreg",
"srcaccimm : srcacc",
"srcaccimm : imm32 srcimmtype",
"srcacc : directsrcaccoperand",
"srcimm : directsrcoperand",
"srcimm : imm32 srcimmtype",
"directsrcaccoperand : directsrcoperand",
"src : directsrcoperand",
"directsrcoperand : directgenreg region regtype",
"directgenreg : GENREGFILE INTEGER gensubregnum",
"gensubregnum : DOT INTEGER",
"gensubregnum :",
"directmsgreg : MSGREGFILE INTEGER msgsubregnum",
"msgsubregnum : gensubregnum",
"dstregion : LANGLE INTEGER RANGLE",
"region : LANGLE INTEGER COMMA INTEGER COMMA INTEGER RANGLE",
"regtype : TYPE_F",
"regtype : TYPE_UD",
"regtype : TYPE_D",
"regtype : TYPE_UW",
"regtype : TYPE_W",
"regtype : TYPE_UB",
"regtype : TYPE_B",
"srcimmtype : regtype",
"imm32 : INTEGER",
"imm32 : NUMBER",
"predicate :",
"execsize : LPAREN INTEGER RPAREN",
"instoptions : LCURLY instoption RCURLY",
"instoption : ALIGN1",
};
#endif
#if YYDEBUG
#include <stdio.h>
#endif
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 10000
#define YYMAXDEPTH 10000
#endif
#endif
#define YYINITSTACKSIZE 200
int yydebug;
int yynerrs;
int yyerrflag;
int yychar;
short *yyssp;
YYSTYPE *yyvsp;
YYSTYPE yyval;
YYSTYPE yylval;
short *yyss;
short *yysslim;
YYSTYPE *yyvs;
int yystacksize;
#line 395 "gram.y"
extern int yylineno;

void yyerror (char *msg)
{
	fprintf(stderr, "parse error \"%s\" at line %d, token \"%s\"\n",
		msg, yylineno, lex_text());
}

#line 375 "y.tab.c"
/* allocate initial stack or double stack size, up to YYMAXDEPTH */
static int yygrowstack()
{
    int newsize, i;
    short *newss;
    YYSTYPE *newvs;

    if ((newsize = yystacksize) == 0)
        newsize = YYINITSTACKSIZE;
    else if (newsize >= YYMAXDEPTH)
        return -1;
    else if ((newsize *= 2) > YYMAXDEPTH)
        newsize = YYMAXDEPTH;
    i = yyssp - yyss;
    newss = yyss ? (short *)realloc(yyss, newsize * sizeof *newss) :
      (short *)malloc(newsize * sizeof *newss);
    if (newss == NULL)
        return -1;
    yyss = newss;
    yyssp = newss + i;
    newvs = yyvs ? (YYSTYPE *)realloc(yyvs, newsize * sizeof *newvs) :
      (YYSTYPE *)malloc(newsize * sizeof *newvs);
    if (newvs == NULL)
        return -1;
    yyvs = newvs;
    yyvsp = newvs + i;
    yystacksize = newsize;
    yysslim = yyss + newsize - 1;
    return 0;
}

#define YYABORT goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR goto yyerrlab

#ifndef YYPARSE_PARAM
#if defined(__cplusplus) || __STDC__
#define YYPARSE_PARAM_ARG void
#define YYPARSE_PARAM_DECL
#else	/* ! ANSI-C/C++ */
#define YYPARSE_PARAM_ARG
#define YYPARSE_PARAM_DECL
#endif	/* ANSI-C/C++ */
#else	/* YYPARSE_PARAM */
#ifndef YYPARSE_PARAM_TYPE
#define YYPARSE_PARAM_TYPE void *
#endif
#if defined(__cplusplus) || __STDC__
#define YYPARSE_PARAM_ARG YYPARSE_PARAM_TYPE YYPARSE_PARAM
#define YYPARSE_PARAM_DECL
#else	/* ! ANSI-C/C++ */
#define YYPARSE_PARAM_ARG YYPARSE_PARAM
#define YYPARSE_PARAM_DECL YYPARSE_PARAM_TYPE YYPARSE_PARAM;
#endif	/* ANSI-C/C++ */
#endif	/* ! YYPARSE_PARAM */

int
yyparse (YYPARSE_PARAM_ARG)
    YYPARSE_PARAM_DECL
{
    int yym, yyn, yystate;
#if YYDEBUG
    const char *yys;

    if ((yys = getenv("YYDEBUG")))
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

    yynerrs = 0;
    yyerrflag = 0;
    yychar = (-1);

    if (yyss == NULL && yygrowstack()) goto yyoverflow;
    yyssp = yyss;
    yyvsp = yyvs;
    *yyssp = yystate = 0;

yyloop:
    if ((yyn = yydefred[yystate])) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yyssp >= yysslim && yygrowstack())
        {
            goto yyoverflow;
        }
        *++yyssp = yystate = yytable[yyn];
        *++yyvsp = yylval;
        yychar = (-1);
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;
#if defined(lint) || defined(__GNUC__)
    goto yynewerror;
#endif
yynewerror:
    yyerror("syntax error");
#if defined(lint) || defined(__GNUC__)
    goto yyerrlab;
#endif
yyerrlab:
    ++yynerrs;
yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yyssp]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yyssp, yytable[yyn]);
#endif
                if (yyssp >= yysslim && yygrowstack())
                {
                    goto yyoverflow;
                }
                *++yyssp = yystate = yytable[yyn];
                *++yyvsp = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yyssp);
#endif
                if (yyssp <= yyss) goto yyabort;
                --yyssp;
                --yyvsp;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = (-1);
        goto yyloop;
    }
yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    yyval = yyvsp[1-yym];
    switch (yyn)
    {
case 1:
#line 92 "gram.y"
{
		  compiled_program = yyvsp[0].program;
		}
break;
case 2:
#line 98 "gram.y"
{
		  struct brw_program_instruction *list_entry =
		    calloc(sizeof(struct brw_program_instruction), 1);
		  list_entry->instruction = yyvsp[-2].instruction;

		  list_entry->next = yyvsp[0].program.first;
		  yyvsp[0].program.first = list_entry;

		  yyval.program = yyvsp[0].program;
		}
break;
case 3:
#line 109 "gram.y"
{
		  struct brw_program_instruction *list_entry =
		    calloc(sizeof(struct brw_program_instruction), 1);
		  list_entry->instruction = yyvsp[-1].instruction;

		  list_entry->next = NULL;

		  yyval.program.first = list_entry;
		}
break;
case 4:
#line 119 "gram.y"
{
		  yyval.program = yyvsp[0].program;
		}
break;
case 9:
#line 132 "gram.y"
{
		  yyval.instruction.header.opcode = yyvsp[-4].integer;
		  yyval.instruction.header.execution_size = yyvsp[-3].integer;
		}
break;
case 10:
#line 138 "gram.y"
{ yyval.integer = BRW_OPCODE_MOV; }
break;
case 11:
#line 143 "gram.y"
{
		  yyval.instruction.header.opcode = yyvsp[-5].integer;
		  yyval.instruction.header.execution_size = yyvsp[-4].integer;
		}
break;
case 12:
#line 149 "gram.y"
{ yyval.integer = BRW_OPCODE_MUL; }
break;
case 13:
#line 153 "gram.y"
{
		  yyval.instruction.header.opcode = yyvsp[-5].integer;
		  yyval.instruction.header.execution_size = yyvsp[-4].integer;
		}
break;
case 14:
#line 159 "gram.y"
{ yyval.integer = BRW_OPCODE_ADD; }
break;
case 16:
#line 167 "gram.y"
{
		  yyval.instruction.header.opcode = BRW_OPCODE_SEND;
		  yyval.instruction.header.execution_size = yyvsp[-8].integer;
		  yyval.instruction.header.destreg__conditonalmod = yyvsp[-9].integer;
		}
break;
case 19:
#line 181 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_NULL; }
break;
case 20:
#line 182 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_SAMPLER; }
break;
case 21:
#line 183 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_MATH; }
break;
case 22:
#line 184 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_GATEWAY; }
break;
case 23:
#line 185 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_DATAPORT_READ; }
break;
case 24:
#line 186 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_DATAPORT_WRITE; }
break;
case 25:
#line 187 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_URB; }
break;
case 26:
#line 188 "gram.y"
{ yyval.integer = BRW_MESSAGE_TARGET_THREAD_SPAWNER; }
break;
case 28:
#line 198 "gram.y"
{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  yyval.instruction.bits1 = yyvsp[-2].instruction.bits1;
		  yyval.instruction.bits1.da1.dest_reg_type = yyvsp[-1].integer;
		}
break;
case 29:
#line 207 "gram.y"
{
		  /* Returns an instruction with just the destination register
		   * filled in.
		   */
		  yyval.instruction.bits1 = yyvsp[-2].instruction.bits1;
		  yyval.instruction.bits1.da1.dest_reg_type = yyvsp[-1].integer;
		}
break;
case 30:
#line 217 "gram.y"
{
		  yyval.instruction.bits1.da1.dest_reg_file = yyvsp[0].direct_gen_reg.reg_file;
		  yyval.instruction.bits1.da1.dest_reg_nr = yyvsp[0].direct_gen_reg.reg_nr;
		  yyval.instruction.bits1.da1.dest_subreg_nr = yyvsp[0].direct_gen_reg.subreg_nr;
		}
break;
case 33:
#line 229 "gram.y"
{
		  yyval.instruction.bits1.da1.src0_reg_file = BRW_IMMEDIATE_VALUE;
		  switch (yyvsp[0].integer) {
		  case BRW_REGISTER_TYPE_UD:
		    yyval.instruction.bits3.ud = yyvsp[-1].imm32;
		    break;
		  case BRW_REGISTER_TYPE_D:
		    yyval.instruction.bits3.id = yyvsp[-1].imm32;
		    break;
		  case BRW_REGISTER_TYPE_F:
		    yyval.instruction.bits3.fd = yyvsp[-1].imm32;
		    break;
		  }
		}
break;
case 36:
#line 251 "gram.y"
{
		  yyval.instruction.bits1.da1.src0_reg_file = BRW_IMMEDIATE_VALUE;
		  switch (yyvsp[0].integer) {
		  case BRW_REGISTER_TYPE_UD:
		    yyval.instruction.bits3.ud = yyvsp[-1].imm32;
		    break;
		  case BRW_REGISTER_TYPE_D:
		    yyval.instruction.bits3.id = yyvsp[-1].imm32;
		    break;
		  case BRW_REGISTER_TYPE_F:
		    yyval.instruction.bits3.fd = yyvsp[-1].imm32;
		    break;
		  }
		}
break;
case 39:
#line 277 "gram.y"
{
		  /* Returns a source operand in the src0 fields of an
		   * instruction.
		   */
		  yyval.instruction.bits1.da1.src0_reg_file = yyvsp[-2].direct_gen_reg.reg_file;
		  yyval.instruction.bits1.da1.src0_reg_type = yyvsp[0].integer;
		  yyval.instruction.bits2.da1.src0_subreg_nr = yyvsp[-2].direct_gen_reg.subreg_nr;
		  yyval.instruction.bits2.da1.src0_reg_nr = yyvsp[-2].direct_gen_reg.reg_nr;
		  yyval.instruction.bits2.da1.src0_vert_stride = yyvsp[-1].region.vert_stride;
		  yyval.instruction.bits2.da1.src0_width = yyvsp[-1].region.width;
		  yyval.instruction.bits2.da1.src0_horiz_stride = yyvsp[-1].region.horiz_stride;
		}
break;
case 40:
#line 293 "gram.y"
{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  yyval.direct_gen_reg.reg_file = BRW_GENERAL_REGISTER_FILE;
		  yyval.direct_gen_reg.reg_nr = yyvsp[-1].integer;
		  yyval.direct_gen_reg.subreg_nr = yyvsp[0].integer;
		}
break;
case 41:
#line 303 "gram.y"
{
		  yyval.integer = yyvsp[0].integer;
		}
break;
case 42:
#line 307 "gram.y"
{
		  /* Default to subreg 0 if unspecified. */
		  yyval.integer = 0;
		}
break;
case 43:
#line 314 "gram.y"
{
		  /* Returns an instruction with just the destination register
		   * fields filled in.
		   */
		  yyval.direct_gen_reg.reg_file = BRW_GENERAL_REGISTER_FILE;
		  yyval.direct_gen_reg.reg_nr = yyvsp[-1].integer;
		  yyval.direct_gen_reg.subreg_nr = yyvsp[0].integer;
		}
break;
case 45:
#line 329 "gram.y"
{
		  /* Returns a value for a horiz_stride field of an
		   * instruction.
		   */
		  if (yyvsp[-1].integer != 1 && yyvsp[-1].integer != 2 && yyvsp[-1].integer != 4) {
		    fprintf(stderr, "Invalid horiz size %d\n", yyvsp[-1].integer);
		  }
		  yyval.integer = ffs(yyvsp[-1].integer);
		}
break;
case 46:
#line 341 "gram.y"
{
		  yyval.region.vert_stride = yyvsp[-5].integer;
		  yyval.region.width = yyvsp[-3].integer;
		  yyval.region.horiz_stride = yyvsp[-1].integer;
		}
break;
case 47:
#line 353 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_F; }
break;
case 48:
#line 354 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_UD; }
break;
case 49:
#line 355 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_D; }
break;
case 50:
#line 356 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_UW; }
break;
case 51:
#line 357 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_UW; }
break;
case 52:
#line 358 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_UB; }
break;
case 53:
#line 359 "gram.y"
{ yyval.integer = BRW_REGISTER_TYPE_B; }
break;
case 58:
#line 372 "gram.y"
{
		  /* Returns a value for the execution_size field of an
		   * instruction.
		   */
		  if (yyvsp[-1].integer != 1 && yyvsp[-1].integer != 2 && yyvsp[-1].integer != 4 && yyvsp[-1].integer != 8 && yyvsp[-1].integer != 16 &&
		      yyvsp[-1].integer != 32) {
		    fprintf(stderr, "Invalid execution size %d\n", yyvsp[-1].integer);
		    YYERROR;
		  }
		  yyval.integer = ffs(yyvsp[-1].integer);
		}
break;
#line 854 "y.tab.c"
    }
    yyssp -= yym;
    yystate = *yyssp;
    yyvsp -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yyssp = YYFINAL;
        *++yyvsp = yyval;
        if (yychar < 0)
        {
            if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yyssp, yystate);
#endif
    if (yyssp >= yysslim && yygrowstack())
    {
        goto yyoverflow;
    }
    *++yyssp = yystate;
    *++yyvsp = yyval;
    goto yyloop;
yyoverflow:
    yyerror("yacc stack overflow");
yyabort:
    return (1);
yyaccept:
    return (0);
}
