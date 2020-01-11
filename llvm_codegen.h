#ifndef LLVM_CODEGEN_H
#define LLVM_CODEGEN_H

#include "sinter.h"

typedef enum {
    ALWAYS_BOX,
    NEVER_BOX,
    SMART_BOX
} boxing_rule_t;

void llvm_codegen_prologue(char *filename, boxing_rule_t boxing_rule, uint
        default_bit_width);

void llvm_codegen(sexpr *ast);

int llvm_codegen_epilogue(char *output_filename, format_t format);

#endif /* LLVM_CODEGEN_H */
