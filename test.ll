; ModuleID = 'test.ll'
target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-f32:32:32-f64:32:64-v64:64:64-v128:128:128-a0:0:64-f80:32:32-n8:16:32-S128"
target triple = "i386-pc-linux-gnu"

define i32 @test_return(i32 %arg) {
  ret i32 123
}

define i64 @test_return_i64() {
  ret i64 1234100100100
}

define double @test_return_double() {
  ret double 123.456
}

define i32 @test_return_undef() {
  ret i32 undef
}

define i64 @test_i64_arg1(i64 %arg1, i64 %arg2) {
  ret i64 %arg1
}
define i64 @test_i64_arg2(i64 %arg1, i64 %arg2) {
  ret i64 %arg2
}

define i32 @test_add(i32 %arg) {
  %1 = add nsw i32 %arg, 100
  ret i32 %1
}

define i32 @test_sub(i32 %arg) {
  %1 = sub nsw i32 1000, %arg
  ret i32 %1
}

define i64 @test_load_int64(i64* %ptr) {
  %1 = load i64* %ptr
  ret i64 %1
}
define i32 @test_load_int32(i32* %ptr) {
  %1 = load i32* %ptr
  ret i32 %1
}
define i16 @test_load_int16(i16* %ptr) {
  %1 = load i16* %ptr
  ret i16 %1
}
define i8 @test_load_int8(i8* %ptr) {
  %1 = load i8* %ptr
  ret i8 %1
}

define void @test_store_int64(i64* %ptr, i64 %value) {
  store i64 %value, i64* %ptr
  ret void
}
define void @test_store_int32(i32* %ptr, i32 %value) {
  store i32 %value, i32* %ptr
  ret void
}
define void @test_store_int16(i16* %ptr, i16 %value) {
  store i16 %value, i16* %ptr
  ret void
}
define void @test_store_int8(i8* %ptr, i8 %value) {
  store i8 %value, i8* %ptr
  ret void
}

define i32* @test_load_ptr(i32** %ptr) {
  %1 = load i32** %ptr
  ret i32* %1
}

define i1 @test_compare(i32 %arg) {
  %1 = icmp eq i32 %arg, 99
  ret i1 %1
}

define i1 @test_compare_ptr(i32* %arg1, i32* %arg2) {
  %1 = icmp eq i32* %arg1, %arg2
  ret i1 %1
}

define i32 @test_branch(i32 %arg) {
  br label %label
label:
  ret i32 101
}

define i32 @test_conditional(i32 %arg) {
entry:
  %cmp = icmp eq i32 %arg, 99
  br i1 %cmp, label %iftrue, label %iffalse
iftrue:
  %ret1 = phi i32 [ 123, %entry ]
  ret i32 %ret1
iffalse:
  %ret2 = phi i32 [ 456, %entry ]
  ret i32 %ret2
}

define i32 @test_switch(i32 %arg) {
entry:
  switch i32 %arg, label %default [
    i32 1, label %match1
    i32 5, label %match5
  ]
match1:
  %ret1 = phi i32 [ 10, %entry ]
  ret i32 %ret1
match5:
  %ret5 = phi i32 [ 50, %entry ]
  ret i32 %ret5
default:
  %ret999 = phi i32 [ 999, %entry ]
  ret i32 %ret999
}

define i32 @test_phi(i32 %arg) {
  %1 = icmp eq i32 %arg, 99
  br i1 %1, label %iftrue, label %iffalse
iftrue:
  br label %return
iffalse:
  br label %return
return:
  %2 = phi i32 [ 123, %iftrue ], [ 456, %iffalse ]
  ret i32 %2
}

define i64 @test_phi_i64(i64 %arg) {
  %1 = icmp eq i64 %arg, 99
  br i1 %1, label %iftrue, label %iffalse
iftrue:
  br label %return
iffalse:
  br label %return
return:
  %2 = phi i64 [ 123, %iftrue ], [ 456, %iffalse ]
  ret i64 %2
}

define i32 @test_select(i32 %arg) {
  %1 = icmp eq i32 %arg, 99
  %2 = select i1 %1, i32 123, i32 456
  ret i32 %2
}

define i32 @test_call(i32 (i32, i32)* %func, i32 %arg1, i32 %arg2) {
  %1 = call i32 %func(i32 %arg1, i32 %arg2)
  %2 = add i32 %1, 1000
  ret i32 %2
}

