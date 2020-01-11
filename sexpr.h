#ifndef SEXPR_H
#define SEXPR_H

typedef struct YYLTYPE {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} YYLTYPE;
typedef YYLTYPE lloc_t;

#define YYLTYPE_IS_DECLARED 1
#define YYLTYPE_IS_TRIVIAL 1

typedef struct sexpr {
    int type;
    union contents {
        double f;
        char *s;
        unsigned i;
        struct node {
            struct sexpr *l;
            struct sexpr *r;
        } n;
    } contents;
    lloc_t lloc;
} sexpr;

#endif /* SEXPR_H */
