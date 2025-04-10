# Dominator Analysis LLVM Pass

This repository contains a custom LLVM pass for performing dominator analysis on LLVM IR. The pass identifies and reports which basic blocks dominate each basic block in a function.

## What is Dominator Analysis?

In compiler theory, a basic block A is said to **dominate** another basic block B if every path from the entry block to B must go through A. This information is crucial for many compiler optimizations and analyses, such as loop optimizations, control flow analysis, and code motion.

This pass computes and displays dominator relationships for all basic blocks in each function of a module.

## Implementation Details

The dominator analysis pass is implemented using the LLVM Pass infrastructure. The core implementation consists of:

1. `dominatorAnalysis.hpp` - Header defining the DominatorAnalysis class
2. `dominatorAnalysis.cpp` - Implementation of the algorithm and pass structure

The implementation uses an iterative algorithm to compute the dominators:
- For the entry block, only itself is its dominator
- For other blocks, their dominators are the intersection of dominators of all their predecessors, plus themselves
- The algorithm iterates until no changes are made to the dominator sets

The pass produces detailed output for each iteration of the algorithm, showing how the dominator sets evolve, and the final result after convergence.

## Building the Pass

To build the pass:

1. Make sure you have LLVM 19+ installed on your system
2. Configure the build with CMake
3. Build the pass

```bash
mkdir build
cd build
cmake -DLT_LLVM_INSTALL_DIR=/path/to/your/llvm/installation ..
make
```

Replace `/path/to/your/llvm/installation` with the path to your LLVM installation. This should be the directory containing `lib/cmake/llvm/LLVMConfig.cmake`.

## Using the Pass

The pass can be used with LLVM's `opt` tool to analyze LLVM IR files:

```bash
opt -load-pass-plugin=./build/libDominatorAnalysis.so -passes=dominator-analysis input.ll -disable-output
```

Where:
- `./build/libDominatorAnalysis.so` is the path to the compiled pass
- `input.ll` is your LLVM IR file to analyze
- `-disable-output` prevents `opt` from writing the IR to stdout (since this is only an analysis pass)

## Sample Output

For each function, the pass will output:
- Intermediate results showing how dominator sets evolve in each iteration
- Final list of dominators for each basic block

Example output for a basic block might look like:
```
Dominators for basic block: then1
entry
then1
```

This shows that the basic block named `then1` is dominated by the `entry` block and itself.

## Requirements

- LLVM 19+
- C++17 compatible compiler
- CMake 3.20 or higher

## Limitations

- This is an educational implementation for demonstration purposes
- For production use, consider using LLVM's built-in DominatorTree analysis
- The output is verbose and designed for educational inspection rather than machine consumption
