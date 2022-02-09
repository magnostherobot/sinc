#include <stdio.h>
#include <stdlib.h>

#include "sinter_codegen.h"
#include "sinc.h"
#include "parse.h"
#include "error.h"

typedef struct func_str_s {
    /* FIXME: magic size */
    char buf[1000];
    struct func_str_s *next;
} func_str;

func_str *sin_ffs;
func_str *sin_lfs;

static int fill_buf(char *buf, sexpr *ast) {
    char *old_buf = buf;

    if (!ast) {
        return sprintf(buf, "()");

    } else switch (ast->type) {

        case INT:
            return sprintf(buf, "%i", ast->contents.i);

        case FLOAT:
            return sprintf(buf, "%f", ast->contents.f);

        case STRING:
            return sprintf(buf, "\"%s\"", ast->contents.s);

        case ID:
            return sprintf(buf, "%s", ast->contents.s);

        case BRANCH:
            buf += sprintf(buf, "(");
            buf += fill_buf(buf, ast->contents.n.l);
            buf += sprintf(buf, ";");
            buf += fill_buf(buf, ast->contents.n.r);
            buf += sprintf(buf, ")");
            return buf - old_buf;

        default:
            return sprintf(buf, "?");
    }
}

void sinter_codegen_prologue() {
    sin_ffs = 0;
    sin_lfs = 0;
}

void sinter_codegen(sexpr *ast) {
    func_str *fs = (func_str *) malloc(sizeof (*fs));
    if (!fs) error(GENERAL_ERROR, "cannot malloc");

    fill_buf(fs->buf, ast);
    fs->next = 0;

    if (!sin_ffs) sin_ffs = fs;
    if (sin_lfs) sin_lfs->next = fs;
    sin_lfs = fs;
}

int sinter_codegen_epilogue(char *output_filename) {
    FILE *fp = fopen(output_filename, "w");

    for (func_str *fs = sin_ffs; fs; fs = fs->next) {
        fprintf(fp, "%s\n", fs->buf);
    }

    return 0;
}
