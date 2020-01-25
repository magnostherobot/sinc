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
  %cond = icmp ule i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, VINT 1) to i64), ptrtoint (i1** getelementptr (i1*, i1** null, VINT 1) to i64)
  br i1 %cond, label %box_smol, label %box_beeg

box_smol:
  %a_smol = ptrtoint i8* %0 to VINT
  %b_smol = ptrtoint i8* %1 to VINT
  br label %box_done

box_beeg:
  %a_uncast = bitcast i8* %0 to VINT*
  %a_beeg = load VINT, VINT* %a_uncast
  %b_uncast = bitcast i8* %1 to VINT*
  %b_beeg = load VINT, VINT* %b_uncast
  br label %box_done

box_done:
  %a = phi VINT [ %a_smol, %box_smol ], [ %a_beeg, %box_beeg ]
  %b = phi VINT [ %b_smol, %box_smol ], [ %b_beeg, %box_beeg ]
  %sum = add VINT %a, %b
  br i1 %cond, label %box_smol1, label %box_beeg1

box_smol1:
  %sum_smol = inttoptr VINT %sum to i8*
  br label %box_done1

box_beeg1:
  %sum_boxed = call fastcc i8* @gc_alloc(i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, i32 1) to i64))
  %sum_box_cast = bitcast i8* %sum_boxed to VINT*
  store VINT %sum, VINT* %sum_box_cast
  br label %box_done1

box_done1:
  %res = phi i8* [ %sum_smol, %box_smol1 ], [ %sum_boxed, %box_beeg1 ]
  ret i8* %res
}

define fastcc i8* @-(i8*, i8*) {
  %cond = icmp ule i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, VINT 1) to i64), ptrtoint (i1** getelementptr (i1*, i1** null, VINT 1) to i64)
  br i1 %cond, label %box_smol, label %box_beeg

box_smol:
  %a_smol = ptrtoint i8* %0 to VINT
  %b_smol = ptrtoint i8* %1 to VINT
  br label %box_done

box_beeg:
  %a_uncast = bitcast i8* %0 to VINT*
  %a_beeg = load VINT, VINT* %a_uncast
  %b_uncast = bitcast i8* %1 to VINT*
  %b_beeg = load VINT, VINT* %b_uncast
  br label %box_done

box_done:
  %a = phi VINT [ %a_smol, %box_smol ], [ %a_beeg, %box_beeg ]
  %b = phi VINT [ %b_smol, %box_smol ], [ %b_beeg, %box_beeg ]
  %sum = sub VINT %a, %b
  br i1 %cond, label %box_smol1, label %box_beeg1

box_smol1:
  %sum_smol = inttoptr VINT %sum to i8*
  br label %box_done1

box_beeg1:
  %sum_boxed = call fastcc i8* @gc_alloc(i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, i32 1) to i64))
  %sum_box_cast = bitcast i8* %sum_boxed to VINT*
  store VINT %sum, VINT* %sum_box_cast
  br label %box_done1

box_done1:
  %res = phi i8* [ %sum_smol, %box_smol1 ], [ %sum_boxed, %box_beeg1 ]
  ret i8* %res
}

define fastcc VINT @count_solns(i32) {
  %ext_p = inttoptr i32 %0 to i8*
  %ext = ptrtoint i8* %ext_p to VINT
  ; %ext = zext i32 %0 to VINT
  br label %box_cond

box_cond:                                         ; preds = %entry
  %cond = icmp ule i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, VINT 1) to i64), ptrtoint (i1** getelementptr (i1*, i1** null, VINT 1) to i64)
  br i1 %cond, label %box_smol, label %box_beeg

box_smol:                                         ; preds = %box_cond
  %cast = inttoptr VINT %ext to i8*
  br label %box_done

box_beeg:                                         ; preds = %box_cond
  %boxed = call fastcc i8* @gc_alloc(i64 ptrtoint (VINT* getelementptr (VINT, VINT* null, i32 1) to i64))
  %box_uncast = bitcast i8* %boxed to VINT*
  store VINT %ext, VINT* %box_uncast
  br label %box_done

box_done:                                         ; preds = %box_beeg, %box_smol
  %box_res = phi i8* [ %cast, %box_smol ], [ %boxed, %box_beeg ]

  %solns = call fastcc i8* @gen(i8* %box_res, i8* %box_res)
  %n_solns = call fastcc i8* @length(i8* %solns)

  br i1 %cond, label %box_smol1, label %box_beeg1

box_smol1:
  %solns_smol = ptrtoint i8* %n_solns to VINT
  br label %box_done1

box_beeg1:
  %solns_uncast = bitcast i8* %n_solns to VINT*
  %solns_beeg = load VINT, VINT* %solns_uncast
  br label %box_done1

box_done1:
  %res = phi VINT [ %solns_smol, %box_smol1 ], [ %solns_beeg, %box_beeg1 ]
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
