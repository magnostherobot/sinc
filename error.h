#ifndef _ERROR_H
#define _ERROR_H

#include <stdio.h>
#include <stdlib.h>

enum errcode {
    OK = 0,
    UNKNOWN_ID
};

#define error(v, ...) do { \
    fprintf(stderr, __VA_ARGS__); \
    exit(v); \
} while (0)

#endif /* _ERROR_H */
