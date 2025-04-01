# First Assignment - LLVM Optimization Pass

The first assignment for the compilers course consists of implementing a custom LLVM pass with three different optimization techniques:

- **Algebraic Identities**: Simplifying expressions based on mathematical properties
  - `x - x = 0`
  - `x / x = 1`
  - `x & x = x`, `x | x = x`, `x ^ x = 0`
  - `x + 0 = x`, `x - 0 = x`
  - `x * 0 = 0`, `x * 1 = x`
  - `x << 0 = x`, `x >> 0 = x`
  - `x ^ 0 = x`
  - `x & -1 = x`

- **Strength Reduction**: Replacing expensive operations with cheaper ones
  - `x * 2^n → x << n` (multiply by power of 2 converted to left shift)
  - `x / 2^n → x >> n` (divide by power of 2 converted to right shift)
  - `x * (2^n - 1) → (x << n) - x` (special case for constants like 3, 7, 15)
  - `x * c → combination of shifts and adds` (for constants with few set bits)

- **Multi-instruction Optimization**: Eliminating canceling operations across instructions
  - `(x + c1) - c1 = x`
  - `(x - c1) + c1 = x`
  - `(x << c1) >> c1 = x`
  - `(x >> c1) << c1 = x`
  - `(x * c1) / c1 = x`
  - `(x / c1) * c1 = x`
  - Complex patterns like `((x + 5) + 3) - 8 = x`

## Setup and Compilation

### Environment Setup

The project includes an `init.sh` script that configures the build environment:

```bash
# Run the setup script
bash init.sh

# or alternatively
chmod +x init.sh
./init.sh

# This will:
# 1. Prompt for LLVM installation path if not set
# 2. Create and configure the build directory
# 3. Compile the pass
```

Alternatively, you can set up manually:

```bash
# Set LLVM directory path (replace with your LLVM installation)
export LLVM_DIR=/usr/lib/llvm-19/bin    # Usually this folder should be fine

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake -DLT_LLVM_INSTALL_DIR=$LLVM_DIR ../

# Build the pass
make
```

## Running the Optimization Pass

After successful compilation, the pass can be used with LLVM's `opt` tool:

```bash
# Basic usage
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts examples/single_function.ll -o optimized.ll

# With verbose output showing applied optimizations
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts -local-opts-verbose examples/single_function.ll -o optimized.ll

# View the optimized output
llvm-dis optimized.ll -o - | less

# It's possible to use only opt and display the optimized file on stdout
opt -load-pass-plugin=./build/libLocalOpts.so -passes=local-opts examples/single_function.ll -S

# To use the pass with custom LLVM IR code derived from C++ files
clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm your_file.cpp -S -o your_file.ll # Generates LLVM IR without applying any optimization

opt -passes=mem2reg your_file.ll -S -o your_file.ll
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts your_file.ll -o your_file_optimized.ll
```

The `-local-opts-verbose` flag enables detailed output about which optimizations were applied to each instruction, including the specific transformation type.

## Example Files

The `examples` directory contains several LLVM IR (.ll) files demonstrating different optimization scenarios:

- **Single Function Example**: Demonstrates optimizations within a simple function
- **Multiple Functions**: Tests the pass across multiple function boundaries
- **Single Function with Multiple Basic Blocks**: Shows optimizations working across basic blocks
- **Single Function with Multiple Optimizations**: Demonstrates how different optimization types interact

## Implementation Details

The pass implements three main optimization categories, each with its own function:

### Algebraic Identity Optimization

The `algebraicIdentityOptimization()` function recognizes mathematical patterns that can be simplified:
- Uses LLVM pattern matchers to identify binary operations with constants
- Handles cases where operations have no effect (e.g., `x + 0 = x`)
- Identifies operations that reduce to identity values (e.g., `x - x = 0`)
- When an identity is found, replaces the instruction with a simpler equivalent

### Strength Reduction

The `strengthReduction()` function converts computationally expensive operations to cheaper alternatives:
- Decomposes constants into their binary components using the `getExpSet()` helper
- Implements three main strategies:
  1. For powers of 2: converts multiplication/division to shifts
  2. For constants like 2^n-1: uses the pattern `(x << n) - x`
  3. For constants with few set bits: creates a sequence of shift and add operations
- Handles negative constants by negating the final result

### Multi-Instruction Optimization

The `multiInstructionOptimization()` function finds sequences of instructions where operations cancel out:
- Uses a worklist algorithm to traverse chains of operations
- Tracks accumulating constants to identify when operations neutralize each other
- Handles complex patterns across multiple instructions
- For cancellation patterns, replaces the sequence with the original variable

When the `-local-opts-verbose` flag is enabled, the pass prints detailed information about each transformation, including the original instruction and the specific optimization rule that was applied.
