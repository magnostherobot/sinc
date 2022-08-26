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
#include "sinc.h"
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

static LLVMTypeRef llvm_str_t;
static LLVMTypeRef boxed_t;

static LLVMDIBuilderRef di_builder;
static LLVMMetadataRef di_file;
static LLVMMetadataRef di_cu;

static LLVMMetadataRef boxed_t_meta;
static LLVMMetadataRef last_func_m;

static optimisation_t *opts;

static scope sc;

void put_builder_at_end(LLVMBuilderRef b, LLVMBasicBlockRef block) {
    LLVMPositionBuilderAtEnd(b, block);
    if (b == builder) {
        builder_block = block;
    }
}

LLVMValueRef build_call(LLVMBuilderRef b, LLVMTypeRef t, LLVMValueRef p,
        LLVMValueRef *args, uint argc, char *id) {

    LLVMValueRef call = LLVMBuildCall2(b, t, p, args, argc, id);
    LLVMSetInstructionCallConv(call, LLVMFastCallConv);
    return call;
}

LLVMValueRef smart_box_val(LLVMValueRef f, LLVMBuilderRef b, LLVMValueRef val,
        LLVMTypeRef type) {

    LLVMValueRef size = LLVMSizeOf(type);
    LLVMTypeRef ptr_t = LLVMPointerType(type, 0);
    LLVMValueRef ptr_size = LLVMSizeOf(ptr_t);

    LLVMBasicBlockRef cond_b = LLVMAppendBasicBlock(f, "box_cond");
    LLVMBasicBlockRef smol_b = LLVMAppendBasicBlock(f, "box_smol");
    LLVMBasicBlockRef beeg_b = LLVMAppendBasicBlock(f, "box_beeg");
    LLVMBasicBlockRef done_b = LLVMAppendBasicBlock(f, "box_done");

    LLVMBuildBr(b, cond_b);

    put_builder_at_end(b, cond_b);
    LLVMValueRef cond = LLVMBuildICmp(b, LLVMIntULE, size, ptr_size, "smol?");
    LLVMBuildCondBr(b, cond, smol_b, beeg_b);

    put_builder_at_end(b, smol_b);
    LLVMValueRef cast = LLVMBuildCast(b, LLVMIntToPtr, val, boxed_t, "cast");
    LLVMBuildBr(b, done_b);

    put_builder_at_end(b, beeg_b);
    LLVMValueRef box = build_call(b, gc_alloc_t, gc_alloc_p, &size, 1, "boxed");
    LLVMValueRef box_cast = LLVMBuildPointerCast(b, box, ptr_t, "box_uncast");
    LLVMBuildStore(b, val, box_cast);
    LLVMBuildBr(b, done_b);

    put_builder_at_end(b, done_b);
    LLVMValueRef phi_vs[] = { cast, box };
    LLVMBasicBlockRef phi_bs[] = { smol_b, beeg_b };
    LLVMValueRef res = LLVMBuildPhi(b, boxed_t, "box_res");
    LLVMAddIncoming(res, phi_vs, phi_bs, 2);

    return res;
}

LLVMValueRef box_val(LLVMValueRef f, LLVMBuilderRef b, LLVMValueRef val,
        LLVMTypeRef type) {

    LLVMValueRef size = LLVMSizeOf(type);
    LLVMTypeRef ptr_t = LLVMPointerType(type, 0);

    switch (opts->boxing_rule) {

        case ALWAYS_BOX: {
            LLVMValueRef box = build_call(b, gc_alloc_t, gc_alloc_p, &size, 1,
                    "boxed");
            LLVMValueRef cast = LLVMBuildPointerCast(b, box, ptr_t,
                    "box_uncast");
            LLVMBuildStore(b, val, cast);
            return box;

        } case NEVER_BOX:
            return LLVMBuildCast(b, LLVMIntToPtr, val, boxed_t, "cast");

        case SMART_BOX:
            return smart_box_val(f, b, val, type);

        default:
            error(GENERAL_ERROR, "generating unboxing code");

    }
}

LLVMValueRef build_gc_alloc(LLVMBuilderRef b, LLVMTypeRef t, char *id) {
    LLVMValueRef size = LLVMSizeOf(t);
    return LLVMBuildCall2(b, gc_alloc_t, gc_alloc_p, &size, 1, id);
}

LLVMValueRef smart_unbox_val(LLVMValueRef f, LLVMBuilderRef b, LLVMValueRef box,
        LLVMTypeRef type) {

    LLVMValueRef size = LLVMSizeOf(type);
    LLVMTypeRef ptr_type = LLVMPointerType(type, 0);
    LLVMValueRef ptr_size = LLVMSizeOf(ptr_type);

    LLVMBasicBlockRef cond_b = LLVMAppendBasicBlock(f, "box_cond");
    LLVMBasicBlockRef smol_b = LLVMAppendBasicBlock(f, "box_smol");
    LLVMBasicBlockRef beeg_b = LLVMAppendBasicBlock(f, "box_beeg");
    LLVMBasicBlockRef done_b = LLVMAppendBasicBlock(f, "box_done");

    LLVMBuildBr(b, cond_b);

    put_builder_at_end(b, cond_b);
    LLVMValueRef cond = LLVMBuildICmp(b, LLVMIntULE, size, ptr_size, "smol?");
    LLVMBuildCondBr(b, cond, smol_b, beeg_b);

    put_builder_at_end(b, smol_b);
    LLVMValueRef uncast = LLVMBuildCast(b, LLVMPtrToInt, box, type, "uncast");
    LLVMBuildBr(b, done_b);

    put_builder_at_end(b, beeg_b);
    LLVMValueRef box_uncast = LLVMBuildPointerCast(b, box, ptr_type,
            "box_uncast");
    LLVMValueRef unbox = LLVMBuildLoad2(b, type, box_uncast, "unboxed");
    LLVMBuildBr(b, done_b);

    put_builder_at_end(b, done_b);
    LLVMValueRef phi_vs[] = { uncast, unbox };
    LLVMBasicBlockRef phi_bs[] = { smol_b, beeg_b };
    LLVMValueRef res = LLVMBuildPhi(b, type, "box_res");
    LLVMAddIncoming(res, phi_vs, phi_bs, 2);

    return res;
}

LLVMValueRef unbox_val(LLVMValueRef f, LLVMBuilderRef b, LLVMValueRef box,
        LLVMTypeRef type) {

    switch (opts->boxing_rule) {

        case ALWAYS_BOX: {
            LLVMTypeRef ptr_t = LLVMPointerType(type, 0);
            LLVMValueRef uncast = LLVMBuildPointerCast(b, box, ptr_t, "uncast");
            return LLVMBuildLoad2(b, type, uncast, "unboxed");

        } case NEVER_BOX:
            return LLVMBuildCast(b, LLVMPtrToInt, box, type, "uncast");

        case SMART_BOX:
            return smart_unbox_val(f, b, box, type);

    }
}

