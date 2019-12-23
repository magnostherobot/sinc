#include <assert.h>

void *test(void);

void *inter_main(void) {
    return test();
}

void *assert_equals(void *a, void *b, void *c) {
    int ia = *((int *) a), ib = *((int *) b);
    assert(ia == ib);
    return c;
}
