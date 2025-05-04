#include <vector>
#include <numeric>
#include <iostream>

// Assume these are complex objects or values determined outside the loop
struct ExternalData {
    int val1 = 10;
    int val2 = 5;
};

// Function simulating a computation within a loop
long long process_data(const ExternalData& data, int loop_limit) {
    long long total_sum = 0;
    volatile int invariant_a = 0; // Volatile to prevent compiler optimizing *before* LICM logic
    volatile int invariant_b = 0; // Volatile to prevent compiler optimizing *before* LICM logic

    for (int i = 0; i < loop_limit; ++i) {
        // --- Potential LICM Candidates ---

        // Instruction 1: Depends only on external data. Clearly invariant.
        invariant_a = data.val1 * 2; // Should be hoisted

        // Instruction 2: Depends on Instruction 1.
        // Becomes invariant *only after* Instruction 1 is identified as invariant and hoisted.
        invariant_b = invariant_a + data.val2; // Should be hoisted, but maybe not in pass 1

        // --- Loop-variant computation ---
        // This part uses the potentially invariant values and the loop counter
        int current_val = invariant_b + i;
        total_sum += current_val;

        // Simulate some other work
        if (total_sum % 100 == 0) {
           // std::cout << "."; // Slows down if uncommented
        }
    }
    // std::cout << std::endl;
    return total_sum;
}

int main() {
    ExternalData data;
    int iterations = 100000; // A non-trivial number of iterations

    long long result = process_data(data, iterations);

    std::cout << "ExternalData: val1=" << data.val1 << ", val2=" << data.val2 << std::endl;
    std::cout << "Loop iterations: " << iterations << std::endl;
    std::cout << "Calculated total_sum: " << result << std::endl;

    // Manually calculate the expected result if fully optimized
    int hoisted_a = data.val1 * 2;
    int hoisted_b = hoisted_a + data.val2;
    long long expected_sum = 0;
    for(int i = 0; i < iterations; ++i) {
        expected_sum += hoisted_b + i;
    }
     std::cout << "Expected total_sum (if fully optimized): " << expected_sum << std::endl;


    return 0;
}
