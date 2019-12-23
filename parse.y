%locations

%{

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "sexpr.h"
#include "sinter.h"

extern int yylineno;
int yylex(void);
void yyerror(char *s, ...);

sexpr *ast;

%}

%union {
    double f;
    int i;
    char *s;
    sexpr *u;
}

%token     NIL
%token <f> FLOAT
%token <i> INT
%token <s> ID
%token <s> STRING
%token     BRANCH

%type  <u> sexpr
%type  <u> lexpr

%%

cmds:
    | cmds sexpr { handle($2); }

sexpr: '(' sexpr ';' sexpr ')' { $$ = new_node($2, $4, yylloc); }
     | '(' sexpr     sexpr ')' { $$ = new_node($2, $3, yylloc); }
     | '[' lexpr { $$ = $2; }
     | INT    { $$ = new_int($1, yylloc); }
     | FLOAT  { $$ = new_float($1, yylloc); }
     | ID     { $$ = new_id($1, yylloc); }
     | NIL    { $$ = 0; }
     | STRING { $$ = new_str($1, yylloc); }

lexpr: ']' { $$ = 0; }
     | sexpr ';' lexpr { $$ = new_node($1, $3, yylloc); }
     | sexpr     lexpr { $$ = new_node($1, $2, yylloc); }

%%

void yyerror(char *s, ...) {
    va_list ap;
    va_start(ap, s);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    fprintf(stderr, "%d: error: ", yylineno);
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
#pragma clang diagnostic pop
}
