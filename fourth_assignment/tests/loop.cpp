// File: simple_loop_fusion.cpp

const int ARRAY_SIZE = 10000; // A size where cache effects can be noticeable

// Declare arrays globally or static in main for simplicity with fixed size
int source_data[ARRAY_SIZE];
int result_a[ARRAY_SIZE];
int result_b[ARRAY_SIZE];

int main() {
    // 1. Initialize the source data
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        source_data[i] = i; // Simple initialization
    }

    // 2. First loop: processes source_data and stores in result_a
    // When source_data[i] is accessed, it's likely loaded into cache.
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        result_a[i] = source_data[i] * 2;
    }

    // 3. Second loop: processes source_data again and stores in result_b
    // If these loops are not fused, source_data[i] might need to be
    // re-fetched from main memory if it was evicted from cache.
    // If fused, source_data[i] is very likely still in cache from the
    // operation in the (conceptually) same iteration.
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        result_b[i] = source_data[i-32] + 5;
    }

    // To ensure the compiler doesn't optimize away the loops and arrays
    // entirely, we can "use" the results in a trivial way.
    // For example, return a value that depends on the computed results.
    if (ARRAY_SIZE > 0) {
        return result_a[0] + result_b[ARRAY_SIZE - 1];
    }

    return 0;
}

/*
How Loop Fusion Helps Here:

Without fusion, the memory access pattern for `source_data` might look like:
- Read source_data[0], source_data[1], ..., source_data[ARRAY_SIZE-1] (for result_a)
  (Cache lines for source_data are filled)
- Read source_data[0], source_data[1], ..., source_data[ARRAY_SIZE-1] (for result_b)
  (Cache lines for source_data might need to be re-filled if evicted)

With loop fusion, a compiler might transform the two loops into one:

for (int i = 0; i < ARRAY_SIZE; ++i) {
    int temp_source_val = source_data[i]; // source_data[i] is read once
    result_a[i] = temp_source_val * 2;    // Uses the cached value
    result_b[i] = temp_source_val + 5;    // Uses the cached value
}

In this fused version, `source_data[i]` is read, and then immediately used for
both calculations before moving to `source_data[i+1]`. This improves
temporal locality for `source_data` accesses, increasing the chance of cache hits.
*/
