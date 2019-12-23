#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stddef.h>

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/DebugInfo.h>

#include "llvm_codegen.h"
#include "sinter.h"
#include "scope.h"
#include "debug.h"
#include "parse.h"
#include "error.h"

#define LLVMDIBuilderFinalise LLVMDIBuilderFinalize

typedef void *(*main_func_t)(void);

static LLVMModuleRef module;
static LLVMValueRef function;
static LLVMBuilderRef builder;
static LLVMContextRef global_context;

static LLVMBasicBlockRef builder_block;

static LLVMTypeRef gc_alloc_t;
static LLVMValueRef gc_alloc_p;

static LLVMTypeRef print_int_t;
static LLVMValueRef print_int_p;

static LLVMTypeRef debug_func_call_t;
static LLVMValueRef debug_func_call_p;

static LLVMTypeRef print_str_t;
static LLVMValueRef print_str_p;

static LLVMTypeRef llvm_int_t;
static LLVMTypeRef llvm_str_t;
static LLVMTypeRef boxed_t;

static LLVMDIBuilderRef di_builder;
static LLVMMetadataRef di_file;
static LLVMMetadataRef di_cu;

static LLVMMetadataRef boxed_t_meta;
static LLVMMetadataRef last_func_m;

static scope sc;

void put_builder_at_end(LLVMBuilderRef b, LLVMBasicBlockRef block) {
    LLVMPositionBuilderAtEnd(b, block);
    if (b == builder) {
        builder_block = block;
    }
}

LLVMValueRef box_val(LLVMBuilderRef b, LLVMValueRef val, LLVMTypeRef type) {
    /* FIXME: memory leak */
    /* LLVMValueRef box = LLVMBuildMalloc(b, type, "box"); */
    LLVMValueRef size = LLVMSizeOf(type);
    LLVMValueRef size_cast = LLVMConstIntCast(size, llvm_int_t, 0);
    LLVMTypeRef ptr_type = LLVMPointerType(type, 0);

    LLVMValueRef box =
        LLVMBuildCall2(b, gc_alloc_t, gc_alloc_p, &size_cast, 1, "boxed");
    LLVMValueRef box_cast = LLVMBuildPointerCast(b, box, ptr_type, "box_cast");
    LLVMBuildStore(b, val, box_cast);

    return box;
}

LLVMValueRef unbox_val(LLVMBuilderRef b, LLVMValueRef box, LLVMTypeRef type) {
    LLVMTypeRef ptr_type = LLVMPointerType(type, 0);
    LLVMValueRef uncast = LLVMBuildPointerCast(b, box, ptr_type, "uncast");
    LLVMValueRef val = LLVMBuildLoad2(b, type, uncast, "unboxed");
    return val;
}

LLVMValueRef codegen_print_int(LLVMValueRef ref) {
    LLVMValueRef args[] = { ref };
    LLVMValueRef ret =
        LLVMBuildCall2(builder, print_int_t, print_int_p, args, 1, "");
    return ret;
}

LLVMValueRef codegen_int(sexpr *sexpr) {
    LLVMValueRef val =
        LLVMConstInt(llvm_int_t, (unsigned) sexpr->contents.i, 0);
    LLVMValueRef box = box_val(builder, val, llvm_int_t);
    return box;
}

LLVMValueRef codegen_id(char *id) {
    scope_entry *entry = scope_find(sc, id);
    if (!entry) return 0;

    LLVMValueRef val = entry->value;

    /*
     * all uses of this function want a value, and since all values are assumed
     * to be boxed right now, it should be safe to cast function pointers to i8*
     */
    //if (entry->type == boxed_t) {
        val = LLVMBuildPointerCast(builder, val, boxed_t, "func_generic_cast");
    //}

    return val;
}

