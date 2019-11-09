#include <stddef.h>
#include <gc.h>

#include "debug.h"

void *inter_main(void);

void *debug_int(void *i) {
    int *p = (int *) i;
    debug("%d\n", *p);
    return i;
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

    return (int) inter_main();
}