LLVMValueRef codegen_print_int(LLVMValueRef ref) {
    LLVMValueRef args[] = { ref };
    LLVMValueRef ret =
        build_call(builder, print_int_t, print_int_p, args, 1, "");
    return ret;
}

LLVMValueRef codegen_default_int(LLVMValueRef f, LLVMBuilderRef b, uint i) {
    if (!opts->default_bit_width) error(GENERAL_ERROR, "no bitwidth specified");

    LLVMTypeRef int_t = LLVMIntType(opts->default_bit_width);
    LLVMValueRef val = LLVMConstInt(int_t, i, 0);
    LLVMValueRef box = box_val(f, b, val, int_t);
    return box;
}

LLVMValueRef codegen_int(LLVMValueRef f, LLVMBuilderRef b, sexpr *se) {

    uint bit_width = se->contents.n.l->contents.i;

    /*
     * could use strtoull to support huge constants
     */
    unsigned long long value = se->contents.n.r->contents.i;

    LLVMTypeRef int_t = LLVMIntType(bit_width);
    LLVMValueRef val = LLVMConstInt(int_t, value, 0);
    return box_val(f, b, val, int_t);
}

LLVMValueRef codegen_string(sexpr *se) {
    assert(se->type == STRING);

    return LLVMBuildGlobalStringPtr(builder, se->contents.s, "string");
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

LLVMTypeRef make_void_function_type(unsigned param_c, LLVMBool vararg) {

    /* FIXME: memory leak */
    LLVMTypeRef *boxed_params = malloc((sizeof (LLVMTypeRef)) * param_c);
    for (uint i = 0; i < param_c; i++) {
        boxed_params[i] = boxed_t;
    }

    return LLVMFunctionType(LLVMVoidType(), boxed_params, param_c, vararg);
}

LLVMTypeRef make_function_type(unsigned param_c, LLVMBool vararg) {

    /* FIXME: memory leak */
    LLVMTypeRef *boxed_params = malloc((sizeof (LLVMTypeRef)) * param_c);
    for (uint i = 0; i < param_c; i++) {
        boxed_params[i] = boxed_t;
    }

    return LLVMFunctionType(boxed_t, boxed_params, param_c, vararg);
}

LLVMValueRef add_function(LLVMTypeRef func_t, char *func_id) {
    LLVMValueRef func_p = LLVMAddFunction(module, func_id, func_t);
    LLVMSetFunctionCallConv(func_p, LLVMFastCallConv);
    return func_p;
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

LLVMValueRef _codegen(sexpr *sexpr, int tail_position);

uint get_args_from_list(sexpr *se, LLVMValueRef *args) {
    debug("getting args from list @%p\n", (void *) se);
    if (!se) return 0;
    assert(se->type == BRANCH);

    *args = _codegen(se->contents.n.l, 0);

    return 1 + get_args_from_list(se->contents.n.r, args + 1);
}

uint get_args_from_list_except_last(sexpr *se, LLVMValueRef *args) {
    if (!se) return 0;
    assert(se->type == BRANCH);
    if (!se->contents.n.r) return 1;

    *args = _codegen(se->contents.n.l, 0);

    return 1 + get_args_from_list_except_last(se->contents.n.r, args + 1);
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

int make_filler_name(char *buf, int n, char *struct_id) {
    return snprintf(buf, n, ".%s.filler", struct_id);
}

int make_trmc_inner_name(char *buf, int n, char *outer_id, char *mod) {
    return snprintf(buf, n, ".%s.inner.%s", outer_id, mod);
}

void add_enum_attr(LLVMValueRef f, uint index, char *id, size_t n, int val) {
    // FIXME adding enums causes the generator to output unfinished LLVM IR.
    return;

    if (!opts->llvm_attributes) return;
    if (!n) n = strlen(id);

    unsigned kind = LLVMGetEnumAttributeKindForName(id, n);
    if (!kind) error(GENERAL_ERROR, "no enum attribute %s", id);

    LLVMAttributeRef attr = LLVMCreateEnumAttribute(global_context, kind, val);
    LLVMAddAttributeAtIndex(f, index, attr);
}

void add_func_enum_attr(LLVMValueRef f, char *id, size_t n, int val) {
    add_enum_attr(f, LLVMAttributeFunctionIndex, id, n, val);
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
     * Create a "filler" function: given a pointer as its last argument, fill in
     * the space it points to with properties given as the other arguments.
     */
    /* FIXME: magic size */
    char filler_id[50];
    make_filler_name(filler_id, 50, struct_id);

    uint filler_param_c = prop_c + 1;
    LLVMTypeRef filler_t = make_void_function_type(filler_param_c, 0);
    LLVMValueRef filler_p = add_function(filler_t, filler_id);
    LLVMBasicBlockRef filler_b = LLVMAppendBasicBlock(filler_p, "entry");
    put_builder_at_end(util_b, filler_b);

    LLVMValueRef space_cast = LLVMGetParam(filler_p, filler_param_c - 1);
    LLVMValueRef space = LLVMBuildPointerCast(util_b, space_cast,
            struct_ptr_t, "space_uncast");
    for (uint i = 0; i < prop_c; i++) {
        LLVMValueRef prop_pos =
            LLVMBuildStructGEP2(util_b, struct_t, space, i, "pos");
        LLVMValueRef param = LLVMGetParam(filler_p, i);
        LLVMBuildStore(util_b, param, prop_pos);
    }
    LLVMBuildRetVoid(util_b);

    scope_entry *struct_e = malloc(sizeof(*struct_e));
    struct_e->type = struct_t;
    struct_e->id = struct_id;
    struct_e->value = filler_p;

    /*
     * Create a wrapper around the filler function for use as a general-purpose
     * constructor.
     */
    char *ctor_id = struct_id;
    uint ctor_param_c = prop_c;
    LLVMTypeRef ctor_t = make_function_type(ctor_param_c, 0);
    LLVMValueRef ctor_p = add_function(ctor_t, ctor_id);
    LLVMBasicBlockRef ctor_b = LLVMAppendBasicBlock(ctor_p, "entry");
    put_builder_at_end(util_b, ctor_b);

    LLVMValueRef src = build_gc_alloc(util_b, struct_t, struct_id);
    assert(src);

    LLVMValueRef *args = malloc(filler_param_c * sizeof(*args));
    LLVMGetParams(ctor_p, args);
    args[filler_param_c - 1] = src;
    build_call(util_b, filler_t, filler_p, args, filler_param_c, "");
    free(args);

    LLVMBuildRet(util_b, src);

    scope_entry * ctor_e = scope_add_entry(sc, struct_id, ctor_p, ctor_t);
    ctor_e->has_filler = 1;
    /* FIXME: memory leak */
    ctor_e->filler.id = strdup(filler_id);
    ctor_e->filler.value = filler_p;
    ctor_e->filler.type = filler_t;
    ctor_e->filler.constructs = struct_t;

    add_func_enum_attr(filler_p, "alwaysinline", 0, 1);
    add_func_enum_attr(filler_p, "norecurse", 0, 1);
    add_func_enum_attr(filler_p, "nounwind", 0, 1);
    add_func_enum_attr(filler_p, "writeonly", 0, 1);
    add_func_enum_attr(filler_p, "argmemonly", 0, 1);
    add_enum_attr(filler_p, filler_param_c, "nonnull", 0, 1);

    add_func_enum_attr(ctor_p, "alwaysinline", 0, 1);
    add_func_enum_attr(ctor_p, "norecurse", 0, 1);
    add_func_enum_attr(ctor_p, "nounwind", 0, 1);

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
            add_function(prop_func_t, prop_func_id);
        LLVMBasicBlockRef prop_func_b =
            LLVMAppendBasicBlock(prop_func_p, "entry");
        put_builder_at_end(util_b, prop_func_b);
        LLVMValueRef param = LLVMGetParam(prop_func_p, 0);
        LLVMValueRef uncast =
            LLVMBuildPointerCast(util_b, param, struct_ptr_t, "uncast");
        LLVMValueRef ptr =
            LLVMBuildStructGEP2(util_b, struct_t, uncast, i, "part");
        LLVMValueRef val = LLVMBuildLoad2(util_b, boxed_t, ptr, "deref");
        LLVMBuildRet(util_b, val);

        debug("adding %s to scope\n", prop_func_id);
        scope_add_entry(sc, prop_func_id, prop_func_p, prop_func_t);

        add_func_enum_attr(prop_func_p, "alwaysinline", 0, 1);
        add_func_enum_attr(prop_func_p, "norecurse", 0, 1);
        add_func_enum_attr(prop_func_p, "nounwind", 0, 1);
        add_func_enum_attr(prop_func_p, "readonly", 0, 1);
        add_func_enum_attr(prop_func_p, "argmemonly", 0, 1);
        add_enum_attr(prop_func_p, 1, "nonnull", 0, 1);
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
    LLVMValueRef func_p = add_function(func_t, func_id);

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

int looks_inside_final_argument(scope_entry *func_entry) {
    /*
     * TODO FIXME
     * implementing this function will require keeping the AST of a function's
     * definition in the entry; just look through the function to see if any
     * access functions or operators are applied to the function's final
     * argument (this would include checking function calls recursively)
     */
    return 0 && func_entry;
}

sexpr *last_list_item(sexpr *list) {
    sexpr *last;
    sexpr *current = list;

    while (current) {
        last = current;
        current = current->contents.n.r;
    }

    return last;
}

typedef enum {
    NOT_TAIL_RECURSIVE = 0,
    TAIL_RECURSIVE,
    TAIL_RECURSIVE_MOD_CONS
} tail_recursive_t;

int is_tail_recursive_branch(char *id, sexpr *branch) {
    if (branch->type != BRANCH) return 0;
    if (branch->contents.n.l->type == INT) return 0;

    char *outermost_id = branch->contents.n.l->contents.s;
    scope_entry *outermost_e = scope_find(sc, outermost_id);
    if (!outermost_e->has_filler) return 0;

    sexpr *last_arg = last_list_item(branch)->contents.n.l;
    if (last_arg->type != BRANCH) return 0;

    char *last_arg_id = last_arg->contents.n.l->contents.s;
    return !strcmp(last_arg_id, id);
}

tail_recursive_t is_tail_recursive(char *id, sexpr *ast) {
    switch (ast->type) {

        case INT:
        case ID:
            /*
             * just an ID here means that the function is returning a constant
             * function pointer
             *
             * an int here means the function is returning a constant
             */
            debug("%s is not tail recursive because it returns a literal\n",
                    id);
            return NOT_TAIL_RECURSIVE;

        case BRANCH: {
            if (ast->contents.n.l->type == INT) {
                /*
                 * an int here means the function is returning a constant
                 */
                debug("%s is not tail recursive because it returns a literal\n",
                        id);
                return NOT_TAIL_RECURSIVE;
            }

            char *outermost_id = ast->contents.n.l->contents.s;

            if (!strcmp(id, outermost_id)) {
                debug("%s is tail recursive\n", id);
                return TAIL_RECURSIVE;
            }

            if (strcmp(outermost_id, "if")) {
                debug("%s is not tail recursive mod cons because it does not "
                        "immediately branch\n", id);
                return NOT_TAIL_RECURSIVE;
            }

            sexpr *cond_node = ast->contents.n.r;
            sexpr *then_node = cond_node->contents.n.r;
            if (is_tail_recursive_branch(id, then_node->contents.n.l)) {
                debug("%s is recursive mod cons on its then branch\n", id);
                return TAIL_RECURSIVE_MOD_CONS;
            }

            sexpr *else_node = then_node->contents.n.r;
            if (is_tail_recursive_branch(id, else_node->contents.n.l)) {
                debug ("%s is recursive mod cons on its else branch\n", id);
                return TAIL_RECURSIVE_MOD_CONS;
            }

            debug("%s is not tail recursive\n", id);
            return NOT_TAIL_RECURSIVE;
        }

        case NIL:
        default:
            error(GENERAL_ERROR, "checking if %s is tail-recursive mod cons",
                    id);

    }
}

sexpr *find_tail_recursive_call(char *id, sexpr *ast) {
    if (ast->type != BRANCH) {
        return 0;
    }

    sexpr *last_arg = last_list_item(ast);
    if (last_arg->type != BRANCH) {
        return 0;
    }

    if (strcmp(last_arg->contents.n.l->contents.n.l->contents.s, id)) {
        return 0;
    }

    return last_arg;
}

void debug_set_builder_location(LLVMBuilderRef b, sexpr *se) {

    LLVMMetadataRef loc =
        debug_make_location(global_context, &se->lloc, last_func_m);

    /*
     * LLVM 9 onwards has LLVMSetCurrentDebugLocation2, and this call is
     * deprecated
     */
    LLVMSetCurrentDebugLocation(b, LLVMMetadataAsValue(global_context,
                loc));
}

LLVMValueRef codegen_from_scope(char *func_id, LLVMValueRef *args, uint arg_c,
        int tail_position) {

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
        build_call(builder, func_t, func_p, args, arg_c, "");

    LLVMSetTailCall(res, tail_position);

    return res;
}

void codegen_trmc_inner(char *outer_id, char **outer_param_ids, LLVMValueRef
        inner_p, LLVMValueRef then_p, LLVMTypeRef then_t, LLVMValueRef else_p,
        LLVMTypeRef else_t, sexpr *body, sexpr* inner_ast) {

    sexpr *tail_recursive_call_ast = find_tail_recursive_call(outer_id,
            inner_ast);

    LLVMBasicBlockRef entry_b = LLVMAppendBasicBlock(inner_p, "entry");
    LLVMBasicBlockRef then_b = LLVMAppendBasicBlock(inner_p, "trmc_then");
    LLVMBasicBlockRef else_b = LLVMAppendBasicBlock(inner_p, "trmc_else");
    put_builder_at_end(builder, entry_b);

    sexpr *cond_node = body->contents.n.r;
    sexpr *cond_ast = cond_node->contents.n.l;

    sexpr *then_node = cond_node->contents.n.r;
    sexpr *then_ast = then_node->contents.n.l;

    sexpr *else_node = then_node->contents.n.r;
    sexpr *else_ast = else_node->contents.n.l;

    uint bit_width = opts->default_bit_width;
    sexpr *bit_width_node = else_node->contents.n.r;
    if (bit_width_node) bit_width = bit_width_node->contents.n.l->contents.i;
    if (!bit_width) error(GENERAL_ERROR, "no bitwidth specified");
    LLVMTypeRef int_t = LLVMIntType(bit_width);

    /* FIXME: magic size */
    LLVMValueRef next_args[50];
    uint outer_arg_c =
        get_args_from_list(tail_recursive_call_ast->contents.n.l->contents.n.r,
                next_args);
    uint inner_arg_c = outer_arg_c + 1;

    /*
     * We need to lift the conditional of the next recursive call up into
     * this one. To do so, we'll add a new layer to the scope that masks the
     * function's variables with the values of those variables the next call
     * down.
     */
    scope_push_layer(&sc);
    for (uint i = 0; i < (inner_arg_c - 1); ++i) {
        scope_add_entry(sc, outer_param_ids[i], next_args[i], boxed_t);
    }

    LLVMValueRef cond_contents = _codegen(cond_ast, 0);
    LLVMValueRef cond_res = unbox_val(inner_p, builder, cond_contents, int_t);
    LLVMValueRef zero_v = LLVMConstInt(int_t, 0, 0);
    LLVMValueRef cond_v = LLVMBuildICmp(builder, LLVMIntNE, cond_res, zero_v,
            "neq_0");
    LLVMBuildCondBr(builder, cond_v, then_b, else_b);

    scope_pop_layer(&sc);

    put_builder_at_end(builder, then_b);

    if (is_tail_recursive_branch(outer_id, then_ast)) {
        scope_entry *cons_e = scope_find(sc,
                inner_ast->contents.n.l->contents.s);
        LLVMValueRef next_space = build_gc_alloc(builder,
                cons_e->filler.constructs, "then_space");

        LLVMValueRef space = LLVMGetLastParam(inner_p);
        /* FIXME: magic size */
        LLVMValueRef filler_args[50];
        uint cons_arg_c =
            get_args_from_list_except_last(inner_ast->contents.n.r,
                    filler_args);
        uint filler_arg_c = cons_arg_c + 1;
        filler_args[cons_arg_c - 1] = next_space;
        filler_args[filler_arg_c - 1] = space;
        /* FIXME: magic size */
        char filler_id[50];
        make_filler_name(filler_id, 50, cons_e->id);
        scope_entry *filler_e = scope_find(sc, filler_id);
        build_call(builder, filler_e->type, filler_e->value, filler_args,
                filler_arg_c, "");

        next_args[inner_arg_c - 1] = next_space;
        LLVMValueRef tail_call = build_call(builder, then_t, then_p,
                next_args, inner_arg_c, "");
        LLVMSetTailCall(tail_call, 1);

        LLVMBuildRetVoid(builder);

    } else {

        /*
         * Codegenning a simple branch requires masking the variables again
         */
        scope_push_layer(&sc);
        for (uint i = 0; i < (inner_arg_c - 1); ++i) {
            scope_add_entry(sc, outer_param_ids[i], next_args[i], boxed_t);
        }

        LLVMValueRef res = _codegen(then_ast, 0);
        scope_pop_layer(&sc);

        LLVMValueRef space = LLVMGetLastParam(inner_p);
        /* FIXME: magic size */
        LLVMValueRef filler_args[50];
        uint cons_arg_c =
            get_args_from_list_except_last(inner_ast->contents.n.r,
                    filler_args);
        uint filler_arg_c = cons_arg_c + 1;
        filler_args[cons_arg_c - 1] = res;
        filler_args[filler_arg_c - 1] = space;
        char *cons_id = inner_ast->contents.n.l->contents.s;
        scope_entry *cons_e = scope_find(sc, cons_id);
        build_call(builder, cons_e->filler.type, cons_e->filler.value,
                filler_args, filler_arg_c, "");

        LLVMBuildRetVoid(builder);

    }

    put_builder_at_end(builder, else_b);

    if (is_tail_recursive_branch(outer_id, else_ast)) {
        scope_entry *cons_e = scope_find(sc,
                inner_ast->contents.n.l->contents.s);
        LLVMValueRef next_space = build_gc_alloc(builder,
                cons_e->filler.constructs, "else_space");

        LLVMValueRef space = LLVMGetLastParam(inner_p);
        /* FIXME: magic size */
        LLVMValueRef filler_args[50];
        uint cons_arg_c =
            get_args_from_list_except_last(inner_ast->contents.n.r,
                    filler_args);
        uint filler_arg_c = cons_arg_c + 1;
        filler_args[cons_arg_c - 1] = next_space;
        filler_args[filler_arg_c - 1] = space;
        /* FIXME: magic size */
        char filler_id[50];
        make_filler_name(filler_id, 50, cons_e->filler.id);
        build_call(builder, cons_e->filler.type, cons_e->filler.value,
                filler_args, filler_arg_c, "");

        next_args[inner_arg_c - 1] = next_space;
        LLVMValueRef tail_call = build_call(builder, else_t, else_p,
                next_args, inner_arg_c, "");
        LLVMSetTailCall(tail_call, 1);

        LLVMBuildRetVoid(builder);

    } else {

        /*
         * Codegenning a simple branch requires masking the variables again
         */
        scope_push_layer(&sc);
        for (uint i = 0; i < (inner_arg_c - 1); ++i) {
            scope_add_entry(sc, outer_param_ids[i], next_args[i], boxed_t);
        }

        LLVMValueRef res = _codegen(else_ast, 0);
        scope_pop_layer(&sc);

        LLVMValueRef space = LLVMGetLastParam(inner_p);
        /* FIXME: magic size */
        LLVMValueRef filler_args[50];
        uint cons_arg_c =
            get_args_from_list_except_last(inner_ast->contents.n.r,
                    filler_args);
        uint filler_arg_c = cons_arg_c + 1;
        filler_args[cons_arg_c - 1] = res;
        filler_args[filler_arg_c - 1] = space;
        char *cons_id = inner_ast->contents.n.l->contents.s;
        scope_entry *cons_e = scope_find(sc, cons_id);
        /* FIXME: magic size */
        char filler_id[50];
        make_filler_name(filler_id, 50, cons_e->filler.id);
        scope_entry *filler_e = scope_find(sc, filler_id);
        build_call(builder, filler_e->type, filler_e->value, filler_args,
                filler_arg_c, "");

        LLVMBuildRetVoid(builder);

   }

    add_func_enum_attr(inner_p, "nounwind", 0, 1);
    add_enum_attr(inner_p, inner_arg_c, "nonnull", 0, 1);
}

void codegen_trmc(scope_entry *outer_e, uint outer_param_c, sexpr *body,
        char **param_names) {

    /*
     * making a TRMC function TR requires creating an inner function that is
     * called by the outer function, and that calls the cons function with a
     * null final argument so that the final function called is the inner
     * function itself.
     *
     * The inner function is given a pointer to space for it to fill alongside
     * its usual arguments. It should fill this space, then (as its final
     * action) tail-call itself recursively with a new space.
     *
     * The outer function has to call the cons for the first inner function call
     * (of which there will always be at least one), but it needs to pass a
     * dummy value in place of the return value of the inner function since it
     * hasn't been called yet.
     *
     * This can be acheived by building two blocks for the outer function:
     * 1. Regular code generation, except when the recursive tail-call is
     *    detected it is replaced by a pointer to empty space.
     * 2. Call the previously-omitted tail-call, giving it its regular arguments
     *    plus a pointer to the space it should fill (rather than allocating
     *    space itself.
     *
     * 1 jumps straight into 2; having separate blocks means that 2 can be
     * generated as soon as the recursive tail-call is found.
     *
     * The difficulty is that 1 needs a pointer to space already allocated for
     * the right size for the result of 2. If the inner function contains a
     * conditional (which it almost certainly does for managing a base case),
     * then the size required could be variable. Pushing the conditional up to
     * the caller function means it can allocate the correct amount of space.
     *
     * As an example:
     *
     * [def map [f xs]
     *     [if [is_nil xs]
     *         [nil]
     *         [cons [f [head xs]]
     *             [map f [tail xs]]]]]
     *
     * This definition of map is TRMC, and should become the following
     * pseudocode:
     *
     * map(f, xs):
     *   if (is_nil(xs)):
     *     p = allocate_space_for(nil)
     *   else:
     *     p = allocate_space_for(cons)
     *   map_inner(f, xs, p)
     *   return p
     *
     * map_inner(f, xs, p):
     *   if (is_nil(xs)):
     *     *p = nil()
     *   else:
     *     next_xs = tail(xs)
     *     if (is_nil(next_xs)):
     *       q = allocate_space(nil)
     *     else:
     *       q = allocate_space(cons)
     *     *p = cons(f(head(xs)), q)
     *     map_inner(f, next_xs, q)
     *
     * This version duplicates the conditional expression, which might slow
     * things down and also duplicate side effects. How about:
     *
     * map(f, xs):
     *   if (is_nil(xs)):
     *     p = allocate_space_for(nil)
     *     map_then(f, xs, p)
     *   else:
     *     p = allocate_space_for(cons)
     *     map_else(f, xs, p)
     *   return p
     *
     * map_then(f, xs, p):
     *   *p = nil()
     *
     * map_else(f, xs, p):
     *   next_f = f
     *   next_xs = tail(xs)
     *   if (is_nil(next_xs)):
     *     q = allocate_space_for(nil)
     *     *p = cons(f(head(xs)), q)
     *     map_then(next_f, next_xs, q)
     *   else:
     *     q = allocate_space_for(cons)
     *     *p = cons(f(head(xs)), q)
     *     map_else(next_f, next_xs, q)
     *
     * [def concat [as bs]
     *   [if [is_nil as]
     *     bs
     *     [cons [head as] [concat [tail as] bs]]]]
     *
     * becomes
     *
     * concat(as, bs):
     *   if (is_nil(as)):
     *     p = bs
     *   else:
     *     p = allocate_space_for(cons)
     *     concat_else(as, bs, p)
     *   return p
     *
     * concat_else(as, bs, p):
     *   next_as = tail(as)
     *   next_bs = bs
     *   if (is_nil(next_as)):
     *     q = next_bs
     *     *p = cons(head(as), q)
     *   else:
     *     q = allocate_space_for(cons)
     *     *p = cons(head(as), q)
     *     concat_else(next_as, next_bs, q)
     *
     * This doesn't duplicate side effects, but it would reorder them.
     *
     * So in general the pattern is:
     *
     * F(args):
     *   if (cond(args)):
     *     A(a_args(args))
     *   else:
     *     B(b_args(args), F(f_args(args)))
     *
     * becomes:
     *
     * F(args):
     *   if (cond(args)):
     *     p = allocate_space_for(A)
     *     F_then(args, p)
     *   else:
     *     p = allocate_space_for(B)
     *     F_else(args, p)
     *   return p
     *
     * F_then(args, p):
     *   *p = A(a_args(args))
     *
     * F_else(args, p):
     *   next_args = f_args(args)
     *   if (cond(next_args)):
     *     q = allocate_space_for(A)
     *     *p = B(b_args(args), q)
     *     F_then(next_args, p)
     *   else:
     *     q = allocate_space_for(B)
     *     *p = B(b_args(args), q)
     *     F_else(next_args, p)
     *
     * The outer function can be inlined, since it doesn't do too much.
     */

    /*
     * Create the functions, by defining their types. The inner functions
     * shouldn't be visible to the rest of the program, and the outer function
     * is already defined, so there's no need to ad them to the scope.
     */
    LLVMValueRef outer_p = outer_e->value;
    LLVMTypeRef inner_t = make_void_function_type(outer_param_c + 1, 0);

    char *outer_id = outer_e->id;

    /* FIXME: magic size */
    char inner_id[50];

    LLVMValueRef then_p = 0;
    LLVMValueRef else_p = 0;

    /*
     * Build the outer function. It's already been defined in
     * codegen_definition, so some of its information is available in its scope
     * entry.
     *
     * The topmost part of the AST should be the conditional.
     */
    LLVMBasicBlockRef then_b = LLVMAppendBasicBlock(function, "trmc_then");
    LLVMBasicBlockRef else_b = LLVMAppendBasicBlock(function, "trmc_else");

    sexpr *cond_node = body->contents.n.r;
    sexpr *cond_ast = cond_node->contents.n.l;

    sexpr *then_node = cond_node->contents.n.r;
    sexpr *then_ast = then_node->contents.n.l;

    sexpr *else_node = then_node->contents.n.r;
    sexpr *else_ast = else_node->contents.n.l;

    uint bit_width = opts->default_bit_width;
    sexpr *bit_width_node = else_node->contents.n.r;
    if (bit_width_node) bit_width = bit_width_node->contents.n.l->contents.i;
    if (!bit_width) error(GENERAL_ERROR, "no bitwidth specified");
    LLVMTypeRef int_t = LLVMIntType(bit_width);

    LLVMValueRef zero_v = LLVMConstInt(int_t, 0, 0);
    LLVMValueRef cond_res = _codegen(cond_ast, 0);
    LLVMValueRef unboxed_cond_res = unbox_val(function, builder, cond_res,
            int_t);
    LLVMValueRef cond_v = LLVMBuildICmp(builder, LLVMIntNE, unboxed_cond_res,
            zero_v, "neq_0");
    LLVMBuildCondBr(builder, cond_v, then_b, else_b);

    uint inner_param_c = outer_param_c + 1;
    LLVMValueRef *params = malloc(inner_param_c * sizeof(*params));
    LLVMGetParams(outer_p, params);

    put_builder_at_end(builder, then_b);

    if (is_tail_recursive_branch(outer_id, then_ast)) {
        make_trmc_inner_name(inner_id, 50, outer_id, "then");
        then_p = add_function(inner_t, inner_id);

        scope_entry *then_cons = scope_find(sc,
                then_ast->contents.n.l->contents.s);
        LLVMValueRef then_space = build_gc_alloc(builder, then_cons->type,
                "then_space");

        params[inner_param_c - 1] = then_space;
        build_call(builder, inner_t, then_p, params, inner_param_c, "");
        LLVMBuildRet(builder, then_space);

    } else {
        LLVMValueRef res = _codegen(then_ast, 0);
        LLVMBuildRet(builder, res);

    }

    put_builder_at_end(builder, else_b);

    if (is_tail_recursive_branch(outer_id, else_ast)) {
        make_trmc_inner_name(inner_id, 50, outer_id, "else");
        else_p = add_function(inner_t, inner_id);

        scope_entry *else_cons = scope_find(sc,
                else_ast->contents.n.l->contents.s);
        LLVMValueRef else_space = build_gc_alloc(builder,
                else_cons->filler.constructs, "else_space");

        params[inner_param_c - 1] = else_space;
        build_call(builder, inner_t, else_p, params, inner_param_c, "");
        LLVMBuildRet(builder, else_space);

    } else {
        LLVMValueRef res = _codegen(else_ast, 0);
        LLVMBuildRet(builder, res);

    }

    function = then_p;
    if (then_p) codegen_trmc_inner(outer_id, param_names, then_p, then_p,
            inner_t, else_p, inner_t, body, then_ast);

    function = else_p;
    if (else_p) codegen_trmc_inner(outer_id, param_names, else_p, then_p,
            inner_t, else_p, inner_t, body, else_ast);

    add_func_enum_attr(outer_p, "nounwind", 0, 1);

    /*
     * It would be nice if we could consider these TRMC functions similarly to
     * filler functions, e.g. flatten would be TRMC with concat being its cons.
     *
     * An idea is to make an allocator function for all TRMC-optimised
     * functions, to complement the inner functions (since they're effectively
     * fillers). Both the outer and inner functions can use this allocator
     * function (maybe), but the readon for making these allocator functions is
     * so that external functions that are tail-recursive mod this function can
     * use it to allocate the correct space.
     */
}

scope_entry *find_constructor_base(sexpr *ast) {
    if (ast->type != BRANCH) return 0;
    if (ast->contents.n.l->type != ID) return 0;

    char *outermost_id = ast->contents.n.l->contents.s;
    scope_entry *outermost_e = scope_find(sc, outermost_id);
    if (!outermost_e) return 0;
    if (outermost_e->has_filler) return outermost_e;

    return 0;
}

void codegen_constructor(scope_entry *outer_e, scope_entry *base_e, uint
        outer_param_c, sexpr *body) {

    LLVMValueRef outer_p = outer_e->value;
    char *outer_id = outer_e->id;

    /* FIXME: magic size */
    char filler_id[50];
    make_filler_name(filler_id, 50, outer_id);

    uint filler_param_c = outer_param_c + 1;
    LLVMTypeRef filler_t = make_void_function_type(filler_param_c, 0);
    LLVMValueRef filler_p = add_function(filler_t, filler_id);

    LLVMValueRef space = build_gc_alloc(builder, base_e->filler.constructs,
            "space");
    /* FIXME: magic size */
    LLVMValueRef filler_params[50];
    LLVMGetParams(outer_p, filler_params);
    filler_params[filler_param_c - 1] = space;
    build_call(builder, filler_t, filler_p, filler_params, filler_param_c,
            "");

    LLVMBuildRet(builder, space);

    LLVMBasicBlockRef filler_entry_b = LLVMAppendBasicBlock(filler_p,
            "entry");
    put_builder_at_end(builder, filler_entry_b);
    function = filler_p;

    LLVMValueRef next_filler_params[50];
    uint next_filler_param_c = get_args_from_list(body->contents.n.r,
            next_filler_params) + 1;
    next_filler_params[next_filler_param_c - 1] = LLVMGetLastParam(filler_p);
    LLVMValueRef next_filler_p = base_e->filler.value;
    LLVMTypeRef next_filler_t = base_e->filler.type;
    build_call(builder, next_filler_t, next_filler_p, next_filler_params,
            next_filler_param_c, "");

    LLVMBuildRetVoid(builder);

    outer_e->has_filler = 1;
    /* FIXME: memory leak */
    outer_e->filler.id = strdup(filler_id);
    outer_e->filler.value = filler_p;
    outer_e->filler.type = filler_t;
    outer_e->filler.constructs = base_e->filler.constructs;

    add_func_enum_attr(filler_p, "nounwind", 0, 1);
    add_enum_attr(filler_p, filler_param_c, "nonnull", 0, 1);

    add_func_enum_attr(outer_p, "nounwind", 0, 1);
}

void codegen_basic_function(LLVMValueRef f, sexpr *body, int
        tail_position) {

    add_func_enum_attr(f, "nounwind", 0, 1);
    LLVMBuildRet(builder, _codegen(body, tail_position));
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

    LLVMBasicBlockRef func_b = LLVMAppendBasicBlock(func_p, "debug");

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

    function = func_p;

    scope_push_layer(&sc);
    for (uint i = 0; i < param_c; i++) {
        scope_add_entry(sc, ids[i], LLVMGetParam(func_p, i), boxed_t);
    }

    assert(!info->contents.n.r->contents.n.r);
    sexpr *body = info->contents.n.r->contents.n.l;

    scope_entry *constructor_base = find_constructor_base(body);

    if (opts->tail_recursive_mod_cons) {

        if (is_tail_recursive(func_id, body) == TAIL_RECURSIVE_MOD_CONS) {
            codegen_trmc(entry, param_c, body, ids);

        } else if (constructor_base) {
            codegen_constructor(entry, constructor_base, param_c, body);

        } else {
            codegen_basic_function(func_p, body, 1);
        }

    } else {
        codegen_basic_function(func_p, body, 1);
    }

    scope_pop_layer(&sc);

    debug("built function %s\n", func_id);

    return func_p;
}

LLVMValueRef codegen_invocation(sexpr *se, int tail_position) {
    assert(se);

    sexpr *l = se->contents.n.l;
    sexpr *r = se->contents.n.r;

    assert(l->type == ID);

    char *func_id = l->contents.s;
    debug("invoking %s\n", func_id);

    /* FIXME: magic values */
    LLVMValueRef args[50];
    uint arg_c = get_args_from_list(r, args);

    debug_set_builder_location(builder, l);

    LLVMValueRef res = codegen_from_scope(func_id, args, arg_c, tail_position);

    if (!res) error(UNKNOWN_ID, "unrecognised ID %s", func_id);

    debug("invoked %s\n", func_id);
    return res;
}

LLVMValueRef codegen_conditional(sexpr *se, int tail_position) {
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

    uint bit_width = opts->default_bit_width;
    se = se->contents.n.r;
    if (se) bit_width = se->contents.n.l->contents.i;
    if (!bit_width) error(GENERAL_ERROR, "no bitwidth specified");

    LLVMBasicBlockRef then_b = LLVMAppendBasicBlock(function, "then");
    LLVMBasicBlockRef else_b = LLVMAppendBasicBlock(function, "else");
    LLVMBasicBlockRef done_b = LLVMAppendBasicBlock(function, "done");

    LLVMValueRef cond_res = _codegen(cond_ast, 0);
    LLVMTypeRef int_t = LLVMIntType(bit_width);
    LLVMValueRef unboxed_cond_res = unbox_val(function, builder, cond_res,
            int_t);
    LLVMValueRef zero = LLVMConstInt(int_t, 0, 0);
    LLVMValueRef cond_v = LLVMBuildICmp(builder, LLVMIntNE, unboxed_cond_res,
            zero, "neq_0");
    LLVMBuildCondBr(builder, cond_v, then_b, else_b);

    put_builder_at_end(builder, then_b);
    LLVMValueRef then_v = _codegen(then_ast, tail_position);
    LLVMBasicBlockRef then_exit_b = builder_block;
    LLVMBuildBr(builder, done_b);

    put_builder_at_end(builder, else_b);
    LLVMValueRef else_v = _codegen(else_ast, tail_position);
    LLVMBasicBlockRef else_exit_b = builder_block;
    LLVMBuildBr(builder, done_b);

    put_builder_at_end(builder, done_b);
    LLVMValueRef phi_p = LLVMBuildPhi(builder, boxed_t, "if_res");
    LLVMBasicBlockRef phi_bs[] = { then_exit_b, else_exit_b };
    LLVMValueRef phi_vs[] = { then_v, else_v };
    LLVMAddIncoming(phi_p, phi_vs, phi_bs, 2);

    return phi_p;
}

LLVMValueRef constant_int(sexpr *se) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *int_w = se->contents.n.l;
    assert(int_w);
    assert(int_w->type == INT);

    sexpr *int_v = se->contents.n.r;
    assert(int_v);
    assert(int_v->type == INT);

    // LLVMTypeRef int_t = LLVMIntType(int_w->contents.i);
    LLVMTypeRef int_t = LLVMIntType(32);
    return LLVMConstInt(int_t, int_v->contents.i, 0);
}

LLVMValueRef codegen_case(sexpr *se, int tail_position) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *match_against = se->contents.n.l;
    assert(match_against);

    sexpr *cases_parent = se->contents.n.r;
    assert(cases_parent);
    assert(cases_parent->type == BRANCH);

    sexpr *cases_start = cases_parent->contents.n.l;
    assert(cases_start);
    assert(cases_start->type == BRANCH);

    sexpr *default_case_parent = cases_parent->contents.n.r;
    assert(default_case_parent);
    assert(default_case_parent->type == BRANCH);
    assert(default_case_parent->contents.n.r == NULL);

    sexpr *default_case_node = default_case_parent->contents.n.l;
    assert(default_case_node);

    LLVMValueRef matchee = _codegen(match_against, tail_position);

    LLVMBasicBlockRef before_b = builder_block;
    LLVMBasicBlockRef exit_b = LLVMAppendBasicBlock(function, "case_exit");

    // FIXME magic number
    LLVMValueRef matches[100];
    LLVMBasicBlockRef case_blocks[100];
    LLVMBasicBlockRef case_exit_blocks[100];
    LLVMValueRef results[100];

    unsigned case_count = 0;
    for (sexpr *case_parent = cases_start; case_parent;
            case_parent = case_parent->contents.n.r) {

        sexpr *case_node = case_parent->contents.n.l;
        assert(case_node);
        assert(case_node->type == BRANCH);

        sexpr *match_node = case_node->contents.n.l;
        assert(match_node);

        sexpr *then_node = case_node->contents.n.r;
        assert(then_node);

        put_builder_at_end(builder, before_b);
        LLVMValueRef match = constant_int(match_node);

        LLVMBasicBlockRef case_b =
            LLVMAppendBasicBlock(function, "case_branch");
        LLVMBasicBlockRef case_exit_b =
            LLVMAppendBasicBlock(function, "case_branch_exit");

        put_builder_at_end(builder, case_b);
        LLVMValueRef then_result = _codegen(then_node, tail_position);
        LLVMBuildBr(builder, case_exit_b);

        put_builder_at_end(builder, case_exit_b);
        LLVMBuildBr(builder, exit_b);

        matches[case_count] = match;
        case_blocks[case_count] = case_b;
        case_exit_blocks[case_count] = case_exit_b;
        results[case_count] = then_result;

        case_count++;
    }

    LLVMBasicBlockRef default_b =
        LLVMAppendBasicBlock(function, "case_default");
    LLVMBasicBlockRef default_exit_b =
        LLVMAppendBasicBlock(function, "case_default_exit");

    put_builder_at_end(builder, default_b);
    LLVMValueRef def_result = _codegen(default_case_node, tail_position);
    LLVMBuildBr(builder, default_exit_b);

    put_builder_at_end(builder, default_exit_b);
    LLVMBuildBr(builder, exit_b);

    /* FIXME
     *
     * Using 32 as a magic number here means that values wider than 32 bits are
     * going to be checked incorrectly. This number should instead be
     * user-provided (perhaps in a similar way to if statements)
     */
    LLVMTypeRef case_width = LLVMIntType(32);

    put_builder_at_end(builder, before_b);
    LLVMValueRef matchee_unboxed =
        unbox_val(function, builder, matchee, case_width);
    LLVMValueRef switch_v =
        LLVMBuildSwitch(builder, matchee_unboxed, default_b, case_count);
    for (unsigned i = 0; i < case_count; i++) {
        LLVMAddCase(switch_v, matches[i], case_blocks[i]);
    }

    put_builder_at_end(builder, exit_b);
    // FIXME shouldn't be using boxed_t here
    LLVMValueRef res = LLVMBuildPhi(builder, boxed_t, "case_result");
    case_blocks[case_count] = default_b;
    case_exit_blocks[case_count] = default_exit_b;
    results[case_count] = def_result;
    LLVMAddIncoming(res, results, case_exit_blocks, case_count + 1);

    return res;
}

LLVMValueRef codegen_let(sexpr *se, int tail_position) {
    assert(se);
    assert(se->type == BRANCH);

    sexpr *binding_pair = se->contents.n.l;
    assert(binding_pair);
    assert(binding_pair->type == BRANCH);

    sexpr *new_name_node = binding_pair->contents.n.l;
    assert(new_name_node);
    assert(new_name_node->type == ID);

    sexpr *old_expr_node = binding_pair->contents.n.r;
    assert(old_expr_node);

    sexpr *in_expr_parent = se->contents.n.r;
    assert(in_expr_parent);
    assert(in_expr_parent->type == BRANCH);

    sexpr *in_expr = in_expr_parent->contents.n.l;
    assert(in_expr);

    LLVMValueRef old_expr = _codegen(old_expr_node, tail_position);

    scope_push_layer(&sc);
    // FIXME should not rely on boxed_t
    scope_add_entry(sc, new_name_node->contents.s, old_expr, boxed_t);

    LLVMValueRef res = _codegen(in_expr, tail_position);

    scope_pop_layer(&sc);

    return res;
}

LLVMValueRef codegen_branch(sexpr *se, int tail_position) {

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
            res = codegen_conditional(r, tail_position);
        } else if (!strcmp(l->contents.s, "case")) {
            res = codegen_case(r, tail_position);
        } else if (!strcmp(l->contents.s, "let")) {
            res = codegen_let(r, tail_position);
        } else {
            res = codegen_invocation(se, tail_position);
        }

    } else if (l->type == INT) {
        res = codegen_int(function, builder, se);

    } else {
        error(SYNTAX_ERROR, "don't know what to do with AST");
    }

    return res;
}