LLVMTypeRef make_function_type(unsigned param_c, LLVMBool vararg) {

    /* FIXME: memory leak */
    LLVMTypeRef *boxed_params = malloc((sizeof (LLVMTypeRef)) * param_c);
    for (uint i = 0; i < param_c; i++) {
        boxed_params[i] = boxed_t;
    }

    return LLVMFunctionType(boxed_t, boxed_params, param_c, vararg);
}

LLVMTypeRef make_struct_type(char *id, uint param_c, LLVMBool packed) {

    /* FIXME: memory leak */
    LLVMTypeRef *boxed_props = malloc((sizeof (LLVMTypeRef)) *param_c);
    for (uint i = 0; i < param_c; i++) {
        boxed_props[i] = boxed_t;
    }

    LLVMTypeRef struct_t = LLVMStructCreateNamed(global_context, id);
    LLVMStructSetBody(struct_t, boxed_props, param_c, packed);
    return struct_t;
}

LLVMValueRef _codegen(sexpr *sexpr);

uint get_args_from_list(sexpr *se, LLVMValueRef *args) {
    debug("getting args from list @%p\n", (void *) se);
    if (!se) return 0;
    assert(se->type == BRANCH);

    *args = _codegen(se->contents.n.l);

    return 1 + get_args_from_list(se->contents.n.r, args + 1);
}

int get_type_param(sexpr *param, char **id, LLVMTypeRef *type) {
    assert(param);
    assert(param->type == BRANCH);

    sexpr *type_se = param->contents.n.l;
    sexpr *id_se = param->contents.n.r;

    *id = id_se->contents.s;

    char *type_str = type_se->contents.s;
    if (!strcmp(type_str, "INT")) {
        *type = llvm_int_t;
    } else if (!strcmp(type_str, "STR")) {
        *type = llvm_str_t;
    } else {

        /*
         * FIXME
         * For now, if we don't recognise the type identifier, we assume it's a
         * user-defined type (aka a boxed value)
         */
        *type = boxed_t;
    }

    debug("type element %s with type %s\n", *id, type_str);

    return 0;
}

uint make_type_params(sexpr *params, char **ids) {
    if (!params) return 0;

    assert(params->type == BRANCH);
    sexpr *l = params->contents.n.l;
    sexpr *r = params->contents.n.r;

    assert(l->type == ID);
    *ids = l->contents.s;

    return 1 + make_type_params(r, ids + 1);
}

uint make_func_params(sexpr *params, char **ids, lloc_t **llocs) {
    if (!params) return 0;

    assert(params->type == BRANCH);
    sexpr* l = params->contents.n.l;
    sexpr* r = params->contents.n.r;

    assert(l->type == ID);
    *ids = l->contents.s;
    *llocs = &l->lloc;

    return 1 + make_func_params(r, ids + 1, llocs + 1);
}

