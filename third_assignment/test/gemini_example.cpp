// third_assignment/test/licm_test.cpp
#include <stdio.h> // For printf

// Use volatile to prevent the compiler from optimizing away calculations whose
// results are only used within the loop or for the final return value,
// making it easier to observe the effects of LICM in LLVM IR.
volatile int sink;

// A simple external function (definition not needed for IR generation)
// Calls to external functions are generally NOT loop invariant unless marked readnone/readonly.
extern int external_func(int);

int main() {
    int x = 10;
    int y = 20;
    const int z = 5;
    int loop_limit = 100;
    int result = 0; // Use volatile to ensure loop isn't optimized away

    int a = 2; // Modified in the loop
    int b = 3; // Modified in the loop

    // --- Loop 1: Basic LICM Tests ---
    printf("Starting Loop 1...\n");
    for (int i = 0; i < loop_limit; ++i) {

        // Case 1: Simple invariant calculation using variables defined outside the loop.
        // Expected: Should be hoisted.
        int inv1 = x + y;

        // Case 2: Invariant calculation using a constant and an external variable.
        // Expected: Should be hoisted.
        int inv2 = x + z; // z is const

        // Case 3: Chained invariant calculation. Depends on inv1.
        // Expected: Should be hoisted (after inv1 is identified as invariant).
        int inv3 = inv1 * 2;

        // Case 4: Calculation that is NOT invariant because 'a' is modified below.
        // Expected: Should NOT be hoisted.
        int not_inv1 = a + x;

        // Case 5: Calculation that is NOT invariant because 'b' is modified below.
        // Expected: Should NOT be hoisted.
        int not_inv2 = b * y;

        // Case 6: Calculation involving loop counter 'i'. Generally NOT invariant.
        // Expected: Should NOT be hoisted.
        int loop_dependent = y + i;

        // Modify loop-carried variables
        a = b + 1; // 'a' depends on 'b' from the *previous* iteration (or initial value)
        b = i;     // 'b' depends on the loop counter

        // Use the results to prevent them from being dead code
        result += inv1 + inv2 + inv3 + not_inv1 + not_inv2 + loop_dependent;

        // Case 7: Invariant calculation inside a conditional block.
        // Operands (x, z) are invariant.
        // Expected: The calculation 'x * z' should be hoisted, even if the condition isn't always true.
        // The result is conditionally added to 'result'.
        if (i % 10 == 0) {
            int inv_in_if = x * z;
            result += inv_in_if;
        } else {
            // Case 8: Another invariant calculation in the 'else' branch.
            // Expected: 'y / 2' should be hoisted.
            int inv_in_else = y / 2; // Assuming y is non-zero
            result += inv_in_else;
        }

        // Case 9: Calculation that *looks* invariant but depends on a variable modified
        // within the loop ('a' was modified above).
        // Expected: Should NOT be hoisted.
        int looks_inv = a + 5;
        result += looks_inv;

    } // End of Loop 1

    printf("After Loop 1, result (volatile): %d\n", result);
    printf("After Loop 1, a=%d, b=%d\n", a, b); // Check final values

    // Reset for next loop
    a = 100;
    b = 200;
    int invariant_val_for_loop2 = x - y; // Truly invariant for Loop 2

    // --- Loop 2: Testing Dominance and Potential Issues ---
    printf("Starting Loop 2...\n");
    // Note: This loop might not execute if loop_limit <= 0 (though it's 100 here)
    for (int j = 0; j < loop_limit; ++j) {

        // Case 10: Use a value computed invariantly outside this loop.
        // Expected: No hoisting needed, already outside.
        result += invariant_val_for_loop2;

        // TODO: CONSIDER OPERATION SAFETY

        // Case 11: A potentially unsafe operation (division).
        int potentially_unsafe_inv = 100 / y;
        result += potentially_unsafe_inv;

        // Case 12: Function call. Typically NOT invariant unless marked specially
        // (e.g., readonly, readnone attributes in LLVM).
        // Expected: Should NOT be hoisted by a standard, safe LICM pass.
        // result += external_func(x); // Keep commented unless you want to test function calls

        // Modify loop variables differently
        a -= 1;
        b += j;

        // Case 13: Invariant calculation whose result is used *after* the loop.
        // The calculation itself (x * y) is invariant.
        // Expected: Should be hoisted. The final value of 'inv_used_later'
        // after the loop will be the single hoisted value.
        int inv_used_later = x * y;
        if (j == loop_limit - 1) { // Only use sink on last iteration for demo
             sink = inv_used_later; // Use volatile sink to ensure calculation isn't dead
        }


    } // End of Loop 2

    printf("After Loop 2, result (volatile): %d\n", result);
    printf("After Loop 2, a=%d, b=%d\n", a, b);

    // Example of using a potentially hoisted value after the loop
    // Note: If LICM hoists 'inv_used_later', its value computed once before the loop
    // is what would be available here IF it were returned or stored globally.
    // Accessing the stack variable 'inv_used_later' after the loop is technically
    // out of scope, but demonstrates the concept for IR level.
    // We used 'sink' inside the loop to keep it alive instead.
    // printf("Value potentially hoisted and used later (via sink): %d\n", sink);


    // --- Loop 3: Empty Loop ---
    printf("Starting Loop 3 (Empty)...\n");
    for (int k = 0; k < 0; ++k) {
        // Test behavior with zero iterations. Hoisted code should still execute once.
        int inv_in_empty = x + y;
        result += inv_in_empty; // This line will never execute
    }
    printf("After Loop 3, result (volatile): %d\n", result);


    // --- Loop 4: Nested Loops ---
    printf("Starting Loop 4 (Nested)...\n");
    int outer_inv = x + 1; // Invariant for both outer and inner loops
    for (int m = 0; m < 5; ++m) { // Outer loop
        int outer_var = m * 2; // Variant for outer loop
        int inner_inv = y + 2; // Invariant for both outer and inner loop

        result += outer_inv; // Use outer invariant

        for (int n = 0; n < 5; ++n) { // Inner loop
            // Case 14: Uses outer invariant 'outer_inv'. Invariant for inner loop.
            // Expected: Hoisted outside inner loop (already outside).
            int use_outer_inv = outer_inv + n; // Depends on n, variant for inner loop

            // Case 15: Uses 'inner_inv'. Invariant for inner loop.
            // Expected: Hoisted outside inner loop (to outer loop's body).
            int use_inner_inv = inner_inv * 3;

            // Case 16: Uses outer loop variable 'outer_var'. Invariant for inner loop.
            // Expected: Hoisted outside inner loop (to outer loop's body).
            int use_outer_var = outer_var + z;

            // Case 17: Uses inner loop variable 'n'. Variant for inner loop.
            // Expected: Not hoisted outside inner loop.
            int use_inner_var = n + x;

            result += use_outer_inv + use_inner_inv + use_outer_var + use_inner_var;
        }
    }
     printf("After Loop 4, result (volatile): %d\n", result);


    printf("Final result (volatile): %d\n", result);
    return 0; // Return result makes it harder to optimize away entirely
}
