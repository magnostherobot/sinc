#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "sinter.h"
#include "sexpr.h"
#include "llvm_codegen.h"
#include "sinter_codegen.h"
#include "graphviz_codegen.h"
#include "parse.h"
#include "debug.h"
#include "error.h"

typedef unsigned int uint;

static char *input_filename;
static char *output_filename;
static format_t format;

extern FILE *yyin;
int yyparse(void);

int verbose;

sexpr *new_sexpr(int type, contents val, lloc_t lloc) {
    /* FIXME: unchecked memory allocation! */
    sexpr *ret = malloc(sizeof (*ret));
    ret->type = type;
    ret->contents = val;
    ret->lloc = lloc;
    return ret;
}

sexpr *new_int(int num, lloc_t lloc) {
    contents c;
    c.i = num;
    return new_sexpr(INT, c, lloc);
}

sexpr *new_str(char *str, lloc_t lloc) {
    contents c;
    c.s = str;
    return new_sexpr(STRING, c, lloc);
}

sexpr *new_float(double num, lloc_t lloc) {
    contents c;
    c.f = num;
    return new_sexpr(FLOAT, c, lloc);
}

sexpr *new_id(char *id, lloc_t lloc) {
    contents c;
    c.s = id;
    return new_sexpr(ID, c, lloc);
}

sexpr *new_node(sexpr *l, sexpr *r, lloc_t lloc) {
    contents c;
    c.n.l = l;
    c.n.r = r;
    return new_sexpr(BRANCH, c, lloc);
}

void prologue() {

    switch (format) {

        case LLVM_INTERMEDIATE:
        case BITCODE:
        case INTERPRET:
            llvm_codegen_prologue(input_filename);
            break;

        case GRAPHVIZ:
            graphviz_codegen_prologue();
            break;

        case SINTER:
            sinter_codegen_prologue();
            break;

    }
}

void handle(sexpr *se) {

    switch (format) {

        case LLVM_INTERMEDIATE:
        case BITCODE:
        case INTERPRET:
            llvm_codegen(se);
            break;

        case GRAPHVIZ:
            graphviz_codegen(se);
            break;

        case SINTER:
            sinter_codegen(se);
            break;

    }
}

void epilogue(char *output_filename, format_t format) {

    switch (format) {

        case LLVM_INTERMEDIATE:
        case BITCODE:
        case INTERPRET:
            llvm_codegen_epilogue(output_filename, format);
            break;

        case GRAPHVIZ:
            graphviz_codegen_epilogue(output_filename);
            break;

        case SINTER:
            sinter_codegen_epilogue(output_filename);
            break;

    }
}

int main(int argc, char **argv) {
    int c;

    output_filename = 0;
    format = LLVM_INTERMEDIATE;

    verbose = 0;

    while ((c = getopt(argc, argv, "bghilo:qsv")) != -1) {
        switch (c) {
            case 'b':
                format = BITCODE;
                break;

            case 'g':
                format = GRAPHVIZ;
                break;

            case 'h':
                printf( "usage: %s [options] <input_file>\n"
                        "-b           Specify bitcode-format output\n"
                        "-c           Compile mode\n"
                        "-g           Output a Graphviz .dot file\n"
                        "-h           Print this help message\n"
                        "-i           Interpret mode\n"
                        "-l           Specify human-readable output\n"
                        "-o <output>  Specify output filename\n"
                        "-q           Quiet (default)\n"
                        "-s           Output S-expression description\n"
                        "-v           Verbose\n",
                        argv[0]);
                exit(0);

            case 'i':
                format = INTERPRET;
                break;

            case 'l':
                format = LLVM_INTERMEDIATE;
                break;

            case 'o':
                output_filename = optarg;
                break;

            case 'q':
                verbose = 0;
                break;

            case 's':
                format = SINTER;
                break;

            case 'v':
                verbose = 1;
                break;

            case '?':
                switch (optopt) {
                    case 'o':
                        error(INVALID_ARGUMENTS,
                                "-%c option requires an argument", optopt);
                        break;

                    default:
                        error(INVALID_ARGUMENTS, "unknown option %c", optopt);
                }
                break;

            default:
                error(INVALID_ARGUMENTS, "error parsing arguments");
        }
    }

    if (argc - optind > 1) {
        error(INVALID_ARGUMENTS, "too many arguments");
    } else if (argc - optind == 1) {
        input_filename = argv[argc - 1];
        yyin = fopen(input_filename, "r");
    }

    prologue();
    yyparse();
    epilogue(output_filename, format);
    return 0;
}
