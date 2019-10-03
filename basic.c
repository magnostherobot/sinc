#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

#include "basic.h"
#include "parse.h"
#include "debug.h"
#include "error.h"

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

sexpr *new_str(char *str) {
    contents c;
    c.s = str;
    return new_sexpr(STRING, c);
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
            case STRING:
                printf("STRING %s", c->contents.s);
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

LLVMModuleRef module;
LLVMBuilderRef builder;

LLVMTypeRef print_int_t;
LLVMValueRef print_int_p;

LLVMTypeRef print_str_t;
LLVMValueRef print_str_p;

LLVMTypeRef llvm_int_t;
LLVMTypeRef llvm_str_t;

LLVMValueRef codegen_print_int(LLVMValueRef ref) {
    LLVMValueRef args[] = { ref };
    LLVMValueRef ret =
        LLVMBuildCall2(builder, print_int_t, print_int_p, args, 1, "");
    return ret;
}

LLVMValueRef codegen_int(sexpr *sexpr) {
    debug("codegenning %d\n", sexpr->contents.i);
    LLVMValueRef ret = LLVMConstInt(llvm_int_t, sexpr->contents.i, 0);
    debug("codegenned %d\n", sexpr->contents.i);

    return ret;
}

LLVMValueRef codegen_id(sexpr *sexpr) {
    /* LLVMValueRef args[] = { */
    /*     LLVMBuildGlobalStringPtr(builder, sexpr->contents.s, "") */
    /* }; */
    /* LLVMValueRef ret = */
    /*     LLVMBuildCall2(builder, print_str_t, print_str_p, args, 1, ""); */

    debug("codegenning '%s'\n", sexpr->contents.s);
    LLVMValueRef ret = LLVMBuildGlobalStringPtr(builder, sexpr->contents.s, "");
    debug("codegenned '%s'\n", sexpr->contents.s);

    return ret;
}

LLVMValueRef _codegen(sexpr *sexpr);

int get_args_from_list(sexpr *se, LLVMValueRef *args_v, LLVMTypeRef *args_t) {
    debug("getting args from list @%p\n", (void *) se);
    if (!se) return 0;
    assert(se->type == BRANCH);

    *args_v = _codegen(se->contents.n.l);
    return 1 + get_args_from_list(se->contents.n.r, args_v + 1, args_t + 1);
}

LLVMValueRef codegen_inbuilt_functions(sexpr *se, int arg_c, LLVMValueRef
        *args_v) {

    assert(se);

    sexpr *l = se->contents.n.l;
    /* sexpr *r = se->contents.n.r; */

    assert(l->type == ID);

    char *func_id = l->contents.s;

    LLVMValueRef res = 0;

    if (!strcmp(func_id, "+")) {
        assert(arg_c == 2);
        res = LLVMBuildAdd(builder, args_v[0], args_v[1], "add_out");
    } else if (!strcmp(func_id, "*")) {
        assert(arg_c == 2);
        res = LLVMBuildMul(builder, args_v[0], args_v[1], "mul_out");
    }

    return res;
}

LLVMValueRef codegen_branch(sexpr *se) {
    debug("codegenning branch @%p\n", (void *) se);

    assert(se);

    sexpr *l = se->contents.n.l;
    sexpr *r = se->contents.n.r;

    assert(l->type == ID);

    /*
     * args_t may not be needed as we can just get the types of values from the
     * function declaration. Who knows, really.
     */
    // TODO: magic values
    LLVMValueRef args_v[50];
    LLVMTypeRef  args_t[50];
    int arg_c = get_args_from_list(r, args_v, args_t);

    LLVMValueRef res = codegen_inbuilt_functions(se, arg_c, args_v);

    if (!res) {
        error(UNKNOWN_ID, "unrecognised ID %s", l->contents.s);
    }

    debug("codegenned branch @%p\n", (void *) se);
    return res;
}

LLVMValueRef _codegen(sexpr *sexpr) {
    switch (sexpr->type) {
        case NIL:
        case INT:
            return codegen_int(sexpr);
        case ID:
            return codegen_id(sexpr);
        case BRANCH:
            return codegen_branch(sexpr);
        default:
            printf("erm\n");
            return 0;
    }
}

void codegen(sexpr *sexpr) {
    LLVMValueRef vr = _codegen(sexpr);
    codegen_print_int(vr);
}

void prologue() {
    llvm_int_t = LLVMInt32Type();
    llvm_str_t = LLVMPointerType(LLVMInt8Type(), 0);

    module = LLVMModuleCreateWithName("main_module");
    builder = LLVMCreateBuilder();

    /* add print_int */
    LLVMTypeRef print_int_params[] = { llvm_int_t };
    print_int_t = LLVMFunctionType(LLVMVoidType(), print_int_params, 1, 0);
    print_int_p = LLVMAddFunction(module, "print_int", print_int_t);

    /* add print_str */
    LLVMTypeRef print_str_params[] = { llvm_str_t };
    print_str_t = LLVMFunctionType(LLVMVoidType(), print_str_params, 1, 0);
    print_str_p = LLVMAddFunction(module, "print_str", print_str_t);

    /* add main */
    LLVMTypeRef main_type = LLVMFunctionType(llvm_int_t, 0, 0, 0);
    LLVMValueRef main_prototype = LLVMAddFunction(module, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_prototype, "entry");

    LLVMPositionBuilderAtEnd(builder, entry);
}

void epilogue() {
    LLVMValueRef main_ret = LLVMConstInt(llvm_int_t, 0, 0);
    LLVMBuildRet(builder, main_ret);

    LLVMVerifyModule(module, LLVMAbortProcessAction, 0);

    char *module_status = LLVMPrintModuleToString(module);
    printf("%s\n", module_status);
    LLVMDisposeMessage(module_status);

    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
}

int main() {
    prologue();
    yyparse();
    epilogue();
    return 0;
}
