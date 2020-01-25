target triple = "x86_64-unknown-linux-gnu"

declare dso_local void @GC_init()
declare dso_local i32 @atoi(i8*)
declare dso_local i8* @gc_alloc(i64)
declare dso_local i32 @printf(i8*, ...)

@.str = private unnamed_addr constant [2 x i8] c"p\00", align 1
@.str.1 = private unnamed_addr constant [7 x i8] c"glue.c\00", align 1
@__PRETTY_FUNCTION__.gc_alloc = private unnamed_addr constant [23 x i8] c"void *gc_alloc(size_t)\00", align 1
@.str.2 = private unnamed_addr constant [13 x i8] c"usage: %s n\0A\00", align 1
@.str.3 = private unnamed_addr constant [4 x i8] c"%i\0A\00", align 1

declare fastcc i8* @gen(i8*, i8*)

declare fastcc i8* @length (i8*)

define fastcc i8* @"+"(i8*, i8*) {
  %a_uncast = bitcast i8* %0 to VINT*
  %a = load VINT, VINT* %a_uncast
  %b_uncast = bitcast i8* %1 to VINT*
  %b = load VINT, VINT* %b_uncast
  %sum = add VINT %a, %b
  %res = call fastcc i8* @gc_alloc(i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, i32 1) to i64))
  %sum_box_cast = bitcast i8* %res to VINT*
  store VINT %sum, VINT* %sum_box_cast
  ret i8* %res
}

define fastcc i8* @"-"(i8*, i8*) {
  %a_uncast = bitcast i8* %0 to VINT*
  %a = load VINT, VINT* %a_uncast
  %b_uncast = bitcast i8* %1 to VINT*
  %b = load VINT, VINT* %b_uncast
  %sum = sub VINT %a, %b
  %res = call fastcc i8* @gc_alloc(i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, i32 1) to i64))
  %sum_box_cast = bitcast i8* %res to VINT*
  store VINT %sum, VINT* %sum_box_cast
  ret i8* %res
}

define fastcc VINT @count_solns(i32) {
  %ext_p = inttoptr i32 %0 to i8*
  %ext = ptrtoint i8* %ext_p to VINT
  %boxed = call fastcc i8* @gc_alloc(i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, i32 1) to i64))
  %box_uncast = bitcast i8* %boxed to VINT*
  store VINT %ext, VINT* %box_uncast
  %solns = call fastcc i8* @gen(i8* %boxed, i8* %boxed)
  %n_solns = call fastcc i8* @length(i8* %solns)
  %solns_uncast = bitcast i8* %n_solns to VINT*
  %res = load VINT, VINT* %solns_uncast
  ret VINT %res
}

define dso_local i32 @main(i32, i8**) optnone noinline {
  call void @GC_init()
  %cond = icmp ne i32 %0, 2
  br i1 %cond, label %aaa, label %bbb

aaa:
  %a12 = getelementptr inbounds i8*, i8** %1, i64 0
  %a13 = load i8*, i8** %a12, align 8
  %a14 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([13 x i8], [13 x i8]* @.str.2, i32 0, i32 0), i8* %a13)
  br label %ccc

bbb:
  %a17 = getelementptr inbounds i8*, i8** %1, i64 1
  %a18 = load i8*, i8** %a17, align 8
  %a19 = call i32 @atoi(i8* %a18)
  %a21 = call fastcc VINT @count_solns(i32 %a19)
  %a22_p = inttoptr VINT %a21 to i8*
  %a22 = ptrtoint i8* %a22_p to i32
  %a23 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.3, i32 0, i32 0), i32 %a22)
  br label %ccc

ccc:
  ret i32 0
}
