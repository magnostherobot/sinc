#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>
#include <stdlib.h>

enum errcode {
    OK = 0,
    GENERAL_ERROR = 1,
    COMMAND_NOT_FOUND = 2,
    UNKNOWN_ID,
    SYNTAX_ERROR,
    INVALID_ARGUMENTS
};

/* FIXME: double fprintf is ugly */
#define error(v, ...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    exit(v); \
} while (0)

#define _warn(fmt, ...) do { \
    fprintf(stderr, fmt "\n", __VA_ARGS__); \
} while (0)

#define warn(...) do { \
    _warn(__VA_ARGS__); \
} while (0)

#endif /* ERROR_H */
