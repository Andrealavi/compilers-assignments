; C++ representation:
;
; int multi_optimization_opportunities(int x, int y) {
;   // Multiple optimizations possible on these operations
;   int a = x + 0;                // Algebraic identity: x + 0 = x
;   int b = a * 16;               // First strength reduction: a * 16 = a << 4
;                                 // Combined with previous: x * 16 = x << 4
;
;   int c = y * 1;                // Algebraic identity: y * 1 = y
;   int d = c + 10;               // Direct computation on constant
;   int e = d - 10;               // Multi-instruction: d = c + 10, e = d - 10 => e = c
;                                 // Combined with previous: e = y
;
;   int f = b + 0;                // Algebraic identity: b + 0 = b
;                                 // Combined with previous: f = x << 4
;
;   int g = f * 2;                // Strength reduction: f * 2 = f << 1
;                                 // Combined with previous: g = x << 5
;
;   // This has at least 3 possible optimizations:
;   // 1. Algebraic identity: g * 1 = g
;   // 2. Propagating previous optimizations: g = x << 5
;   // 3. Multi-instruction with next operation: g * 1 + e = g + e = (x << 5) + y
;   int h = g * 1;
;
;   return h + e;                 // Could be optimized to: return (x << 5) + y
; }

define dso_local i32 @multi_optimization_opportunities(i32 %x, i32 %y) {
  ; First chain of optimizations
  ; Algebraic identity: x + 0 = x
  %a = add nsw i32 %x, 0

  ; Strength reduction: a * 16 = a << 4
  ; Combined with previous: x * 16 = x << 4
  %b = mul nsw i32 %a, 16

  ; Second chain of optimizations
  ; Algebraic identity: y * 1 = y
  %c = mul nsw i32 %y, 1

  ; Multi-instruction optimization opportunity
  %d = add nsw i32 %c, 10
  %e = sub nsw i32 %d, 10
  ; Combined optimizations: e = y

  ; Third chain of optimizations
  ; Algebraic identity: b + 0 = b
  %f = add nsw i32 %b, 0

  ; Strength reduction: f * 2 = f << 1
  ; Combined with previous: g = x << 5
  %g = mul nsw i32 %f, 2

  ; Multiple optimization opportunities here:
  ; 1. Algebraic identity: g * 1 = g
  ; 2. Propagating previous: g = x << 5
  ; 3. Multi-instruction combination with final return
  %h = mul nsw i32 %g, 1

  ; Final calculation
  ; Could be entirely optimized to: return (x << 5) + y
  %result = add nsw i32 %h, %e

  ret i32 %result
}
