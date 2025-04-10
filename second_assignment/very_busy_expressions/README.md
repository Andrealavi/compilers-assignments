# Very Busy Expressions Analysis LLVM Pass

## Overview

The Very Busy Expressions Analysis pass is an LLVM module pass that analyzes code to identify expressions (instructions) that are "very busy" at each program point. An expression is considered "very busy" at a program point if it will be evaluated along every path starting from that point before any of its operands are redefined.

This pass is purely an analysis tool; it does not modify the IR or perform any optimizations. It provides information that can be valuable for subsequent optimization passes, such as code motion and common subexpression elimination.

## Implementation Details

The pass implements a dataflow analysis algorithm that works on the LLVM IR level. It operates using a backward flow analysis to compute very busy expressions for each basic block.

### Key Components

1. **Expression Equality**: The pass defines what it means for two instructions to be equal:
   - They must both be binary operations
   - They must have the same opcode
   - Their operands must be the same or, for commutative operations, in reverse order
   - If operands involve loads, it checks whether they load from the same memory locations

2. **Memory Handling**: The pass uses LLVM's MemorySSA to track memory dependencies and determine when a pointer might be modified.

3. **Analysis Algorithm**: The implementation follows these steps:
   - Processes basic blocks in reverse order
   - For each basic block, computes the intersection of very busy expressions from its successors
   - Processes instructions in forward order to identify killed expressions and add new very busy expressions
   - Iterates until a fixed point is reached

4. **Output**: The pass prints the very busy expressions for each basic block, showing both intermediate iterations and the final result.

## Building the Pass

### Prerequisites

- LLVM 19 or higher
- CMake 3.20 or higher
- A C++17 compatible compiler

### Build Instructions

1. Clone this repository
2. Create a build directory:
   ```bash
   mkdir build && cd build
   ```
3. Configure the build, specifying the path to your LLVM installation:
   ```bash
   cmake -DLT_LLVM_INSTALL_DIR=/path/to/your/llvm/installation ..
   ```
4. Build the pass:
   ```bash
   cmake --build .
   ```

This will create a shared library `libVeryBusyExpressions.so` (or `.dylib` on macOS) that can be loaded by LLVM's `opt` tool.

## Using the Pass

### Running with opt

To run the pass on an LLVM IR file:

```bash
opt -load-pass-plugin=./libVeryBusyExpressions.so -passes=very-busy input.ll
```

The pass is registered with the name `very-busy`.

### Example Output

Running the pass on a program will produce output similar to:

```
Output after iteration 1
...

Final output after N iterations

Dominators for function: functionName

Very Busy Expressions for basic block: blockName
%17 = mul nsw i32 %15, %16
%19 = add nsw i32 %18, %17
...
```

The output shows:
1. Intermediate results after each iteration
2. The final results showing very busy expressions for each basic block
3. The number of iterations required to reach a fixed point

## Example

Given the input file `example.ll`, which contains a simple C++ program with conditional branches, the pass will identify expressions that are "very busy" at each point in the program. For instance, the multiplication operation `%mul = mul nsw i32 %operand1, %operand2` might be identified as very busy in multiple blocks if it's computed along all paths from certain program points.

## Notes on Implementation

- The analysis handles commutative binary operations appropriately
- The pass uses memory SSA analysis to reason about pointer aliases and memory modifications
- The equality check between expressions handles both register-based and memory-based operands
