#ifndef SINTER_H
#define SINTER_H

#include <stdio.h>

#include "sexpr.h"
#include "parse.h"

typedef unsigned int uint;

typedef enum {
    BITCODE,
    LLVM_INTERMEDIATE,
    INTERPRET,
    GRAPHVIZ,
    SINTER
} format_t;

typedef struct node node;
typedef union contents contents;

sexpr *new_int(int num, lloc_t lloc);
sexpr *new_float(double num, lloc_t lloc);
sexpr *new_id(char *id, lloc_t lloc);
sexpr *new_node(sexpr *l, sexpr *r, lloc_t lloc);
sexpr *new_str(char *str, lloc_t lloc);

void print_sexpr(sexpr *se);
void fprint_sexpr(FILE *f, sexpr *se);

void handle(sexpr *se);

#endif /* SINTER_H */
