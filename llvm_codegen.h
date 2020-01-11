#ifndef LLVM_CODEGEN_H
#define LLVM_CODEGEN_H

#include <stdbool.h>

#include "sinc.h"

typedef enum {
    ALWAYS_BOX,
    NEVER_BOX,
    SMART_BOX
} boxing_rule_t;

typedef struct {
    boxing_rule_t boxing_rule;
    uint default_bit_width;
    bool tail_recursive_mod_cons;
} optimisation_t;

void llvm_codegen_prologue(char *filename, optimisation_t *optimisations);
void llvm_codegen(sexpr *ast);
int llvm_codegen_epilogue(char *output_filename, format_t format);

#endif /* LLVM_CODEGEN_H */
