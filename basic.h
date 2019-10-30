#ifndef BASIC_H
#define BASIC_H

/*
 * This struct being padded is unimportant.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"

typedef struct sexpr {
    int type;
    union contents {
        double f;
        char *s;
        int i;
        struct node {
            struct sexpr *l;
            struct sexpr *r;
        } n;
    } contents;
} sexpr;

#pragma clang diagnostic pop

typedef struct node node;
typedef union contents contents;

sexpr *new_int(int num);
sexpr *new_float(double num);
sexpr *new_id(char *id);
sexpr *new_node(sexpr *l, sexpr *r);
sexpr *new_str(char *str);

void print_sexpr(sexpr *se);
void fprint_sexpr(FILE *f, sexpr *se);

void codegen(sexpr *sexpr);

#endif /* BASIC_H */
