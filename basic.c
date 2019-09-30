#include <stdlib.h>
#include <stdio.h>

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

#include "basic.h"
#include "parse.h"

int yyparse();

sexpr *new_sexpr(int type, contents val) {
    /* TODO: unchecked memory allocation! */
    sexpr *ret = malloc(sizeof (*ret));
    ret->type = type;
    ret->contents = val;
    return ret;
}

sexpr *new_int(int num) {
    contents c;
    c.i = num;
    return new_sexpr(INT, c);
}

sexpr *new_float(float num) {
    contents c;
    c.f = num;
    return new_sexpr(FLOAT, c);
}

sexpr *new_id(char *id) {
    contents c;
    c.s = id;
    return new_sexpr(ID, c);
}

sexpr *new_node(sexpr *l, sexpr *r) {
    contents c;
    c.n.l = l;
    c.n.r = r;
    return new_sexpr(BRANCH, c);
}

void _print_sexpr(sexpr *c) {
    if (!c) {
        printf("NIL");
    } else {
        switch (c->type) {
            case INT:
                printf("INT %i", c->contents.i);
                break;
            case FLOAT:
                printf("FLOAT %f", c->contents.f);
                break;
            case ID:
                printf("ID %s", c->contents.s);
                break;
            case BRANCH:
                printf("(");
                _print_sexpr(c->contents.n.l);
                printf(";");
                _print_sexpr(c->contents.n.r);
                printf(")");
                break;
            case NIL:
            default:
                printf("ERROR");
        }
    }
}

void print_sexpr(sexpr *c) {
    _print_sexpr(c);
    printf("\n");
}

int module_id = 0;

void codegen(sexpr *sexpr) {
    char module_name[50];
    snprintf(module_name, 50, "module_%d", module_id++);
    LLVMModuleRef module = LLVMModuleCreateWithName(module_name);

    LLVMBuilderRef builder = LLVMCreateBuilder();

    LLVMTypeRef func_type = LLVMFunctionType(LLVMInt32Type(), 0, 0, 0);
    LLVMValueRef new_func = LLVMAddFunction(module, "main", func_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(new_func, "entry");

    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef x = LLVMConstInt(LLVMInt32Type(), 4, 0);
    LLVMValueRef add_output = LLVMBuildAdd(builder, x, x, "add_output");
    LLVMBuildRet(builder, add_output);

    char *err = 0;
    LLVMVerifyModule(module, LLVMAbortProcessAction, &err);
    LLVMDisposeMessage(err);

    char *module_status = LLVMPrintModuleToString(module);
    printf("%s\n", module_status);
    LLVMDisposeMessage(module_status);

    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
}

int main() {
    yyparse();
    return 0;
}
