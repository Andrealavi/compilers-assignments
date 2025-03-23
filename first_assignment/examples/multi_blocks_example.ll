; C++ representation:
;
; int complex_calculations(int x, int y, bool condition1, bool condition2) {
;   int result = 0;
;
;   // First condition block
;   if (condition1) {
;     int a = x + 0;                // Algebraic identity: x + 0 = x
;     int b = y * 16;               // Strength reduction: y * 16 = y << 4
;     result = a + b;
;   } else {
;     int c = x * 1;                // Algebraic identity: x * 1 = x
;     int d = y * 32;               // Strength reduction: y * 32 = y << 5
;     result = c + d;
;   }
;
;   // Second condition block
;   if (condition2) {
;     int e = result + 10;          // Part of multi-instruction sequence
;     int f = e - 10;               // Multi-instruction: e = result + 10, f = e - 10 => f = result
;     int g = x * 24;               // Strength reduction: x * 24 = (x << 4) + (x << 3)
;     result = f + g;
;   } else {
;     int h = result * 1;           // Algebraic identity: result * 1 = result
;     int i = y * 12;               // Strength reduction: y * 12 = (y << 3) + (y << 2)
;     int j = i + 0;                // Algebraic identity: i + 0 = i
;
;     int k = y + 5;                // Part of multi-instruction sequence
;     int l = k - 5;                // Multi-instruction: k = y + 5, l = k - 5 => l = y
;
;     result = h + j + l;
;   }
;
;   return result;
; }

define dso_local i32 @complex_calculations(i32 %x, i32 %y, i1 %condition1, i1 %condition2) {
entry:
  %result = alloca i32, align 4
  store i32 0, i32* %result, align 4
  br i1 %condition1, label %if.then, label %if.else

if.then:                                          ; First condition: true branch
  ; Algebraic identity: x + 0 = x
  %a = add nsw i32 %x, 0

  ; Strength reduction: y * 16 = y << 4
  %b = mul nsw i32 %y, 16

  ; result = a + b
  %add1 = add nsw i32 %a, %b
  store i32 %add1, i32* %result, align 4
  br label %if.end

if.else:                                          ; First condition: false branch
  ; Algebraic identity: x * 1 = x
  %c = mul nsw i32 %x, 1

  ; Strength reduction: y * 32 = y << 5
  %d = mul nsw i32 %y, 32

  ; result = c + d
  %add2 = add nsw i32 %c, %d
  store i32 %add2, i32* %result, align 4
  br label %if.end

if.end:                                           ; End of first condition
  ; Branch based on the second condition
  br i1 %condition2, label %if.then2, label %if.else2

if.then2:                                         ; Second condition: true branch
  ; Load result value
  %result_val1 = load i32, i32* %result, align 4

  ; Multi-instruction optimization: e = result + 10, f = e - 10 => f = result
  %e = add nsw i32 %result_val1, 10
  %f = sub nsw i32 %e, 10

  ; Strength reduction: x * 24 = (x << 4) + (x << 3)
  %g = mul nsw i32 %x, 24

  ; result = f + g
  %add3 = add nsw i32 %f, %g
  store i32 %add3, i32* %result, align 4
  br label %if.end2

if.else2:                                         ; Second condition: false branch
  ; Load result value
  %result_val2 = load i32, i32* %result, align 4

  ; Algebraic identity: result * 1 = result
  %h = mul nsw i32 %result_val2, 1

  ; Strength reduction: y * 12 = (y << 3) + (y << 2)
  %i = mul nsw i32 %y, 12

  ; Algebraic identity: i + 0 = i
  %j = add nsw i32 %i, 0

  ; Multi-instruction optimization: k = y + 5, l = k - 5 => l = y
  %k = add nsw i32 %y, 5
  %l = sub nsw i32 %k, 5

  ; result = h + j + l
  %add4 = add nsw i32 %h, %j
  %add5 = add nsw i32 %add4, %l
  store i32 %add5, i32* %result, align 4
  br label %if.end2

if.end2:                                          ; End of second condition
  ; Return the final result
  %result_val3 = load i32, i32* %result, align 4
  ret i32 %result_val3
}