LLVMValueRef codegen_type_definition(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *title = se->contents.n.l;
    sexpr *info = se->contents.n.r;

    assert(title);
    assert(info);

    /* FIXME: magic sizes */
    char *prop_ids[50];
    uint prop_c = make_type_params(info->contents.n.l, prop_ids);

    char *struct_id = title->contents.s;

    debug("building struct %s\n", struct_id);

    /* last parameter determines if struct is packed */
    LLVMTypeRef struct_t = make_struct_type(struct_id, prop_c, 0);

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
    LLVMBuilderRef util_b = LLVMCreateBuilder();

    /*
     * Make a function for constructing an instance of this struct type. Takes
     * each of the struct's properties as an individual argument.
     */
    LLVMTypeRef ctor_t = make_function_type(prop_c, 0);
    LLVMValueRef ctor_p = LLVMAddFunction(module, struct_id, ctor_t);
    LLVMBasicBlockRef ctor_b = LLVMAppendBasicBlock(ctor_p, "entry");
    put_builder_at_end(util_b, ctor_b);
    /* FIXME: memory leak */
    LLVMValueRef src = LLVMBuildMalloc(util_b, struct_t, "src");
    for (uint i = 0; i < prop_c; i++) {
        LLVMValueRef prop_pos = LLVMBuildStructGEP(util_b, src, i, "pos");
        LLVMValueRef param = LLVMGetParam(ctor_p, i);
        LLVMBuildStore(util_b, param, prop_pos);
    }
    LLVMValueRef cast = LLVMBuildPointerCast(util_b, src, boxed_t, "cast");
    LLVMBuildRet(util_b, cast);
    scope_add_entry(sc, struct_id, ctor_p, ctor_t);

    /*
     * Make functions to retreive each property of the struct.
     */
    /* FIXME: magic size */
    char prop_func_id[50];
    for (uint i = 0; i < prop_c; i++) {
        /* TODO: maybe choose a different separator character? */
        snprintf(prop_func_id, 50, "%s.%s", struct_id, prop_ids[i]);

        LLVMTypeRef prop_func_t = make_function_type(1, 0);
        LLVMValueRef prop_func_p =
            LLVMAddFunction(module, prop_func_id, prop_func_t);
        LLVMBasicBlockRef prop_func_b =
            LLVMAppendBasicBlock(prop_func_p, "entry");
        put_builder_at_end(util_b, prop_func_b);
        LLVMValueRef param = LLVMGetParam(prop_func_p, 0);
        LLVMValueRef uncast =
            LLVMBuildPointerCast(util_b, param, struct_ptr_t, "uncast");
        LLVMValueRef ptr =
            LLVMBuildStructGEP2(util_b, struct_t, uncast, i, "part");
        LLVMValueRef val = LLVMBuildLoad(util_b, ptr, "deref");
        LLVMBuildRet(util_b, val);

        debug("adding %s to scope\n", prop_func_id);
        scope_add_entry(sc, prop_func_id, prop_func_p, prop_func_t);
    }
    LLVMDisposeBuilder(util_b);

    return ctor_p;
}

LLVMValueRef codegen_declaration(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *title = se->contents.n.l;
    sexpr *info = se->contents.n.r;

    assert(title);
    assert(info);

    char *func_id = title->contents.s;

    /* FIXME: magic sizes */
    char *ids[50];
    lloc_t *llocs[50];
    uint param_c = make_func_params(info->contents.n.l, ids, llocs);

    debug("building function %s\n", func_id);

    LLVMTypeRef func_t = make_function_type(param_c, 0);
    LLVMValueRef func_p = LLVMAddFunction(module, func_id, func_t);

    scope_add_entry(sc, func_id, func_p, func_t);

    return func_p;
}

LLVMMetadataRef debug_make_subprogram(LLVMDIBuilderRef di_builder,
        LLVMMetadataRef scope, char *id, LLVMMetadataRef di_file, lloc_t *lloc,
        LLVMTypeRef type) {

    debug("%p\n", (void *) scope);

    LLVMDIFlags flags = 0;

    uint param_c = LLVMCountParamTypes(type);
    /* FIXME: magic size */
    LLVMMetadataRef func_param_types[20];
    for (uint i = 0; i <= param_c; i++) {
        func_param_types[i] = boxed_t_meta;
    }

    LLVMMetadataRef type_meta = LLVMDIBuilderCreateSubroutineType(di_builder,
            di_file, func_param_types, param_c + 1, flags);

    size_t id_len = strlen(id);
    uint lineno = lloc->first_line;
    int local = 0;
    int definition = 1;
    uint scope_line = lloc->first_line;
    int optimised = 0;

    last_func_m = LLVMDIBuilderCreateFunction(di_builder, scope, id, id_len, id,
            id_len, di_file, lineno, type_meta, local, definition, scope_line,
            flags, optimised);

    return last_func_m;
}

LLVMMetadataRef debug_make_parameter(LLVMDIBuilderRef di_builder,
        LLVMMetadataRef scope, char *id, uint i, LLVMMetadataRef di_file,
        lloc_t *lloc) {

    size_t id_len = strlen(id);
    uint lineno = lloc->first_line;
    LLVMMetadataRef type = boxed_t_meta;
    int preserve = 0;
    int flags = 0;

    return LLVMDIBuilderCreateParameterVariable(di_builder, scope, id, id_len,
            i, di_file, lineno, type, preserve, flags);
}

