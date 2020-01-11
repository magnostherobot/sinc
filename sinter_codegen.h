#ifndef SINTER_CODEGEN_H
#define SINTER_CODEGEN_H

#include "sinc.h"

void sinter_codegen_prologue();
void sinter_codegen(sexpr *ast);
int sinter_codegen_epilogue(char *output_filename);

#endif /* SINTER_CODEGEN_H */
