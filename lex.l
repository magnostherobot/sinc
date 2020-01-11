%option noinput nounput
%option noyywrap
%option nodefault
/* %option reentrant bison-bridge bison-locations */

%{

#include <string.h>

#include "sexpr.h"
#include "sinc.h"
#include "parse.h"

void yyerror(const char *s, ...);
char *str_value(char *s);

void update_yylloc() {
    yylloc.first_line = yylloc.last_line;
    yylloc.first_column = yylloc.last_column;

    for (int i = 0; yytext[i]; i++) {
        if (yytext[i] == '\n') {
            yylloc.last_line++;
            yylloc.last_column = 0;
        } else {
            yylloc.last_column++;
        }
    }
}

#define YY_USER_ACTION update_yylloc();

char *str_value(char *s) {
    s++;
    /* FIXME: strdup allocates memory! */
    char *ret = strdup(s);
    ret[strlen(s) - 2] = 0;

    return ret;
}

%}

%option yylineno

ID     [-A-Za-z_+*^!:/%.=<>][-A-Za-z0-9_+*^!:/%.=<>]*
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