LLVMMetadataRef debug_make_location(LLVMContextRef context, lloc_t *lloc,
        LLVMMetadataRef scope) {

    uint lineno = lloc->first_line;
    uint colno = lloc->first_column;

    return LLVMDIBuilderCreateDebugLocation(context, lineno, colno, scope, 0);
}

LLVMValueRef codegen_definition(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *title = se->contents.n.l;
    sexpr *info = se->contents.n.r;

    assert(title);
    assert(info);

    char *func_id = title->contents.s;
    debug("%s\n", func_id);

    scope_entry *entry = scope_find(sc, func_id);
    if (!entry) {
        codegen_declaration(se);
        entry = scope_find(sc, func_id);
    }

    /* FIXME: magic sizes */
    char *ids[50];
    lloc_t *llocs[50];
    uint param_c = make_func_params(info->contents.n.l, ids, llocs);
    LLVMValueRef func_p = entry->value;
    LLVMTypeRef func_t = entry->type;

    assert(param_c == LLVMCountParamTypes(entry->type));

    LLVMBasicBlockRef func_b = LLVMAppendBasicBlock(func_p, "entry");

    /* create debug info for function */
    LLVMMetadataRef func_m = debug_make_subprogram(di_builder, di_file, func_id,
            di_file, &title->lloc, func_t);
    entry->metadata = func_m;
    LLVMSetSubprogram(func_p, func_m);

    /* create debug info for function's parameters */
    for (uint i = 0; i < param_c; i++) {
        LLVMValueRef param_v = LLVMGetParam(func_p, i);
        LLVMMetadataRef param_m = debug_make_parameter(di_builder, func_m,
                ids[i], i + 1, di_file, llocs[i]);
        LLVMMetadataRef param_loc = debug_make_location(global_context,
                llocs[i], func_m);
        LLVMMetadataRef diexpr = LLVMDIBuilderCreateExpression(di_builder, 0,
                0);
        LLVMDIBuilderInsertDbgValueAtEnd(di_builder, param_v, param_m, diexpr,
                param_loc, func_b);
    }

    put_builder_at_end(builder, func_b);

    LLVMMetadataRef loc =
        debug_make_location(global_context, &title->lloc, func_m);

    /*
     * LLVM 9 onwards has LLVMSetCurrentDebugLocation2, and this call is
     * deprecated
     */
    LLVMSetCurrentDebugLocation(builder, LLVMMetadataAsValue(global_context,
                loc));

/*     LLVMValueRef func_name = */
/*         LLVMBuildGlobalString(builder, func_id, "func_name"); */
/*     LLVMValueRef f_n_cast = */
/*         LLVMBuildPointerCast(builder, func_name, boxed_t, "func_name_cast"); */
/*     LLVMBuildCall2(builder, debug_func_call_t, debug_func_call_p, &f_n_cast, 1, */
/*         ""); */

    function = func_p;

    scope_push_layer(&sc);
    for (uint i = 0; i < param_c; i++) {
        scope_add_entry(sc, ids[i], LLVMGetParam(func_p, i), boxed_t);
    }

    assert(!info->contents.n.r->contents.n.r);
    LLVMValueRef ret = _codegen(info->contents.n.r->contents.n.l);
    LLVMBuildRet(builder, ret);

    scope_pop_layer(&sc);

    debug("built function %s\n", func_id);

    return func_p;
}

