%option noyywrap
%option nodefault
%option yylineno
%option noinput nounput

%{

#include <string.h>

#include "basic.h"
#include "parse.h"

void yyerror(const char *s, ...);

%}

ID    [-A-Za-z_+*!/][-A-Za-z0-9_+*!]*
FLOAT [+-]?([0-9]+[.][0-9]*([Ee][+-]?[0-9]+)?|[.][0-9]+([Ee][+-]?[0-9]+)?)
INT   [+-]?[0-9]+

%%

"(" |
")" |
";" { return yytext[0]; }

"()" { return NIL; }

{ID} { /* TODO: strdup allocates memory! */
       yylval.s = strdup(yytext); return ID; }

{INT} { yylval.i = atoi(yytext); return INT; }

{FLOAT} { yylval.f = atof(yytext); return FLOAT; }

[ \t\n] /* ignore whitespace */

. { yyerror("Unexpected character %c\n", yytext[0]); }
