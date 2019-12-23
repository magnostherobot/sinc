#ifndef SCOPE_H
#define SCOPE_H

#include <llvm-c/Core.h>

typedef struct scope_entry {
    char *id;
    LLVMValueRef value;
    LLVMTypeRef type;
    LLVMMetadataRef metadata;
    struct scope_entry *prev;
} scope_entry;

typedef struct scope_layer {
    struct scope_entry *head;
    struct scope_layer *prev;
} scope_layer;

typedef scope_layer *scope;

void scope_push_layer(scope *sc);
void scope_pop_layer(scope *sc);

void scope_add_entry(scope sc, char *id, LLVMValueRef val, LLVMTypeRef type);
scope_entry *scope_find(scope sc, char *id);

#endif /* SCOPE_H */