LLVMValueRef codegen_from_scope(char *func_id, LLVMValueRef *args, uint arg_c) {
    scope_entry *entry = scope_find(sc, func_id);
    if (!entry) return 0;

    LLVMValueRef func_p = entry->value;
    LLVMTypeRef func_t = entry->type;

    /*
     * in the case that the function being called is passed in as an argument,
     * we cast it to the function kind that we're expecting.
     */
    if (func_t == boxed_t) {
        LLVMTypeRef exp_func_t = make_function_type(arg_c, 0);
        LLVMTypeRef exp_func_ptr_t = LLVMPointerType(exp_func_t, 0);
        LLVMValueRef casted = LLVMBuildPointerCast(builder, entry->value,
            exp_func_ptr_t, "func_cast");
        func_t = exp_func_t;
        func_p = casted;
    }

    LLVMValueRef res =
        LLVMBuildCall2(builder, func_t, func_p, args, arg_c, "");

    return res;
}

/* TODO: maybe make a function? */
#define codegen_binop(b, x, y, op) do { \
    assert(arg_c == 2); \
    LLVMValueRef deref_x = unbox_val(b, x, llvm_int_t); \
    LLVMValueRef deref_y = unbox_val(b, y, llvm_int_t); \
    LLVMValueRef op_res = op(b, deref_x, deref_y, "op_res"); \
    LLVMValueRef res_box = box_val(b, op_res, llvm_int_t); \
    res = res_box; \
} while (0)

LLVMValueRef codegen_inbuilt_functions(sexpr *se, uint arg_c, LLVMValueRef
        *args_v) {

    assert(se);

    sexpr *l = se->contents.n.l;
    /* sexpr *r = se->contents.n.r; */

    assert(l->type == ID);

    char *func_id = l->contents.s;

    LLVMValueRef res = 0;

    if (!strcmp(func_id, "+")) {
        codegen_binop(builder, args_v[0], args_v[1], LLVMBuildAdd);
    } else if (!strcmp(func_id, "*")) {
        codegen_binop(builder, args_v[0], args_v[1], LLVMBuildMul);
    } else if (!strcmp(func_id, "-")) {
        codegen_binop(builder, args_v[0], args_v[1], LLVMBuildSub);
    } else if (!strcmp(func_id, "/")) {
        codegen_binop(builder, args_v[0], args_v[1], LLVMBuildSDiv);
    } else if (!strcmp(func_id, "%")) {
        codegen_binop(builder, args_v[0], args_v[1], LLVMBuildSRem);
    }

    return res;
}

LLVMValueRef codegen_invocation(sexpr *se) {
    assert(se);

    sexpr *l = se->contents.n.l;
    sexpr *r = se->contents.n.r;

    assert(l->type == ID);

    char *func_id = l->contents.s;
    debug("invoking %s\n", func_id);

    /* FIXME: magic values */
    LLVMValueRef args[50];
    uint arg_c = get_args_from_list(r, args);

    LLVMMetadataRef loc =
        debug_make_location(global_context, &l->lloc, last_func_m);

    /*
     * LLVM 9 onwards has LLVMSetCurrentDebugLocation2, and this call is
     * deprecated
     */
    LLVMSetCurrentDebugLocation(builder, LLVMMetadataAsValue(global_context,
                loc));

    LLVMValueRef res = codegen_from_scope(func_id, args, arg_c);
    if (!res) res = codegen_inbuilt_functions(se, arg_c, args);

    if (!res) error(UNKNOWN_ID, "unrecognised ID %s", func_id);

    debug("invoked %s\n", func_id);
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
    LLVMValueRef unboxed_cond_res = unbox_val(builder, cond_res, llvm_int_t);
    LLVMValueRef cond_v = LLVMBuildICmp(builder, LLVMIntNE, unboxed_cond_res,
            llvm_zero_v, "neq_0");
    LLVMBuildCondBr(builder, cond_v, then_b, else_b);

    put_builder_at_end(builder, then_b);
    LLVMValueRef then_v = _codegen(then_ast);
    LLVMBasicBlockRef then_exit_b = builder_block;
    LLVMBuildBr(builder, done_b);

    put_builder_at_end(builder, else_b);
    LLVMValueRef else_v = _codegen(else_ast);
    LLVMBasicBlockRef else_exit_b = builder_block;
    LLVMBuildBr(builder, done_b);

    put_builder_at_end(builder, done_b);
    LLVMValueRef phi_p = LLVMBuildPhi(builder, boxed_t, "if_res");
    LLVMBasicBlockRef phi_bs[] = { then_exit_b, else_exit_b };
    LLVMValueRef phi_vs[] = { then_v, else_v };
    LLVMAddIncoming(phi_p, phi_vs, phi_bs, 2);

    return phi_p;
}

