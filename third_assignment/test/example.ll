; ModuleID = 'example.ll'
source_filename = "test/example.cpp"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

; Function Attrs: mustprogress noinline norecurse nounwind ssp uwtable(sync)
define noundef i32 @main() #0 {
  br label %1

1:                                                ; preds = %16, %0
  %.01 = phi i32 [ 0, %0 ], [ %17, %16 ]
  %.0 = phi i32 [ 2, %0 ], [ %6, %16 ]
  %2 = icmp slt i32 %.01, 10
  br i1 %2, label %3, label %18

3:                                                ; preds = %1
  %4 = add nsw i32 %.0, 3
  %5 = add nsw i32 %.0, 4
  %6 = add nsw i32 3, 1
  %7 = icmp sgt i32 %6, 3
  br i1 %7, label %8, label %11

8:                                                ; preds = %3
  %9 = mul nsw i32 3, 2
  %10 = add nsw i32 %6, %9
  br label %14

11:                                               ; preds = %3
  %12 = sdiv i32 3, 2
  %13 = add nsw i32 %5, %12
  br label %14

14:                                               ; preds = %11, %8
  %15 = add nsw i32 %4, %5
  br label %16

16:                                               ; preds = %14
  %17 = add nsw i32 %.01, 1
  br label %1, !llvm.loop !6

18:                                               ; preds = %1
  ret i32 0
}

attributes #0 = { mustprogress noinline norecurse nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a,+zcm,+zcz" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 4]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 19.1.7"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
