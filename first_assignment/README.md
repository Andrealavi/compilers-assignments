# First Assignment - LLVM Optimization Pass

The first assignment for the compilers course consists of implementing a custom LLVM pass with three different optimization techniques:

- **Algebraic Identities**: Simplifying expressions based on mathematical properties
  - `x - x = 0`
  - `x / x = 1`
  - `x & x = x`, `x | x = x`, `x ^ x = 0`
  - `x + 0 = x`, `x - 0 = x`, `0 - x = -x`
  - `x * 0 = 0`, `x * 1 = x`
  - `x << 0 = x`, `x >> 0 = x`

- **Strength Reduction**: Replacing expensive operations with cheaper ones
  - `x * 2^n → x << n` (multiply by power of 2 converted to left shift)
  - `x / 2^n → x >> n` (divide by power of 2 converted to right shift)
  - `x * c → (x << ceil(log2(c))) - x` (multiply by constant approximated with shifts)

- **Multi-instruction Optimization**: Eliminating canceling operations across instructions
  - `(x + c) - c = x`
  - `(x - c) + c = x`
  - `(x * c) / c = x`
  - `(x / c) * c = x`

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
export LLVM_DIR=/usr/lib/llvm-19/bin

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
```

The `-local-opts-verbose` flag enables detailed output about which optimizations were applied to each instruction, including the specific transformation type.

## Example Files

The `examples` directory contains several LLVM IR (.ll) files demonstrating different optimization scenarios:

- **Single Function Example**: Demonstrates optimizations within a simple function
- **Multiple Functions**: Tests the pass across multiple function boundaries
- **Single Function with Multiple Basic Blocks**: Shows optimizations working across basic blocks
- **Single Function with Multiple Optimizations**: Demonstrates how different optimization types interact

To run the examples:

```bash
# Example for single function optimization
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts -local-opts-verbose examples/single_function.ll -o optimized.ll

# View the optimized output
llvm-dis optimized.ll -o - | less
```

## Implementation Details

The pass implements three main optimization functions:
- `algebraicIdentityOptimization()`: Identifies and applies algebraic simplifications
- `strengthReduction()`: Converts expensive operations to more efficient equivalents
- `multiInstructionOptimization()`: Detects and eliminates redundant operations across instructions

When the pass is run with verbose mode enabled (`-local-opts-verbose`), it prints information about each transformation, including the original instruction and the specific optimization applied.