define i32 @test_call2(i32 (i32, i32)* %func, i32 %arg1, i32 %arg2) {
  %1 = call i32 %func(i32 %arg1, i32 %arg2)
  ; We are checking that the args don't clobber %1.
  %2 = call i32 %func(i32 0, i32 0)
  ret i32 %1
}

define i64 @test_call_i64(i64 (i64, i64)* %func, i64 %arg1, i64 %arg2) {
  %1 = call i64 %func(i64 %arg1, i64 %arg2)
  ret i64 %1
}

define i32 @test_direct_call() {
  %1 = call i32 @test_return(i32 0)
  ret i32 %1
}

@global1 = global i32 124

define i32* @get_global() {
  ret i32* @global1
}

@string = constant [7 x i8] c"Hello!\00"

define i8* @get_global_string() {
  ret i8* getelementptr ([7 x i8]* @string, i32 0, i32 0)
}

@array = constant [3 x [2 x i16]]
  [[2 x i16] [i16 1, i16 2],
   [2 x i16] [i16 3, i16 4],
   [2 x i16] [i16 5, i16 6]]

@ptr_reloc = global i32* @global1
@ptr_zero = global i32* null

%MyStruct = type { i8, i32, i8 }
@struct_val = global %MyStruct { i8 11, i32 22, i8 33 }
@struct_zero_init = global %MyStruct zeroinitializer

; Need to handle "undef": Clang generates it for the padding at the
; end of a struct.
@undef_init = global [8 x i8] undef

@global_getelementptr = global i8* getelementptr (%MyStruct* null, i32 0, i32 2)

@global_i64 = global i64 1234100100100

; TODO: Disallow extern_weak global variables instead.
@__ehdr_start = extern_weak global i8

define i8* @get_weak_global() {
  ret i8* @__ehdr_start
}

define i32 @test_alloca() {
  %addr = alloca i32
  store i32 125, i32* %addr
  %1 = load i32* %addr
  ret i32 %1
}

define void @func_with_args(i32 %arg1, i32 %arg2) {
  ret void
}

define i32 @test_alloca2() {
  %addr = alloca i32
  store i32 125, i32* %addr
  ; We are checking that the args don't clobber %addr.
  call void @func_with_args(i32 98, i32 99)
  %1 = load i32* %addr
  ret i32 %1
}

declare void @llvm.lifetime.start(i64, i8* nocapture) nounwind
declare void @llvm.lifetime.end(i64, i8* nocapture) nounwind

define void @test_lifetime_start_and_end() {
  %addr = alloca i8
  call void @llvm.lifetime.start(i64 1, i8* %addr)
  call void @llvm.lifetime.end(i64 1, i8* %addr)
  ret void
}

define i32* @test_bitcast(i8* %arg) {
  %1 = bitcast i8* %arg to i32*
  ret i32* %1
}

define i8* @test_bitcast_global() {
  %ptr = bitcast i32** @ptr_reloc to i8*
  ret i8* %ptr
}

; TODO: Generate all these variants
; Zero-extension
define i32 @test_zext16(i32 %arg) {
  %1 = trunc i32 %arg to i16
  %2 = zext i16 %1 to i32
  ret i32 %2
}
define i32 @test_zext8(i32 %arg) {
  %1 = trunc i32 %arg to i8
  %2 = zext i8 %1 to i32
  ret i32 %2
}
define i32 @test_zext1(i32 %arg) {
  %1 = trunc i32 %arg to i1
  %2 = zext i1 %1 to i32
  ret i32 %2
}
define i64 @test_zext_32_to_64(i32 %arg) {
  %1 = zext i32 %arg to i64
  ret i64 %1
}
define i64 @test_zext_16_to_64(i16 %arg) {
  %1 = zext i16 %arg to i64
  ret i64 %1
}
; Sign-extension
define i32 @test_sext16(i32 %arg) {
  %1 = trunc i32 %arg to i16
  %2 = sext i16 %1 to i32
  ret i32 %2
}
define i32 @test_sext8(i32 %arg) {
  %1 = trunc i32 %arg to i8
  %2 = sext i8 %1 to i32
  ret i32 %2
}
define i32 @test_sext1(i32 %arg) {
  %1 = trunc i32 %arg to i1
  %2 = sext i1 %1 to i32
  ret i32 %2
}
define i64 @test_sext_32_to_64(i32 %arg) {
  %1 = sext i32 %arg to i64
  ret i64 %1
}
define i64 @test_sext_16_to_64(i16 %arg) {
  %1 = sext i16 %arg to i64
  ret i64 %1
}

