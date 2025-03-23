; C++ representation:
; int main() {
;   int x = 5;
;   int a = x + 0;                // Algebraic identity: x + 0 = x
;   int b = 15 * a;               // Strength reduction: 15 * a = (a << 4) - a
;   int c = b + 1;                // Part of multi-instruction sequence
;   int d = c - 1;                // Multi-instruction: c = b + 1, d = c - 1 => d = b
;   int e = x * 8;                // Strength reduction: x * 8 = x << 3
;   int f = (e / 2) + 0;          // Algebraic identity and strength reduction
;   return d + f;
; }

define dso_local i32 @main() {
  %x = alloca i32, align 4
  store i32 5, i32* %x, align 4
  %1 = load i32, i32* %x, align 4

  ; Algebraic identity: x + 0 = x
  %a = add nsw i32 %1, 0

  ; Strength reduction: 15 * a = (a << 4) - a
  %b = mul nsw i32 %a, 15

  ; Multi-instruction optimization opportunity
  %c = add nsw i32 %b, 1
  %d = sub nsw i32 %c, 1
  %k = add nsw i32 %d, 4

  ; Another strength reduction: x * 8 = x << 3
  %e = mul nsw i32 %1, 8

  ; Algebraic identity and strength reduction: e / 2 + 0 = e >> 1
  %f1 = sdiv i32 %e, 2
  %f = add nsw i32 %f1, 0

  ; Final result
  %result = add nsw i32 %d, %f
  ret i32 %result
}
