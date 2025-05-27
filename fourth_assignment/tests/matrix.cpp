// File: matrix_loop_fusion.cpp
#include <iostream> // For potential output, not strictly needed for optimization

const int ROWS = 200; // Number of rows
const int COLS = 200; // Number of columns

// Declare matrices globally or static for simplicity with fixed size
// (In real applications, dynamic allocation or std::vector might be preferred)
int source_matrix[ROWS][COLS];
int result_matrix_A[ROWS][COLS];
int result_matrix_B[ROWS][COLS];

int main() {
    // 1. Initialize the source matrix
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            source_matrix[i][j] = i * COLS + j; // Assign a unique value to each element
        }
    }

    // 2. First loop: Process source_matrix and store in result_matrix_A
    // When source_matrix[i][j] is accessed, its cache line is likely loaded.
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            result_matrix_A[i][j] = source_matrix[i][j] * 3 + i - j;
        }
    }

    // 3. Second loop: Process source_matrix again and store in result_matrix_B
    // If not fused, source_matrix[i][j] might need to be re-fetched from main memory
    // if its cache line was evicted (e.g., by writes to result_matrix_A or other data).
    // If fused, source_matrix[i][j] is very likely still in cache from the
    // operation in the (conceptually) same iteration.
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            result_matrix_B[i][j] = source_matrix[i][j] / 2 + j - i;
        }
    }

    // To prevent the compiler from optimizing away the loops and matrices entirely,
    // we can "use" the results. Returning a value dependent on them is a common way.
    long long checksum = 0;
    if (ROWS > 0 && COLS > 0) {
        checksum = result_matrix_A[0][0] + result_matrix_B[ROWS - 1][COLS - 1];
    }
    // For demonstration, you could print a value:
    // std::cout << "Checksum: " << checksum << std::endl;

    return static_cast<int>(checksum % 256); // Return something based on results
}

/*
How Loop Fusion Helps Here:

The two main processing loops iterate over `source_matrix` in the exact same way (row-major order).

Without fusion:
- The first nested loop reads `source_matrix[i][j]` for each `(i, j)`.
- The second nested loop reads `source_matrix[i][j]` again for each `(i, j)`.
If `ROWS * COLS` is large enough, the data from `source_matrix` accessed early in the first loop
might be evicted from the cache by the time the second loop starts, or even by writes to
`result_matrix_A` and other memory accesses.

With loop fusion, a compiler might transform the two processing loops into one:

for (int i = 0; i < ROWS; ++i) {
    for (int j = 0; j < COLS; ++j) {
        int current_source_val = source_matrix[i][j]; // source_matrix[i][j] is read once

        result_matrix_A[i][j] = current_source_val * 3 + i - j; // Uses the cached value
        result_matrix_B[i][j] = current_source_val / 2 + j - i; // Uses the cached value
    }
}

In this fused version:
1. `source_matrix[i][j]` is read.
2. The computation for `result_matrix_A[i][j]` uses this value.
3. The computation for `result_matrix_B[i][j]` uses this *same* value, which is highly
   likely to still be in a CPU register or L1 cache.

This improves temporal locality for accesses to `source_matrix`, reducing cache misses and
potentially speeding up execution, especially for large matrices where memory latency is a bottleneck.
*/
