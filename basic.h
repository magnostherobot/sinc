#ifndef _BASIC_H
#define _BASIC_H

typedef struct sexpr {
    int type;
    union contents {
        float f;
        char *s;
        int i;
        struct node {
            struct sexpr *l;
            struct sexpr *r;
        } n;
    } contents;
} sexpr;

typedef struct node node;
typedef union contents contents;

sexpr *new_int(int num);
sexpr *new_float(float num);
sexpr *new_id(char *id);
sexpr *new_node(sexpr *l, sexpr *r);
sexpr *new_str(char *str);

void print_sexpr(sexpr *contents);
void codegen(sexpr *sexpr);

#endif /* _BASIC_H */
