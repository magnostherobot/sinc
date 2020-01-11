#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

#include <gc.h>

int count_solns(int n);

void *gc_alloc(size_t size) {
    void *p = GC_MALLOC(size);

    /*
     * To get info about the GC discarding objects, use
     * GC_REGISTER_FINALIZER(p, fn, arg, 0, 0);
     *
     * This runs fn(p, arg) when the object at p is finalised.
     *
     * More info in gc.h of Boehm garbage collector.
     */

    assert(p);
    return p;
}

int main(int argc, char **argv) {

    /*
     * Initialising the GC is not necessary on all systems but may be required
     * on some. Better safe than sorry! See gc.h for more info.
     */
    GC_INIT();

    if (argc != 2) {
        printf("usage: %s n\n", argv[0]);

    } else {
        int n = atoi(argv[1]);
        int n_solns = count_solns(n);
        printf("%i solutions found\n", n_solns);

    }

    return 0;
}
