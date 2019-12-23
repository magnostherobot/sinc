#include <stdio.h>
#include <stdlib.h>

#include "graphviz_codegen.h"
#include "sinter.h"
#include "parse.h"
#include "error.h"

/*
 * TODO
 * The structure of this file is very similar to that of sinter_codegen.c; maybe
 * they can be merged?
 */

typedef struct func_str {
    /* FIXME: magic size */
    char buf[100000];
    struct func_str *next;
} func_str;

func_str *ffs;
func_str *lfs;

int nid;

static int make_node(char *buf, sexpr *ast, sexpr *parent) {
    if (!ast) {
        return sprintf(buf, "\"%p%p\"[label=\"()\",shape=\"circle\"]\n",
                (void *) parent, (void *) ast);

    } else switch (ast->type) {

        case INT:
            return sprintf(buf, "\"%p%p\"[label=\"%i\"]\n", (void *) parent,
                    (void *) ast, ast->contents.i);

        case FLOAT:
            return sprintf(buf, "\"%p%p\"[label=\"%f\"]\n", (void *) parent,
                    (void *) ast, ast->contents.f);

        case STRING:
            return sprintf(buf, "\"%p%p\"[label=\"\\\"%s\\\"\"]\n",
                    (void *) parent, (void *) ast, ast->contents.s);

        case ID:
            return sprintf(buf, "\"%p%p\"[label=\"%s\"]\n", (void *) parent,
                    (void *) ast, ast->contents.s);

        case BRANCH:
            return sprintf(buf,
                    "\"%p%p\"[label=\"\",shape=\"circle\",style=\"filled\",width=\"0.2\",fillcolor=\"black\"]\n",
                    (void *) parent, (void *) ast);

        default:
            return 0;
    }
}

static int make_children(char *buf, sexpr *ast, sexpr *parent) {
    if (!ast || ast->type != BRANCH) return 0;

    char *old_buf = buf;

    buf += sprintf(buf, "{\nrank=\"same\"\nrankdir=\"LR\"\n");
    sexpr *last = ast->contents.n.l;
    sexpr *rast = ast->contents.n.r;
    buf += make_node(buf, last, ast);
    buf += make_node(buf, rast, ast);
    buf += sprintf(buf, "\"%p%p\"->\"%p%p\"[style=\"invis\"]\n", (void *) ast,
            (void *) last, (void *) ast, (void *) rast);
    buf += sprintf(buf, "}\n");
    buf += sprintf(buf, "\"%p%p\"->\"%p%p\"\n", (void *) parent, (void *) ast,
            (void *) ast, (void *) last);
    buf += sprintf(buf, "\"%p%p\"->\"%p%p\"\n", (void *) parent, (void *) ast,
            (void *) ast, (void *) rast);

    buf += make_children(buf, last, ast);
    buf += make_children(buf, rast, ast);

    return buf - old_buf;
}

void graphviz_codegen_prologue() {
    ffs = 0;
    lfs = 0;
    nid = 0;
}

void graphviz_codegen(sexpr *ast) {
    func_str *fs = (func_str *) malloc(sizeof (*fs));
    if (!fs) error(GENERAL_ERROR, "cannot malloc");

    char *buf = fs->buf;
    buf += sprintf(buf, "subgraph \"cluster_%i\" {\n", ++nid);
    buf += make_node(buf, ast, 0);
    buf += make_children(buf, ast, 0);
    buf += sprintf(buf, "}\n");
    fs->next = 0;

    if (!ffs) ffs = fs;
    if (lfs) lfs->next = fs;
    lfs = fs;
}

int graphviz_codegen_epilogue(char *output_filename) {
    FILE *fp = fopen(output_filename, "w");

    fprintf(fp, "digraph {\n");
    fprintf(fp, "node[fontname=\"monospace\"]\n");
    for (func_str *fs = ffs; fs; fs = fs->next) {
        fprintf(fp, "%s\n", fs->buf);
    }
    fprintf(fp, "}\n");

    return 0;
}
