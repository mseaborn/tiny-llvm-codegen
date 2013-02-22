; ModuleID = 'test.ll'
target datalayout = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-f32:32:32-f64:32:64-v64:64:64-v128:128:128-a0:0:64-f80:32:32-n8:16:32-S128"
target triple = "i386-pc-linux-gnu"

define i32 @test_return(i32 %arg) nounwind readnone {
  ret i32 123
}

define i32 @test_add(i32 %arg) nounwind readnone {
  %1 = add nsw i32 %arg, 100
  ret i32 %1
}

define i32 @test_sub(i32 %arg) nounwind readnone {
  %1 = sub nsw i32 1000, %arg
  ret i32 %1
}

define i32 @test_load_int32(i32* nocapture %ptr) nounwind readonly {
  %1 = load i32* %ptr, align 4
  ret i32 %1
}

define void @test_store_int32(i32* nocapture %ptr, i32 %value) nounwind {
  store i32 %value, i32* %ptr, align 4
  ret void
}

define i1 @test_compare(i32 %arg) nounwind readonly {
  %1 = icmp eq i32 %arg, 99
  ret i1 %1
}

define i32 @test_branch(i32 %arg) {
  br label %label
label:
  ret i32 101
}

define i32 @test_conditional(i32 %arg) nounwind readonly {
  %1 = icmp eq i32 %arg, 99
  br i1 %1, label %iftrue, label %iffalse
iftrue:
  ret i32 123
iffalse:
  ret i32 456
}

define i32 @test_phi(i32 %arg) nounwind readonly {
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

define i32 @test_direct_call() {
  %1 = call i32 @test_return(i32 0)
  ret i32 %1
}

@global1 = global i32 124

define i32* @get_global() {
  ret i32* @global1
}
