%option noyywrap
%option nodefault
%option yylineno
%option noinput nounput

%{

#include <string.h>

#include "basic.h"
#include "parse.h"

void yyerror(const char *s, ...);
char *str_value(char *s);

char *str_value(char *s) {
    s++;
    /* FIXME: strdup allocates memory! */
    char *ret = strdup(s);
    ret[strlen(s) - 2] = 0;

    return ret;
}

%}

ID     [-A-Za-z_+*!/.][-A-Za-z0-9_+*!.]*
STRING ["][^"]*["]|['][^']*[']
FLOAT  [+-]?([0-9]+[.][0-9]*([Ee][+-]?[0-9]+)?|[.][0-9]+([Ee][+-]?[0-9]+)?)
INT    [+-]?[0-9]+

%%

"(" |
")" |
"[" |
"]" |
";" { return yytext[0]; }

"()" { return NIL; }

{STRING} { yylval.s = str_value(yytext); return STRING; }

{ID} { /* FIXME: strdup allocates memory! */
       yylval.s = strdup(yytext); return ID; }

{INT} { yylval.i = atoi(yytext); return INT; }

{FLOAT} { yylval.f = atof(yytext); return FLOAT; }

[ \t\n] /* ignore whitespace */

. { yyerror("Unexpected character %c\n", yytext[0]); }
