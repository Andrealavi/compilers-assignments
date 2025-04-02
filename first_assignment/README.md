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

After successful compilation, the pass can be used with LLVM's `opt` tool in various configurations:

### Running All Optimizations

```bash
# Run all optimizations together with the combined pass
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts examples/single_function.ll -o optimized.ll
```

### Running Individual Optimization Passes

```bash
# Algebraic Identity Optimizations
opt -load-pass-plugin=build/libLocalOpts.so -passes=algebraic-identity examples/single_function.ll -o optimized.ll

# Strength Reduction
opt -load-pass-plugin=build/libLocalOpts.so -passes=strength-reduction examples/single_function.ll -o optimized.ll

# Multi-Instruction Optimizations
opt -load-pass-plugin=build/libLocalOpts.so -passes=multi-instruction examples/single_function.ll -o optimized.ll
```

### Verbose Output Options

```bash
# With verbose output showing applied optimizations
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts -local-opts-verbose examples/single_function.ll -o optimized.ll

# Verbose output also works with individual passes
opt -load-pass-plugin=build/libLocalOpts.so -passes=algebraic-identity -local-opts-verbose examples/single_function.ll -o optimized.ll
```

### Viewing Optimized Output

```bash
# View the optimized output with llvm-dis
llvm-dis optimized.ll -o - | less

# Or display the optimized file directly on stdout
opt -load-pass-plugin=./build/libLocalOpts.so -passes=local-opts examples/single_function.ll -S
```

### Using with Custom C++ Code

```bash
# Generate LLVM IR from C++ without optimization
clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm your_file.cpp -S -o your_file.ll

# Promote memory to registers (recommended before local optimizations)
opt -passes=mem2reg your_file.ll -S -o your_file.ll

# Apply local optimizations
opt -load-pass-plugin=build/libLocalOpts.so -passes=local-opts your_file.ll -o your_file_optimized.ll
```

The `-local-opts-verbose` flag enables detailed output about which optimizations were applied to each instruction, including the specific transformation type.

## Example Files

The `examples` directory contains several LLVM IR (.ll) files demonstrating different optimization scenarios:

- **Single Function Example**: Demonstrates optimizations within a simple function
- **Multiple Functions**: Tests the pass across multiple function boundaries
- **Single Function with Multiple Basic Blocks**: Shows optimizations working across basic blocks
- **Single Function with Multiple Optimizations**: Demonstrates how different optimization types interact
