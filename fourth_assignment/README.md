# Custom LLVM Loop Fusion Pass

This folder contains a custom LLVM pass that implements the Loop Fusion optimization. This pass was developed as the fourth assignment for the Compilers exam.

## Algorithm Description

Loop fusion is an optimization technique that merges two or more adjacent loops into a single loop. This can improve performance by reducing loop overhead, improving instruction scheduling opportunities, and enhancing data locality.

Two loops, L1 and L2, can be fused if the following conditions hold:

1.  **Adjacency:** L1 and L2 must be directly adjacent in the control flow graph. Typically, this means the unique successor basic block of L1 (that is not part of L1's own control flow, e.g., its latch) is the preheader or header of L2.
2.  **Identical Trip Count:** L1 and L2 must iterate the same number of times.
3.  **Control Flow Equivalence:** L1 and L2 must be control-flow equivalent. If L1 executes, L2 must also execute, and vice-versa.
4.  **Data Dependency Preservation:** All original data dependencies must be preserved after fusion.
    *   If a statement in L2 at iteration `k` depends on a value computed by L1 at iteration `j`, then `k` must be greater than or equal to `j` (i.e., L2 cannot depend on a value from a "future" iteration of L1).
    *   Generally, any dependency from L2 to L1 (e.g., L1 iteration `k` depends on L2 iteration `j`) prevents fusion, as L1 executes before L2 in the original program.
    *   Essentially, the transformation must not change the semantics of the program by altering the order of dependent operations.

**Fusion Process:**
If all conditions are met, the loops are fused:
1.  The bodies of L1 and L2 are combined into a new, single loop body. Typically, the instructions from L1's body are placed before the instructions from L2's body within the new fused body.
2.  The control flow graph is modified:
    *   The preheader of L1 becomes the preheader of the new fused loop.
    *   The header of L1 (or a new common header) becomes the header of the fused loop.
    *   The latch of L2 becomes the latch of the fused loop, branching back to the fused header.
    *   The original header of L2 and the latch of L1 are bypassed or removed.

## Building the Pass

1.  Ensure your LLVM installation is accessible. You might need to set the `LLVM_DIR` environment variable to point to your LLVM installation directory (e.g., `/path/to/llvm/lib/`).

2.  Navigate to the pass directory and run:
    ```bash
    mkdir build && cd build

    # Adjust -DLT_LLVM_INSTALL_DIR if your CMake setup requires a different variable
    # or if LLVM_DIR is not set/picked up.
    cmake -DLT_LLVM_INSTALL_DIR=$LLVM_DIR ../ # LLVM_DIR is your /path/to/llvm/bin

    make
    ```
    This pass was developed and tested with LLVM 19.1.7.

## Running the Pass

The pass is registered as `lf` and is a `FunctionPass` (New PM: `FunctionAnalysisManager`-based pass).

1.  **Compile C++ to LLVM IR:**
    Generate LLVM IR without optimizations.
    ```bash
    # source.cpp is your input C++ file
    clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm source.cpp -S -o input.ll
    ```
    *Why `-Xclang -disable-O0-optnone`?* At `-O0`, Clang adds an `optnone` attribute to functions, preventing most LLVM optimization passes from running. This flag disables that behavior.

2.  **(Recommended) Run `mem2reg`:**
    Promote memory accesses to SSA registers. This often simplifies dependency analysis for loop passes.
    ```bash
    opt -passes=mem2reg input.ll -S -o opt.ll
    ```

3.  **Apply the Loop Fusion Pass:**
    ```bash
    # GNU/Linux version
    opt -load-pass-plugin=./build/libLoopFusion.so -passes=lf opt.ll -S -o fused.ll

    # macOS version
    opt -load-pass-plugin=./build/libLoopFusion.dylib -passes=lf opt.ll -S -o fused.ll
    ```

4.  **View Output on Terminal:**
    To see the output directly without writing to a file:
    ```bash
    # GNU/Linux version
    opt -load-pass-plugin=./build/libLoopFusion.so -passes=lf opt.ll -S

    # macOS version
    opt -load-pass-plugin=./build/libLoopFusion.dylib -passes=lf opt.ll -S
    ```

**Verbose Output:**
To enable verbose logging from the pass (if implemented):
```bash
opt -load-pass-plugin=./build/libLoopFusion.so -passes=lf --lf-verbose opt.ll -S
# (Adjust .so/.dylib as needed)
```

## Example

It is possible to use the example provided in the tests folder. In order to use it you can run the following commands:

```bash
clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm tests/loop.cpp -S -o input.ll
opt -passes=mem2reg input.ll -S -o opt.ll

# GNU/Linux
opt -load-pass-plugin=./build/libLoopFusion.so -passes=lf opt.ll -S

# macOS
opt -load-pass-plugin=./build/libLoopFusion.dylib -passes=lf opt.ll -S
```
