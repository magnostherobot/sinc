#include <assert.h>
#include <string.h>

#include <gc.h>

void *test(void);

int main(void) {
    test();
    return 0;
}

void *gc_alloc(size_t size) {
    void *p = GC_MALLOC(size);
    assert(p);
    return p;
}

void *assert_equals(void *a, void *b, void *c) {
    int ia = *((int *) a), ib = *((int *) b);
    assert(ia == ib);
    return c;
}

void *assert_string_equals(void *a, void *b, void *c) {
    char *sa = (char *) a, *sb = (char *) b;
    assert(!strcmp(sa, sb));
    return c;
}
