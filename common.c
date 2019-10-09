#include "debug.h"

int debug_int(int i) {
    debug("%d\n", i);
    return i;
}

char *debug_str(char *s) {
    debug("%s\n", s);
    return s;
}

int int_if(int p, int a, int b) {
    return p ? a : b;
}
