#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

extern int verbose;

#define debug(...) do { \
    if (verbose) { \
        fprintf(stderr, __VA_ARGS__); \
    } \
} while (0)

#endif /* DEBUG_H */