define i32 @test_ptrtoint(i8* %arg) {
  %1 = ptrtoint i8* %arg to i32
  ret i32 %1
}
define i8* @test_inttoptr(i32 %arg) {
  %1 = inttoptr i32 %arg to i8*
  ret i8* %1
}

define i8* @test_getelementptr1() {
  %addr = getelementptr %MyStruct* @struct_val, i32 0, i32 2
  ret i8* %addr
}

define i16* @test_getelementptr2() {
  %addr = getelementptr [3 x [2 x i16]]* @array, i32 0, i32 2, i32 1
  ret i16* %addr
}

define i16* @test_getelementptr_constantexpr() {
  ret i16* getelementptr ([3 x [2 x i16]]* @array, i32 0, i32 2, i32 1)
}

define i8* @test_bitcast_constantexpr() {
  ret i8* bitcast ([3 x [2 x i16]]* @array to i8*)
}

define i32 @test_ptrtoint_constantexpr() {
  ret i32 ptrtoint ([3 x [2 x i16]]* @array to i32)
}

define i8* @test_inttoptr_constantexpr() {
  ret i8* inttoptr (i32 123456 to i8*)
}

@var_to_compare1 = global i32 0
@var_to_compare2 = global i32 0

define i1 @test_icmp_lt_constantexpr() {
  ; Don't use "icmp eq" here because it gets constant folded when reading
  ; the bitcode!
  ret i1 icmp ult (i32* @var_to_compare1, i32* @var_to_compare2)
}

; This tests that ExpandConstantExpr handles PHI nodes correctly.
define i32 @test_add_constantexpr_phi1() {
entry:
  br label %label
label:
  %val = phi i32 [ add (i32 ptrtoint (i32* @var_to_compare1 to i32),
                        i32 ptrtoint (i32* @var_to_compare2 to i32)), %entry ]
  ret i32 %val
}

; This tests that ExpandConstantExpr correctly handles a PHI node that
; contains the same ConstantExpr twice.
; Using replaceAllUsesWith() is not correct on a PHI node when the
; new instruction has to be added to an incoming block.
define i32 @test_add_constantexpr_phi2(i32 %arg) {
entry:
  switch i32 %arg, label %exit [
    i32 1, label %match1
    i32 2, label %match2
  ]
match1:
  br label %exit
match2:
  br label %exit
exit:
  %val = phi i32 [ add (i32 ptrtoint (i32* @var_to_compare1 to i32),
                        i32 ptrtoint (i32* @var_to_compare2 to i32)), %match1 ],
                 [ add (i32 ptrtoint (i32* @var_to_compare1 to i32),
                        i32 ptrtoint (i32* @var_to_compare2 to i32)), %match2 ],
                 [ 456, %entry ]
  ret i32 %val
}

declare void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)
declare void @llvm.memmove.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)
declare void @llvm.memset.p0i8.i32(i8*, i8, i32, i32, i1)

define void @test_memcpy(i8* %dest, i8* %src, i32 %size) {
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %size,
                                       i32 1, i1 false)
  ret void
}

define void @test_memmove(i8* %dest, i8* %src, i32 %size) {
  call void @llvm.memmove.p0i8.p0i8.i32(i8* %dest, i8* %src, i32 %size,
                                        i32 1, i1 false)
  ret void
}

define void @test_memset(i8* %dest, i8 %val, i32 %size) {
  call void @llvm.memset.p0i8.i32(i8* %dest, i8 %val, i32 %size,
                                  i32 1, i1 false)
  ret void
}

declare i8* @llvm.nacl.read.tp()

define i8* @test_nacl_read_tp() {
  %1 = call i8* @llvm.nacl.read.tp()
  ret i8* %1
}

define void @test_unreachable() {
  unreachable
}

define i32 @test_atomicrmw_xchg(i32* %ptr, i32 %val) {
  %1 = atomicrmw xchg i32* %ptr, i32 %val seq_cst
  ret i32 %1
}
