#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "repeat.h"

typedef struct list {
    void *kind;
    void *contents;
    struct list *next;
} list;

/* FIXME: double fprintf is ugly */
#define error(v, ...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    exit(v); \
} while (0)

#define _concat(a, b) a ## b
#define concat(a, b) _concat(a, b)

#define assign_arg(n) \
    concat(arg_, n) = l->contents; \
    l = l->next

#define cast_apply_f(n) return ((void *(*)(concat(repeat_, n)(void *))) f)\
(concat(repeat_n_, n)(arg_))

#define new_case(n) \
    assign_arg(n); \
    if (arity == n) cast_apply_f(n)

void *run_func(void *f, void *v, void *as) {
    int arity = *((int *) v);
    list *l = (list *) as;

    void repeat_n_32(*arg_);

    if (arity == 0) return ((void *(*)()) f)();
    new_case(1);
    new_case(2);
    new_case(3);
    new_case(4);
    new_case(5);
    new_case(6);
    new_case(7);
    new_case(8);
    new_case(9);
    new_case(10);
    new_case(11);
    new_case(12);
    new_case(13);
    new_case(14);
    new_case(15);
    new_case(16);
    new_case(17);
    new_case(18);
    new_case(19);
    new_case(20);
    new_case(21);
    new_case(22);
    new_case(23);
    new_case(24);
    new_case(25);
    new_case(26);
    new_case(27);
    new_case(28);
    new_case(29);
    new_case(30);
    new_case(31);
    new_case(32);
    error(1, "arity %i not handled", arity);
}

void *inter_assert(void *a, void *b) {
    assert(*((int *) a));
    return b;
}
