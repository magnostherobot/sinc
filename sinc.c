#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "sinc.h"
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

void prologue(optimisation_t *llvm_info) {

    switch (format) {

        case LLVM_INTERMEDIATE:
        case BITCODE:
        case INTERPRET:
            llvm_codegen_prologue(input_filename, llvm_info);
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

    optimisation_t llvm_info = {
        .boxing_rule = ALWAYS_BOX,
        .default_bit_width = 0,
        .tail_recursive_mod_cons = true,
        .llvm_attributes = true
    };

    verbose = 0;

    debug("\n");

    while ((c = getopt(argc, argv, "bghilo:O:qsu:vw:")) != -1) {
        switch (c) {
            case 'b':
                format = BITCODE;
                break;

            case 'g':
                format = GRAPHVIZ;
                break;

            case 'h':
                printf(
"usage: %s [options] <input_file>\n"
"-b           Specify bitcode-format output\n"
"-c           Compile mode\n"
"-g           Output a Graphviz .dot file\n"
"-h           Print this help message\n"
"-i           Interpret mode\n"
"-l           Specify human-readable output\n"
"-o <output>  Specify output filename\n"
"-O <opt>     Enable compiler optimisation\n"
"-q           Quiet (default)\n"
"-s           Output S-expression description\n"
"-u <rule>    Set the boxing rule: always, never, or smart\n"
"-v           Verbose\n"
"-w <width>   Specify default width of integers in bits\n",
                        argv[0]);
                exit(0);

            case 'i':
                format = INTERPRET;
                break;

            case 'l':
                format = LLVM_INTERMEDIATE;
                break;

            case 'o':
                output_filename = strdup(optarg);
                break;

            case 'O':
                if (!strcmp(optarg, "tail-recursive-mod-cons")) {
                    llvm_info.tail_recursive_mod_cons = true;

                } else if (!strcmp(optarg, "no-tail-recursive-mod-cons")) {
                    llvm_info.tail_recursive_mod_cons = false;

                } else if (!strcmp(optarg, "llvm-attributes")) {
                    llvm_info.llvm_attributes = true;

                } else if (!strcmp(optarg, "no-llvm-attributes")) {
                    llvm_info.llvm_attributes = false;

                } else {
                    error(INVALID_ARGUMENTS,
                            "%s is not a recognised optimisation name\n",
                            optarg);

                }
                break;

            case 'q':
                verbose = 0;
                break;

            case 's':
                format = SINTER;
                break;

            case 'u':
                if (!strcmp(optarg, "always")) {
                    llvm_info.boxing_rule = ALWAYS_BOX;

                } else if (!strcmp(optarg, "never")) {
                    llvm_info.boxing_rule = NEVER_BOX;

                } else if (!strcmp(optarg, "smart")) {
                    llvm_info.boxing_rule = SMART_BOX;

                } else {
                    error(INVALID_ARGUMENTS,
                            "%s is not a valid option for -u\n", optarg);

                }
                break;

            case 'v':
                verbose = 1;
                break;

            case 'w':
                llvm_info.default_bit_width = atoi(optarg);
                break;

            case '?':
                switch (optopt) {
                    case 'o':
                    case 'O':
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

    prologue(&llvm_info);
    yyparse();
    epilogue(output_filename, format);
    return 0;
}
