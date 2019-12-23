#ifndef LLVM_CODEGEN_H
#define LLVM_CODEGEN_H

#include "sinter.h"

void llvm_codegen_prologue(char *filename);
void llvm_codegen(sexpr *ast);
int llvm_codegen_epilogue(char *output_filename, format_t format);

#endif /* LLVM_CODEGEN_H */
