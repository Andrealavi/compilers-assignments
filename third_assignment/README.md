# Loop Invariant Code Motion (LICM) Pass

This is a custom implementation of the Loop Invatiant Code Motion (LICM) optimization pass made as the third assignment of the Compilers course (Assignment 3).

The pass identifies instructions within loops whose results do not change across loop iterations and moves them to the loop's preheader, reducing redundant computations.

## Algorithm Description

This LICM pass implementation iterates over every loop in the program identified using LoopInfo analysis. The pass checks what are the operations that are loop invariant and safe to move and then moves them into the loop preheader.

### Detailed description

1.  **Iteration:** The pass iterates through all loops in the function. For each loop, it requires a preheader (a block that dominates the loop header and has the header as its only successor).
2.  **Invariant Identification:** It traverses the instructions within each basic block of the loop to identify candidates for hoisting. An instruction is considered loop-invariant if it meets the following criteria:
    *   **Operands:** All its operands are either:
        *   Constants.
        *   Defined outside the current loop.
        *   Results of other instructions already identified as loop-invariant in this pass.
    *   **Safety Exclusions:** The pass explicitly avoids hoisting:
        *   `BranchInst`: Modifying control flow is unsafe.
        *   `ReturnInst`: Cannot be moved out of its function context.
        *   `CallInst`: Due to potential side effects, calls are conservatively considered non-invariant by this pass. (More advanced LICM might analyze `readonly` or `readnone` attributes).
        *   Terminator instructions in general.
        *   PHINodes instruction since they cannot be hoisted 
3.  **Hoisting:** Instructions identified as invariant are candidates for hoisting. Before moving:
    *   **Dominance:** Before actually hoist instruction we need to check their dominance. Specifically we have to check that a loop invariant instruction dominates all its uses, even the ones outside of the loop. This implicitly test if the instruction dominates all loops exits. 
    *   **Movement:** Safe, invariant instructions are moved from their original basic block to the end of the loop's preheader block (just before the preheader's terminator instruction).

## Building the Pass

Along with the pass implementation come several examples that can be tested in order to evaluate the pass effectiveness.

To use the pass, we need to build it from source, using `cmake`.

```bash
mkdir build && cd build

# Configure CMake. Replace $LLVM_DIR with the actual path
# to your LLVM installation directory (e.g., /usr/lib/llvm-14, ~/llvm-project/build)
# This directory should contain include/, lib/, bin/ etc.
cmake -DLT_LLVM_INSTALL_DIR=$LLVM_DIR ../ # Make sure to have your path to llvm/bin as LLVM_DIR

# Build the pass library and return to the project root directory
make && cd ..
```

## Running the Pass

After having build the pass, you can test it with the following commands:

```bash
clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm test/example.cpp -S -o example.ll

opt -passes=mem2reg example.ll -S -o example.ll

# With the following commands, the optimized IR code will be printed directly on the terminal
opt -load-pass-plugin=./build/libLoopInvariantCodeMotion.so -passes=loop-inv-cm example.ll -S # For GNU/Linux OS
opt -load-pass-plugin=./build/libLoopInvariantCodeMotion.dylib -passes=loop-inv-cm example.ll -S # For MacOS

# With the following commands, the optimized IR code will be placed in the given file
opt -load-pass-plugin=./build/libLoopInvariantCodeMotion.so -passes=loop-inv-cm example.ll -S -o example.ll # For GNU/Linux OS
opt -load-pass-plugin=./build/libLoopInvariantCodeMotion.dylib -passes=loop-inv-cm example.ll -S -o example.ll # For MacOS

# It is possible to use the flag --licm-verbose flag in order to show debugging information (the instruction that are considered loop invariant)

# If you want to execute the optimized IR
clang++ -o example.out example.ll
```
