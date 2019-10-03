%{

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "basic.h"

extern int yylineno;
int yylex();
void yyerror(char *s, ...);

%}

%union {
    float f;
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
    | cmds sexpr { codegen($2); }

sexpr: '(' sexpr ';' sexpr ')' { $$ = new_node($2, $4); }
     | '(' sexpr     sexpr ')' { $$ = new_node($2, $3); }
     | '[' lexpr { $$ = $2; }
     | INT    { $$ = new_int($1); }
     | FLOAT  { $$ = new_float($1); }
     | ID     { $$ = new_id($1); }
     | NIL    { $$ = 0; }
     | STRING { $$ = new_str($1); }

lexpr: ']' { $$ = 0; }
     | sexpr ';' lexpr { $$ = new_node($1, $3); }
     | sexpr     lexpr { $$ = new_node($1, $2); }

%%

void yyerror(char *s, ...) {
    va_list ap;
    va_start(ap, s);

    fprintf(stderr, "%d: error: ", yylineno);
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
}