LLVMValueRef codegen_branch(sexpr *se) {

    assert(se);

    sexpr *l = se->contents.n.l;
    sexpr *r = se->contents.n.r;

    LLVMValueRef res;

    if (l->type == ID) {
        if (!strcmp(l->contents.s, "def")) {
            res = codegen_definition(r);
        } else if (!strcmp(l->contents.s, "dec")) {
            res = codegen_declaration(r);
        } else if (!strcmp(l->contents.s, "type")) {
            res = codegen_type_definition(r);
        } else if (!strcmp(l->contents.s, "if")) {
            res = codegen_conditional(r);
        } else {
            res = codegen_invocation(se);
        }

    } else {
        error(SYNTAX_ERROR, "don't know what to do with above AST");
    }

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

void set_module_flag_int(LLVMModuleRef module, char *flag, int n, int val) {
    n = n ? n : strlen(flag);

    LLVMTypeRef flag_val_t = LLVMInt32Type();
    LLVMValueRef flag_llvm_val = LLVMConstInt(flag_val_t, val, 0);
    LLVMMetadataRef flag_meta_val = LLVMValueAsMetadata(flag_llvm_val);

    int flag_behaviour = LLVMModuleFlagBehaviorWarning;

    LLVMAddModuleFlag(module, flag_behaviour, flag, n, flag_meta_val);
}

LLVMMetadataRef debug_make_file(LLVMDIBuilderRef di_builder, char *filename) {
    filename = filename ? filename : "<stdin>";
    char *cwd = getcwd(0, 0);
    size_t filename_len = strlen(filename);
    size_t cwd_len = strlen(cwd);

    /*
     * this is a hacky solution that will work when the compiler is compiling
     * files found in the cwd
     */

     return LLVMDIBuilderCreateFile(di_builder, filename, filename_len, cwd,
                                    cwd_len);
}

LLVMMetadataRef debug_make_compile_unit(LLVMDIBuilderRef di_builder,
        LLVMMetadataRef di_file) {

    LLVMDWARFSourceLanguage lang = LLVMDWARFSourceLanguageC;
    char *producer = "Sinter";
    size_t producer_len = strlen(producer);
    int optimised = 0;
    char *flags = ""; // TODO: include compiler flags
    size_t flags_len = strlen(flags);
    uint runtime_version = 0; // TODO: include runtime version
    char *split_name = "";
    size_t split_name_len = strlen(split_name);
    LLVMDWARFEmissionKind emission = LLVMDWARFEmissionFull;
    uint dwoid = 0;
    int inline_debug_info = 1;
    int profile_debug_info = 1;

    return LLVMDIBuilderCreateCompileUnit(di_builder, lang, di_file, producer,
            producer_len, optimised, flags, flags_len, runtime_version,
            split_name, split_name_len, emission, dwoid, inline_debug_info,
            profile_debug_info);
}

void llvm_codegen_prologue(char *filename) {
    sc = 0;
    scope_push_layer(&sc);

    llvm_int_t = LLVMInt32Type();
    llvm_str_t = LLVMPointerType(LLVMInt8Type(), 0);
    boxed_t = LLVMPointerType(LLVMInt8Type(), 0);

    char *target_triple = LLVMGetDefaultTargetTriple();
    debug("using target %s\n", target_triple);

    module = LLVMModuleCreateWithName("main_module");
    set_module_flag_int(module, "Dwarf Version", 0, 4);
    set_module_flag_int(module, "Debug Info Version", 0, 3);

    LLVMSetTarget(module, target_triple);
    builder = LLVMCreateBuilder();
    di_builder = LLVMCreateDIBuilder(module);

    boxed_t_meta = LLVMDIBuilderCreatePointerType(di_builder, 0,
            (sizeof (uintptr_t) * 8), 0, 0, "box", strlen("box"));

    global_context = LLVMGetGlobalContext();

    di_file = debug_make_file(di_builder, filename);
    di_cu = debug_make_compile_unit(di_builder, di_file);

    LLVMDisposeMessage(target_triple);

    /* add print_int */
    print_int_t = make_function_type(1, 0);
    print_int_p = LLVMAddFunction(module, "debug_int", print_int_t);
    scope_add_entry(sc, "debug_int", print_int_p, print_int_t);

    /* add print_str */
    LLVMTypeRef print_str_params[] = { llvm_str_t };
    print_str_t = LLVMFunctionType(LLVMInt32Type(), print_str_params, 1, 0);
    print_str_p = LLVMAddFunction(module, "debug_str", print_str_t);
    scope_add_entry(sc, "debug_str", print_str_p, print_str_t);

    /* add debug_func_call */
    debug_func_call_t = make_function_type(1, 0);
    debug_func_call_p =
        LLVMAddFunction(module, "debug_func_call", debug_func_call_t);

    /* add gc_alloc */
    LLVMTypeRef gc_alloc_params[] = { llvm_int_t };
    gc_alloc_t = LLVMFunctionType(boxed_t, gc_alloc_params, 1, 0);
    gc_alloc_p = LLVMAddFunction(module, "gc_alloc", gc_alloc_t);
    scope_add_entry(sc, "gc_alloc", gc_alloc_p, gc_alloc_t);
}

int write_bitcode(LLVMModuleRef mod, char *fn) {
    LLVMWriteBitcodeToFile(mod, fn);
    return 0;
}

int write_module(LLVMModuleRef mod, char *fn) {
    char *code = LLVMPrintModuleToString(mod);
    FILE *fp = fopen(fn, "w");
    fprintf(fp, "%s", code);
    LLVMDisposeMessage(code);
    return 0;
}

int interpret(LLVMModuleRef module) {

    /*
     * XXX
     * This doesn't work right now, due to the need to call external GC
     * functions
     */

    LLVMExecutionEngineRef engine;
    char *error_str = 0;

    LLVMLinkInMCJIT();
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    if (LLVMCreateExecutionEngineForModule(&engine, module, &error_str)) {
        error(GENERAL_ERROR, "failed to create execution engine");
    }

    if (error_str) {
        error(GENERAL_ERROR, "%s", error_str);
    }

    /*
     * This is a very ugly cast, fetching a function pointer from the JIT'd
     * code. C will never like this cast because LLVMGetFunctionAddress
     * returns a uint64_t (deliberately, so that a cast is necessary).
     */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbad-function-cast"
    main_func_t main_func =
        (main_func_t) LLVMGetFunctionAddress(engine, "main");
#pragma clang diagnostic pop

    int *res = (int *) main_func();
    return *res;
}

int llvm_codegen_epilogue(char *output_filename, format_t format) {
    // char *error_str = 0;

    // LLVMVerifyModule(module, LLVMAbortProcessAction, &error_str);
    // LLVMDisposeMessage(error_str);

    LLVMDIBuilderFinalise(di_builder);

    switch (format) {
        case BITCODE:
            return write_bitcode(module,
                    output_filename ? output_filename : "/dev/stdout");

        case LLVM_INTERMEDIATE:
            return write_module(module,
                    output_filename ? output_filename : "/dev/stdout");

        case INTERPRET:
            return interpret(module);

        default:
            error(GENERAL_ERROR, "somehow got here with wrong format");

    }
}

void llvm_codegen(sexpr *se) {
    _codegen(se);
}
