; C++ representation:
;
; int compute_value(int x) {
;   int a = x + 0;                // Algebraic identity: x + 0 = x
;   int b = x - 0;                // Algebraic identity: x - 0 = x
;   int c = x * 1;                // Algebraic identity: x * 1 = x
;   int d = x / 1;                // Algebraic identity: x / 1 = x
;   return a + b + c + d;
; }
;
; int perform_multiplication(int x, int y) {
;   int a = x * 8;                // Strength reduction: x * 8 = x << 3
;   int b = y * 4;                // Strength reduction: y * 4 = y << 2
;   int c = x * 10;               // Strength reduction: x * 10 = (x << 3) + (x << 1)
;   int d = y * 15;               // Strength reduction: y * 15 = (y << 4) - y
;   return a + b + c + d;
; }
;
; int multi_instruction_opt(int x) {
;   int a = x + 5;                // Multi-instruction sequence
;   int b = a - 5;                // Can be optimized to b = x
;
;   int c = x * 2;                // Multi-instruction sequence
;   int d = c / 2;                // Can be optimized to d = x
;
;   int e = x + 10;               // Multi-instruction sequence
;   int f = e + 20;               // These two can be combined: f = x + 30
;
;   return b + d + f;
; }
;
; int mixed_optimizations(int x, int y) {
;   int a = x * 0;                // Algebraic identity: x * 0 = 0
;   int b = y * 64;               // Strength reduction: y * 64 = y << 6
;
;   int c = x + 10;               // Multi-instruction sequence
;   int d = c - 10;               // Can be optimized to d = x
;
;   int e = b + 0;                // Algebraic identity: b + 0 = b
;   return d + e + a;             // Which is x + (y << 6) + 0 = x + (y << 6)
; }
;
; int main() {
;   int result1 = compute_value(5);
;   int result2 = perform_multiplication(3, 4);
;   int result3 = multi_instruction_opt(7);
;   int result4 = mixed_optimizations(10, 2);
;   return result1 + result2 + result3 + result4;
; }

define dso_local i32 @compute_value(i32 %x) {
  ; Algebraic identity: x + 0 = x
  %a = add nsw i32 %x, 0

  ; Algebraic identity: x - 0 = x
  %b = sub nsw i32 %x, 0

  ; Algebraic identity: x * 1 = x
  %c = mul nsw i32 %x, 1

  ; Algebraic identity: x / 1 = x
  %d = sdiv i32 %x, 1

  ; Sum all results
  %tmp1 = add nsw i32 %a, %b
  %tmp2 = add nsw i32 %tmp1, %c
  %result = add nsw i32 %tmp2, %d

  ret i32 %result
}

define dso_local i32 @perform_multiplication(i32 %x, i32 %y) {
  ; Strength reduction: x * 8 = x << 3
  %a = mul nsw i32 %x, 8

  ; Strength reduction: y * 4 = y << 2
  %b = mul nsw i32 %y, 4

  ; Strength reduction: x * 10 = (x << 3) + (x << 1)
  %c = mul nsw i32 %x, 10

  ; Strength reduction: y * 15 = (y << 4) - y
  %d = mul nsw i32 %y, 15

  ; Sum all results
  %tmp1 = add nsw i32 %a, %b
  %tmp2 = add nsw i32 %tmp1, %c
  %result = add nsw i32 %tmp2, %d

  ret i32 %result
}

define dso_local i32 @multi_instruction_opt(i32 %x) {
  ; Multi-instruction sequence: a = x + 5, b = a - 5 => b = x
  %a = add nsw i32 %x, 5
  %b = sub nsw i32 %a, 5

  ; Multi-instruction sequence: c = x * 2, d = c / 2 => d = x
  %c = mul nsw i32 %x, 2
  %d = sdiv i32 %c, 2

  ; Multi-instruction sequence: e = x + 10, f = e + 20 => f = x + 30
  %e = add nsw i32 %x, 10
  %f = add nsw i32 %e, 20

  ; Sum all results
  %tmp = add nsw i32 %b, %d
  %result = add nsw i32 %tmp, %f

  ret i32 %result
}

define dso_local i32 @mixed_optimizations(i32 %x, i32 %y) {
  ; Algebraic identity: x * 0 = 0
  %a = mul nsw i32 %x, 0

  ; Strength reduction: y * 64 = y << 6
  %b = mul nsw i32 %y, 64

  ; Multi-instruction sequence: c = x + 10, d = c - 10 => d = x
  %c = add nsw i32 %x, 10
  %d = sub nsw i32 %c, 10

  ; Algebraic identity: b + 0 = b
  %e = add nsw i32 %b, 0

  ; Calculate final result: d + e + a = x + (y << 6) + 0
  %tmp = add nsw i32 %d, %e
  %result = add nsw i32 %tmp, %a

  ret i32 %result
}

define dso_local i32 @main() {
  ; Call compute_value with argument 5
  %result1 = call i32 @compute_value(i32 5)

  ; Call perform_multiplication with arguments 3 and 4
  %result2 = call i32 @perform_multiplication(i32 3, i32 4)

  ; Call multi_instruction_opt with argument 7
  %result3 = call i32 @multi_instruction_opt(i32 7)

  ; Call mixed_optimizations with arguments 10 and 2
  %result4 = call i32 @mixed_optimizations(i32 10, i32 2)

  ; Sum all results from function calls
  %tmp1 = add nsw i32 %result1, %result2
  %tmp2 = add nsw i32 %tmp1, %result3
  %final_result = add nsw i32 %tmp2, %result4

  ret i32 %final_result
}
