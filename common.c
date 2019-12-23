#include <stddef.h>
#include <stdio.h>

#include <gc.h>

void *inter_main(void);

void *debug_int(void *i) {
    int *p = (int *) i;
    fprintf(stderr, "%d\n", *p);
    return i;
}

void *debug_func_call(void *f) {
    char *func_id = (char *) f;
    fprintf(stderr, "%s\n", func_id);
    return 0;
}

void *gc_alloc(int size) {
    void *p = GC_MALLOC(size);

    /*
     * To get info about the GC discarding objects, use
     * GC_REGISTER_FINALIZER(p, fn, arg, 0, 0);
     *
     * This runs fn(p, arg) when the object at p is finalised.
     *
     * More info in gc.h of Boehm garbage collector.
     */

    return p;
}

int main() {

    /*
     * Initialising the GC is not necessary on all systems but may be required
     * on some. Better safe than sorry! See gc.h for more info.
     */
    GC_INIT();

    int *ret = (int *) inter_main();

    return *ret;
}
