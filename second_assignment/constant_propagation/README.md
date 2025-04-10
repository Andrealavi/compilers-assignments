# LLVM Constant Propagation Analysis Pass

This repository contains an LLVM pass that performs constant propagation analysis. Unlike optimization passes, this analysis pass doesn't transform code but rather extracts information about constant values that can be determined at each basic block in a program.

## Overview

The Constant Propagation Analysis Pass tracks the constant values of variables across a program's control flow graph. For each basic block, it determines which variables hold known constant values and what those values are. This information can be useful for understanding program behavior and could serve as a foundation for optimizations like constant folding.

Key features:
- Tracks constant values through variable assignments
- Handles basic arithmetic operations (addition, subtraction, multiplication, division)
- Propagates constants across basic blocks
- Operates on LLVM IR level
- Provides detailed output about constant values at each basic block

## Implementation Details

The pass works by iteratively analyzing each basic block in a function until a fixed point is reached (when no new constant information is discovered). The core components include:

### Main Analysis Functions:
- `constantPropagation`: Performs the analysis on a function, iterating through each basic block
- `blockConstants`: Computes the constants for a specific basic block
- `computeConstant`: Recursively determines constant values resulting from operations
- `computeIntersection`: Combines constant information from predecessor blocks

### Data Structures:
- A map of basic blocks to their respective constant value maps (`std::map<BasicBlock*, std::map<Value*, int>>`)
- Each constant value map associates LLVM IR values (typically pointers to variables) with their known integer values

### Pattern Matching:
The pass makes extensive use of LLVM's PatternMatch module to identify and analyze different instruction patterns, such as:
- Variable assignments (stores)
- Binary operations between constants and variables
- Memory loads

### Limitations:
- Currently only handles integer constants
- Limited to basic arithmetic operations (add, subtract, multiply, divide)
- Does not handle more complex operations like bitwise operations, etc.

## Building the Pass

### Prerequisites
- CMake (version 3.20 or newer)
- LLVM (version 19 or newer)

### Build Steps

1. Clone this repository:
   ```bash
   git clone <repository-url>
   cd <repository-directory>
   ```

2. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```

3. Configure with CMake, specifying your LLVM installation:
   ```bash
   cmake -DLT_LLVM_INSTALL_DIR=/path/to/your/llvm/install ..
   ```

4. Build the pass:
   ```bash
   make
   ```

This will generate a shared library file (e.g., `libConstantPropagation.so` on Linux or `libConstantPropagation.dylib` on macOS).

## Using the Pass

### Running on LLVM IR

1. Generate LLVM IR from your source code:
   ```bash
   clang -S -emit-llvm input.c -o input.ll
   ```

2. Run the pass using opt:
   ```bash
   opt -load-pass-plugin=./build/libConstantPropagation.so -passes="constant-propagation" -disable-output input.ll
   ```

   This will analyze the code and print the constant propagation information to stdout.

### Example

For a simple example like:

```cpp
// example.cpp
#include <iostream>

int main() {
    int a = 2;
    int b, c, d, e;

    if (a > 1) {
        b = a + 2;  // b = 4
        c = 5;
    } else {
        b = a * 2;  // b = 4
        c = 8;
    }

    a = b;  // a = 4

    int f = 2;
    c = b + a;  // c = 8
    d = b * f;  // d = 8
    a = a + 1;  // a = 5

    std::cout << (b + c) << std::endl;

    return 0;
}
```

When compiled to LLVM IR and analyzed with our pass, the output will show the constants identified at each basic block, helping you understand which values can be determined at compile time.

## Output Format

The pass generates output in the following format:

1. Iterative updates during the analysis:
   ```
   Output after iteration 0

   Constants for basic block: <block_name>
   <variable>: <constant_value>
   ...

   -------------------
   ```

2. Final constant propagation results:
   ```
   Constants for function: <function_name>

   Constant propagation for basic block: <block_name>
   <variable>: <constant_value>
   ...

   ------------------
   ```

## CMake Configuration

The provided `CMakeLists.txt` handles the necessary configuration to build the pass as an LLVM plugin. It:
- Locates your LLVM installation
- Sets up the appropriate compiler flags and include directories
- Configures the build for the shared library

Key parts of the CMake configuration:
- Requires LLVM 19 or newer
- Uses C++17 standard (matching LLVM's requirements)
- Disables RTTI if LLVM was built without it
- Creates a shared library that can be loaded by LLVM's opt tool
