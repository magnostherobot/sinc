#ifndef GRAPHVIZ_CODEGEN_H
#define GRAPHVIZ_CODEGEN_H

#include "sinc.h"

void graphviz_codegen_prologue();
void graphviz_codegen(sexpr *ast);
int graphviz_codegen_epilogue(char *output_filename);

#endif /* GRAPHVIZ_CODEGEN_H */
