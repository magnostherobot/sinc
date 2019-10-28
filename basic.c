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
#include "scope.h"

int yyparse();

sexpr *new_sexpr(int type, contents val) {
    /* FIXME: unchecked memory allocation! */
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

void _fprint_sexpr(FILE *fd, sexpr *c) {
    if (!c) {
        fprintf(fd, "NIL");
    } else {
        switch (c->type) {
            case INT:
                fprintf(fd, "INT %i", c->contents.i);
                break;
            case FLOAT:
                fprintf(fd, "FLOAT %f", c->contents.f);
                break;
            case STRING:
                fprintf(fd, "STRING %s", c->contents.s);
                break;
            case ID:
                fprintf(fd, "ID %s", c->contents.s);
                break;
            case BRANCH:
                fprintf(fd, "(");
                _fprint_sexpr(fd, c->contents.n.l);
                fprintf(fd, ";");
                _fprint_sexpr(fd, c->contents.n.r);
                fprintf(fd, ")");
                break;
            case NIL:
            default:
                fprintf(fd, "ERROR");
        }
    }
}

void fprint_sexpr(FILE *fd, sexpr *c) {
    _fprint_sexpr(fd, c);
    fprintf(fd, "\n");
}

void print_sexpr(sexpr *se) {
    fprint_sexpr(stdin, se);
}

LLVMModuleRef module;
LLVMValueRef function;
LLVMBuilderRef builder;

LLVMTypeRef print_int_t;
LLVMValueRef print_int_p;

LLVMTypeRef print_str_t;
LLVMValueRef print_str_p;

LLVMTypeRef llvm_int_t;
LLVMTypeRef llvm_str_t;

scope sc;

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

LLVMValueRef codegen_id(char *id) {
    scope_entry *entry = scope_find(sc, id);
    if (!entry) return 0;

    return entry->value;
}

LLVMValueRef _codegen(sexpr *sexpr);

int get_args_from_list(sexpr *se, LLVMValueRef *args) {
    debug("getting args from list @%p\n", (void *) se);
    if (!se) return 0;
    assert(se->type == BRANCH);

    *args = _codegen(se->contents.n.l);
    return 1 + get_args_from_list(se->contents.n.r, args + 1);
}

void get_param_info(sexpr* param_def, char **id, LLVMTypeRef *type) {
    assert(param_def);
    assert(param_def->type == BRANCH);

    sexpr *var_type_node = param_def->contents.n.l;
    sexpr *var_name_node = param_def->contents.n.r;

    assert(var_type_node);
    assert(var_type_node->type == ID);
    assert(var_name_node);
    assert(var_name_node->type == ID);

    char *var_type_str = var_type_node->contents.s;
    assert(var_type_str);

    *id = var_name_node->contents.s;

    if (!strcmp(var_type_str, "INT")) {
        *type = llvm_int_t;
    } else if (!strcmp(var_type_str, "STR")) {
        *type = llvm_str_t;
    } else {
        error(UNKNOWN_ID, "Unknown parameter type %s", var_type_str);
    }
}

int make_func_params(sexpr *params, char **ids, LLVMTypeRef *types) {
    if (!params) return 0;

    assert(params->type == BRANCH);

    sexpr *l = params->contents.n.l;
    sexpr *r = params->contents.n.r;

    assert(l);
    assert(l->type == BRANCH);

    get_param_info(l, ids, types);

    return 1 + make_func_params(r, ids + 1, types + 1);
}

LLVMTypeRef codegen_type_definition(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *title = se->contents.n.l;
    sexpr *info = se->contents.n.r;

    assert(title);
    assert(info);

    /*
     * TODO
     * Decide how to do struct declarations.
     * Would be nice to allow named and unnamed struct properties.
     *
     * What should accessing a struct's preoperties look like?
     *
     * Should it look like Haskell's property accessing? If it did, the struct
     * type would also need to be mentioned, kinda like:
     * `<struct_name> <property_name> <value>`
     * Would require doing a special thing to get the property index from its
     * name.
     *
     * Similar option is something like:
     * `<struct_name>.<property_name> <value>`
     * Where a function is defined for each struct-property combo.
     * Think I'll go with this option for now.
     */

    /* FIXME: magic sizes */
    char *prop_ids[50];
    LLVMTypeRef prop_t[50];
    int prop_c = make_func_params(info->contents.n.l, prop_ids, prop_t);

    char *struct_id = title->contents.s;

    debug("building struct %s\n", struct_id);

    /* last parameter determines if struct is packed */
    LLVMTypeRef struct_t = LLVMStructType(prop_t, prop_c, 0);

    /*
     * TODO
     * I'm not sure what the second parameter of LLVMPointerType (unsigned
     * AddressSpace) is used for:
     * https://llvm.org/doxygen/group__LLVMCCoreTypeSequential.html#ga299fe6147083678d0494b1b875f542fa
     * I'm guessing it's for restricting the address space the pointer can
     * point to, which would be a cool property to expose to front-end
     * languages.
     */
    LLVMTypeRef struct_ptr_t = LLVMPointerType(struct_t, 0);

    /*
     * Create a temporary builder for building some extra functions so that the
     * main builder doesn't lose its place. Disposed of at end of scope.
     */
    LLVMBuilderRef util_builder = LLVMCreateBuilder();

    /*
     * Make a function for constructing an instance of this struct type. Takes
     * each of the struct's properties as an individual argument.
     */
    LLVMTypeRef ctor_t = LLVMFunctionType(struct_ptr_t, prop_t, prop_c, 0);
    LLVMValueRef ctor_p = LLVMAddFunction(module, struct_id, ctor_t);
    LLVMBasicBlockRef ctor_b = LLVMAppendBasicBlock(ctor_p, "entry");
    LLVMPositionBuilderAtEnd(util_builder, ctor_b);
    /* FIXME: memory leak */
    LLVMValueRef src = LLVMBuildMalloc(util_builder, struct_t, "src");
    for (int i = 0; i < prop_c; i++) {
        LLVMValueRef prop_pos = LLVMBuildStructGEP(util_builder, src, i, "pos");
        LLVMValueRef param = LLVMGetParam(ctor_p, i);
        LLVMBuildStore(util_builder, param, prop_pos);
    }
    LLVMBuildRet(util_builder, src);
    scope_add_entry(sc, struct_id, ctor_p, ctor_t);

    /*
     * Make functions to retreive each property of the struct.
     */
    /* FIXME: magic size */
    char prop_func_id[50];
    for (int i = 0; i < prop_c; i++) {
        /* TODO: maybe choose a different separator character? */
        snprintf(prop_func_id, 50, "%s.%s", struct_id, prop_ids[i]);

        LLVMTypeRef param_t[] = { struct_ptr_t };
        LLVMTypeRef prop_func_t = LLVMFunctionType(prop_t[i], param_t, 1, 0);
        LLVMValueRef prop_func_p =
            LLVMAddFunction(module, prop_func_id, prop_func_t);
        LLVMBasicBlockRef prop_func_b =
            LLVMAppendBasicBlock(prop_func_p, "entry");
        LLVMPositionBuilderAtEnd(util_builder, prop_func_b);
        LLVMValueRef param = LLVMGetParam(prop_func_p, 0);
        LLVMValueRef ptr =
            LLVMBuildStructGEP(util_builder, param, i, "part");
        LLVMValueRef ret = LLVMBuildLoad(util_builder, ptr, "deref");
        LLVMBuildRet(util_builder, ret);

        debug("adding %s to scope\n", prop_func_id);
        scope_add_entry(sc, prop_func_id, prop_func_p, prop_func_t);
    }
    LLVMDisposeBuilder(util_builder);

    return struct_t;
}

LLVMValueRef codegen_definition(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *title = se->contents.n.l;
    sexpr *info = se->contents.n.r;

    assert(title);
    assert(info);

    // TODO I'm finding it really hard to figure out how I should do this, I
    // want to add this function's argument names to the scope table, but also I
    // need the function's types first
    // The function's arg types will be declared with the args themselves, like
    //     [(int x) (str y) (int z)]
    // For now don't make a general thing, just get definitions to work

    char *func_id;
    LLVMTypeRef ret_type;
    get_param_info(title, &func_id, &ret_type);

    /* FIXME: magic sizes */
    char *ids[50];
    LLVMTypeRef param_t[50];
    int param_c = make_func_params(info->contents.n.l, ids, param_t);

    debug("building function %s\n", func_id);

    LLVMTypeRef func_t = LLVMFunctionType(ret_type, param_t, param_c, 0);
    LLVMValueRef func_p = LLVMAddFunction(module, func_id, func_t);
    LLVMBasicBlockRef func_b = LLVMAppendBasicBlock(func_p, "entry");
    LLVMPositionBuilderAtEnd(builder, func_b);

    function = func_p;

    scope_add_entry(sc, func_id, func_p, func_t);

    scope_push_layer(&sc);
    for (int i = 0; i < param_c; i++) {
        scope_add_entry(sc, ids[i], LLVMGetParam(func_p, i), param_t[i]);
    }

    assert(!info->contents.n.r->contents.n.r);
    LLVMValueRef ret = _codegen(info->contents.n.r->contents.n.l);
    LLVMBuildRet(builder, ret);

    scope_pop_layer(&sc);

    debug("built function %s\n", func_id);

    return func_p;
}

LLVMValueRef codegen_from_scope(char *func_id, LLVMValueRef *args, int arg_c) {
    scope_entry *entry = scope_find(sc, func_id);
    if (!entry) return 0;

    LLVMValueRef res =
        LLVMBuildCall2(builder, entry->type, entry->value, args, arg_c, "");

    return res;
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
    } else if (!strcmp(func_id, "-")) {
        assert(arg_c == 2);
        res = LLVMBuildSub(builder, args_v[0], args_v[1], "sub_out");
    }

    return res;
}

