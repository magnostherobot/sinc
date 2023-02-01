%option noinput nounput
%option noyywrap
%option nodefault
/* %option reentrant bison-bridge bison-locations */

%{

#include <string.h>

#include "sexpr.h"
#include "sinc.h"
#include "parse.h"
#include "error.h"
#include "debug.h"

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

/**
 * Calculate the size of a string value from a literal, by skipping backslashes
 * and stopping at the first unescaped double quote encountered.
 */
size_t string_literal_size(char *s) {
    size_t result = 0;

    for (char *c = s; *c != '"'; result++, c++) {
        if (*c == '\\') {
            c++;
        }
    }

    return result;
}

/**
 * Copy a string literal into a new buffer, removing things like escape
 * sequences.
 *
 * n is the size of the literal, NOT the size of the buffer (the buffer should
 * be one char larger for the null terminator).
 */
void deliteral_string(char *literal, char *buf, size_t n) {
    char *c = literal;

    for (size_t i = 0; i < n; c++, i++) {
        if (*c == '\\') {
            c++;
            switch (*c) {
                case 'n':
                    buf[i] = '\n';
                    break;

                case 't':
                    buf[i] = '\t';
                    break;

                case '\\':
                    buf[i] = '\\';
                    break;

                default:
                    warn("unrecognised escape sequence \\%c", *c);
                    buf[i] = *c;
            }

        } else {
            buf[i] = *c;
        }
    }

    buf[n] = '\0';
}

char *str_value(char *s) {
    /*
     * This function is given a string literal between quotes, for example:
     *
     *     "hello world\n"
     *
     * We need to do a few things here:
     * 1. drop the quotes;
     * 2. replace escape sequences with the character they represent.
     */

    s++;
    size_t size = string_literal_size(s);
    char *result = malloc(size + 1);
    deliteral_string(s, result, size);

    debug("in: %s\tout: %s (size %zu)\n", s-1, result, size);

    return result;
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