LLVMValueRef _codegen(sexpr *sexpr, int tail_position) {
    switch (sexpr->type) {
        case INT:
            return codegen_default_int(function, builder, sexpr->contents.i);
        case ID:
            return codegen_id(sexpr->contents.s);
        case STRING:
            return codegen_string(sexpr);
        case BRANCH:
            return codegen_branch(sexpr, tail_position);
        case NIL:
        default:
            return 0;
    }
}

void set_module_flag_int(LLVMModuleRef module, char *flag, size_t n, int val) {
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

    return LLVMDIBuilderCreateCompileUnit(
            di_builder,
            lang,
            di_file,
            producer, producer_len,
            optimised,
            flags, flags_len,
            runtime_version,
            split_name, split_name_len,
            emission,
            dwoid,
            inline_debug_info,
            profile_debug_info,
            // TODO
            "", 0,
            "", 0
            );
}

void llvm_codegen_prologue(char *filename, optimisation_t *llvm_info) {
    sc = 0;
    scope_push_layer(&sc);

    llvm_str_t = LLVMPointerType(LLVMInt8Type(), 0);
    boxed_t = LLVMPointerType(LLVMInt8Type(), 0);

    char *target_triple = LLVMGetDefaultTargetTriple();
    debug("using target %s\n", target_triple);

    module = LLVMModuleCreateWithName("main_module");
    set_module_flag_int(module, "Dwarf Version", 0, 4);
    set_module_flag_int(module, "Debug Info Version", 0, 3);

    opts = llvm_info;

    LLVMSetTarget(module, target_triple);
    builder = LLVMCreateBuilder();
    di_builder = LLVMCreateDIBuilder(module);

    boxed_t_meta = LLVMDIBuilderCreatePointerType(di_builder, 0,
            (sizeof (uintptr_t) * 8), 0, 0, "box", strlen("box"));

    global_context = LLVMGetGlobalContext();

    di_file = debug_make_file(di_builder, filename);
    di_cu = debug_make_compile_unit(di_builder, di_file);

    LLVMDisposeMessage(target_triple);

    /*
     * Get the size of an arbitrary value to find out what type LLVM uses to
     * report sizes.
     */
    LLVMTypeRef llvm_size_t = LLVMTypeOf(LLVMSizeOf(LLVMIntType(1)));

    /* add gc_alloc */
    LLVMTypeRef gc_alloc_params[] = { llvm_size_t };
    gc_alloc_t = LLVMFunctionType(boxed_t, gc_alloc_params, 1, 0);
    gc_alloc_p = add_function(gc_alloc_t, "gc_alloc");

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
    /* char *error_str = 0; */
    /* LLVMDumpModule(module); */
    /* LLVMVerifyModule(module, LLVMAbortProcessAction, &error_str); */
    /* LLVMDisposeMessage(error_str); */

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
    _codegen(se, 0);
}
