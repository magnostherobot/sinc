#include "debug.h"

char *debug_int(char *i) {
    int *p = (int *) i;
    debug("%d\n", *p);
    return i;
}

char *debug_str(char *s) {
    debug("%s\n", s);
    return s;
}

int int_if(int p, int a, int b) {
    return p ? a : b;
}