/*
 * To implement lazy evaluation, I could implement thunks
 * Closures would require using a struct to keep track of variable's values from
 * when the closure was created
 */

LLVMValueRef codegen_invocation(sexpr *se) {
    assert(se);

    sexpr *l = se->contents.n.l;
    sexpr *r = se->contents.n.r;

    assert(l->type == ID);

    char *func_id = l->contents.s;

    /*
     * args_t may not be needed as we can just get the types of values from the
     * function declaration. Who knows, really.
     */
    /* FIXME: magic values */
    LLVMValueRef args[50];
    int arg_c = get_args_from_list(r, args);

    LLVMValueRef res = codegen_from_scope(func_id, args, arg_c);
    if (!res) res = codegen_inbuilt_functions(se, arg_c, args);

    if (!res) error(UNKNOWN_ID, "unrecognised ID %s", func_id);


    return res;
}

LLVMValueRef codegen_conditional(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *cond_ast = se->contents.n.l;
    assert(cond_ast);

    se = se->contents.n.r;
    assert(se);
    assert(se->type == BRANCH);

    sexpr *then_ast = se->contents.n.l;
    assert(then_ast);

    se = se->contents.n.r;
    assert(se);
    assert(se->type == BRANCH);

    sexpr *else_ast = se->contents.n.l;
    assert(else_ast);

    assert(!se->contents.n.r);

    LLVMBasicBlockRef then_b = LLVMAppendBasicBlock(function, "then");
    LLVMBasicBlockRef else_b = LLVMAppendBasicBlock(function, "else");
    LLVMBasicBlockRef done_b = LLVMAppendBasicBlock(function, "done");

    LLVMValueRef llvm_zero_v = LLVMConstInt(llvm_int_t, 0, 0);
    LLVMValueRef cond_res = _codegen(cond_ast);
    LLVMValueRef cond_v =
        LLVMBuildICmp(builder, LLVMIntNE, cond_res, llvm_zero_v, "neq_0");
    LLVMBuildCondBr(builder, cond_v, then_b, else_b);

    LLVMPositionBuilderAtEnd(builder, then_b);
    LLVMValueRef then_v = _codegen(then_ast);
    LLVMBuildBr(builder, done_b);

    LLVMPositionBuilderAtEnd(builder, else_b);
    LLVMValueRef else_v = _codegen(else_ast);
    LLVMBuildBr(builder, done_b);

    LLVMPositionBuilderAtEnd(builder, done_b);
    LLVMValueRef phi_p = LLVMBuildPhi(builder, llvm_int_t, "if_res");
    LLVMBasicBlockRef phi_bs[] = { then_b, else_b };
    LLVMValueRef phi_vs[] = { then_v, else_v };
    LLVMAddIncoming(phi_p, phi_vs, phi_bs, 2);

    return phi_p;
}

