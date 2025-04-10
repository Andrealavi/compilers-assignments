; ModuleID = 'reaching_definitions_test.ll'
source_filename = "reaching_definitions_test.c"

define i32 @test_reaching_defs(i32 %arg1, i32 %arg2) {
entry:
  ; Initial definitions
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  %result = alloca i32

  ; First set of definitions
  store i32 %arg1, i32* %a
  store i32 %arg2, i32* %b
  store i32 5, i32* %c

  %cond1 = icmp sgt i32 %arg1, 10
  br i1 %cond1, label %then1, label %else1

then1:
  ; Redefine 'a'
  store i32 20, i32* %a
  %b_val1 = load i32, i32* %b
  %b_plus_5 = add i32 %b_val1, 5
  store i32 %b_plus_5, i32* %b  ; Redefine 'b'
  br label %join1

else1:
  ; Alternative definitions
  store i32 30, i32* %a  ; Another definition of 'a'
  %c_val1 = load i32, i32* %c
  %c_plus_10 = add i32 %c_val1, 10
  store i32 %c_plus_10, i32* %c  ; Redefine 'c'
  br label %join1

join1:
  %a_val1 = load i32, i32* %a  ; Uses the value of 'a' from either then1 or else1
  %cond2 = icmp slt i32 %a_val1, 25
  br i1 %cond2, label %then2, label %else2

then2:
  %b_val2 = load i32, i32* %b
  %c_val2 = load i32, i32* %c
  %sum1 = add i32 %b_val2, %c_val2
  store i32 %sum1, i32* %result
  br label %exit

else2:
  ; Redefine 'c' again
  store i32 100, i32* %c
  %a_val2 = load i32, i32* %a
  %c_val3 = load i32, i32* %c
  %sum2 = add i32 %a_val2, %c_val3
  store i32 %sum2, i32* %result
  br label %exit

exit:
  ; Complex phi-like situation for reaching defs
  %a_val3 = load i32, i32* %a
  %b_val3 = load i32, i32* %b
  %c_val4 = load i32, i32* %c

  ; Final computation using all variables
  %temp1 = add i32 %a_val3, %b_val3
  %temp2 = add i32 %temp1, %c_val4
  %final_result = load i32, i32* %result
  %return_val = add i32 %final_result, %temp2

  ret i32 %return_val
}

define i32 @simple_loop(i32 %n) {
entry:
  %i = alloca i32
  %sum = alloca i32

  ; Initialize variables
  store i32 0, i32* %i
  store i32 0, i32* %sum

  br label %loop_header

loop_header:
  %i_val = load i32, i32* %i
  %sum_val = load i32, i32* %sum

  %cond = icmp slt i32 %i_val, %n
  br i1 %cond, label %loop_body, label %loop_exit

loop_body:
  ; Update sum
  %new_sum = add i32 %sum_val, %i_val
  store i32 %new_sum, i32* %sum

  ; Increment i
  %i_incr = add i32 %i_val, 1
  store i32 %i_incr, i32* %i

  br label %loop_header

loop_exit:
  %final_sum = load i32, i32* %sum
  ret i32 %final_sum
}
