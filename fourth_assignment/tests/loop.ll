; ModuleID = 'loop.ll'
source_filename = "loop.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress noinline norecurse nounwind uwtable
define dso_local noundef i32 @main() #0 {
  br label %1

1:                                                ; preds = %5, %0
  %.03 = phi i32 [ 0, %0 ], [ %6, %5 ]
  %.01 = phi i32 [ 0, %0 ], [ %4, %5 ]
  %2 = icmp slt i32 %.03, 10
  br i1 %2, label %3, label %7

3:                                                ; preds = %1
  %4 = add nsw i32 %.01, 1
  br label %5

5:                                                ; preds = %3
  %6 = add nsw i32 %.03, 1
  br label %1, !llvm.loop !6

7:                                                ; preds = %1
  br label %8

8:                                                ; preds = %12, %7
  %.02 = phi i32 [ 0, %7 ], [ %11, %12 ]
  %.0 = phi i32 [ 0, %7 ], [ %13, %12 ]
  %9 = icmp slt i32 %.0, 10
  br i1 %9, label %10, label %14

10:                                               ; preds = %8
  %11 = add nsw i32 %.02, 1
  br label %12

12:                                               ; preds = %10
  %13 = add nsw i32 %.0, 1
  br label %8, !llvm.loop !8

14:                                               ; preds = %8
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
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
!8 = distinct !{!8, !7}
