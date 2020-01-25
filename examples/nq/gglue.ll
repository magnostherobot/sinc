target triple = "x86_64-unknown-linux-gnu"

declare fastcc i8* @gen(i8*, i8*)

declare fastcc i8* @length (i8*)

define fastcc i32 @"+"(i32, i32) {
  %res = add i32 %0, %1
  ret i32 %res
}

define fastcc i32 @-(i32, i32) {
  %res = sub i32 %0, %1
  ret i32 %res
}

define fastcc i32 @count_solns(i32) {
  %cast = inttoptr i32 %0 to i8*
  %solns = call fastcc i8* @gen(i8* %cast, i8* %cast)
  %n_solns = call fastcc i8* @length(i8* %solns)
  %res = ptrtoint i8* %n_solns to i32
  ret i32 %res
}

define fastcc i32 @_count_solns(i32) {
  %ext = zext i32 %0 to i128
  %alloc = alloca i128
  store i128 %ext, i128* %alloc
  %box = bitcast i128* %alloc to i8*
  %solns = call fastcc i8* @gen(i8* %box, i8* %box)
  %n_solns = call fastcc i8* @length(i8* %solns)
  %uncast = bitcast i8* %n_solns to i128*
  %unboxed = load i128, i128* %uncast
  %res = trunc i128 %unboxed to i32
  ret i32 %res
}