LLVMValueRef codegen_branch(sexpr *se) {
    debug("codegenning branch @%p\n", (void *) se);

    assert(se);

    sexpr *l = se->contents.n.l;
    sexpr *r = se->contents.n.r;

    LLVMValueRef res;

    if (l->type == ID) {
        if (!strcmp(l->contents.s, "def")) {
            res = codegen_definition(r);
        } else if (!strcmp(l->contents.s, "type")) {
            codegen_type_definition(r);
        } else if (!strcmp(l->contents.s, "if")) {
            res = codegen_conditional(r);
        } else {
            res = codegen_invocation(se);
        }
    } else {
        fprint_sexpr(stderr, se);
        error(SYNTAX_ERROR, "don't know what to do with above AST");

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
            return codegen_id(sexpr->contents.s);
        case BRANCH:
            return codegen_branch(sexpr);
        default:
            printf("erm\n");
            return 0;
    }
}

void codegen(sexpr *sexpr) {
    _codegen(sexpr);
}

void prologue() {
    sc = 0;
    scope_push_layer(&sc);

    llvm_int_t = LLVMInt32Type();
    llvm_str_t = LLVMPointerType(LLVMInt8Type(), 0);

    module = LLVMModuleCreateWithName("main_module");
    builder = LLVMCreateBuilder();

    /* add print_int */
    LLVMTypeRef print_int_params[] = { llvm_int_t };
    print_int_t = LLVMFunctionType(LLVMInt32Type(), print_int_params, 1, 0);
    print_int_p = LLVMAddFunction(module, "debug_int", print_int_t);
    scope_add_entry(sc, "debug_int", print_int_p, print_int_t);

    /* add print_str */
    LLVMTypeRef print_str_params[] = { llvm_str_t };
    print_str_t = LLVMFunctionType(LLVMInt32Type(), print_str_params, 1, 0);
    print_str_p = LLVMAddFunction(module, "debug_str", print_str_t);
    scope_add_entry(sc, "debug_str", print_str_p, print_str_t);
}

void epilogue() {
    /* LLVMVerifyModule(module, LLVMAbortProcessAction, 0); */

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
