; ModuleID = 'assignment_example.ll'
source_filename = "tests/assignment_example.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress noinline norecurse nounwind uwtable
define dso_local noundef i32 @main() #0 {
A:
  %1 = add nsw i32 3, 4
  %2 = icmp sgt i32 3, 4
  br i1 %2, label %B, label %C

B:                                                ; preds = %0
  %4 = mul nsw i32 3, 2
  %5 = icmp sgt i32 %4, 100
  br label %G

C:                                                ; preds = %0
  %9 = sub nsw i32 4, 3
  %10 = srem i32 %9, 2
  %11 = icmp eq i32 %10, 0
  br i1 %11, label %D, label %E

D:                                               ; preds = %8
  %13 = mul nsw i32 %9, %9
  %14 = add nsw i32 %1, %13
  br label %F

E:                                               ; preds = %8
  %16 = add nsw i32 %9, 10
  %17 = sub nsw i32 %1, %16
  br label %F

F:                                               ; preds = %15, %12
  br label %G

G:                                               ; preds = %19, %6
  ret i32 0
}

attributes #0 = { mustprogress noinline norecurse nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 19.1.7 (++20250114103320+cd708029e0b2-1~exp1~20250114103432.75)"}
