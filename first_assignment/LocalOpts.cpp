#include "LocalOpts.hpp"
#include <iostream>
#include <llvm-19/llvm/ADT/ADL.h>

using namespace llvm;

/**
 * Command-line option that enables verbose output for the optimizer.
 * When enabled, the pass will print detailed information about
 * each optimization that is applied, including:
 * - The original instruction before optimization
 * - The specific optimization rule that was applied
 *
 * Use with `-local-opts-verbose` flag when running the LLVM opt tool.
 */
static cl::opt<bool> LocalOptsVerbose(
    "local-opts-verbose",
    cl::desc(
        "Enables verbose output for the different optimizations"
    ),
    cl::init(false)
);

/**
 * Optimize instructions based on algebraic identities.
 *
 * This function recognizes and applies common algebraic simplification rules
 * to reduce computation overhead. It handles several categories of identities:
 *
 * Zero-based identities:
 * - x - x = 0, x ^ x = 0 (resulting in constant zero)
 * - x * 0 = 0 (multiplication by zero)
 *
 * Identity operations:
 * - x + 0 = x, x - 0 = x, x << 0 = x, x >> 0 = x, x ^ 0 = x (no effect)
 * - x & x = x, x | x = x (idempotent operations)
 * - x / x = 1 (division by self)
 * - x * 1 = x, x / 1 = x (multiplication/division by one)
 *
 * @param inst The instruction to be optimized
 * @return true if the instruction was optimized (and marked for removal), false otherwise
 */
bool algebraicIdentityOptimization(Instruction &inst) {
    Value *LHS = nullptr;
    Value *RHS = nullptr;
    ConstantInt *C = nullptr;

    // Matches if the operation is a binary operation
    // I've used this approach instead of the simpler inst.isBinaryOp() because
    // this allow to easily separate the constant and the value, avoiding further checks on the types
    if (
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_ConstantInt(C))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(C), PatternMatch::m_Value(LHS))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_Value(RHS)))
    ) {
        unsigned int opCode = inst.getOpcode();  // Operation code (Add, Sub, etc.)

        int64_t constantValue = 0;
        if (C) constantValue = C->getSExtValue();
        Value *newValue = nullptr;  // Will hold the simplified value if optimization applies

        std::string identity = "";  // Stores the description of the applied identity for verbose output

        std::vector<int> zeroConstantOps = {
            Instruction::Add,
            Instruction::Sub,
            Instruction::Shl,
            Instruction::LShr,
            Instruction::And,
            Instruction::Xor
        };

        std::vector<int> oneConstantOps = {
            Instruction::Mul,
            Instruction::UDiv,
            Instruction::SDiv,
        };

        if (
            (C && constantValue == 0 && opCode == Instruction::Mul) ||
            ((LHS == RHS) && (opCode == Instruction::Sub || opCode == Instruction::Xor))
        ) {
            newValue = Constant::getNullValue(inst.getType());
        } else if (
            (C && constantValue == 0 && std::find(zeroConstantOps.begin(), zeroConstantOps.end(), opCode) != zeroConstantOps.end()) ||
            (C && constantValue == 1 && std::find(oneConstantOps.begin(), oneConstantOps.end(), opCode) != oneConstantOps.end()) ||
            ((LHS == RHS) && (opCode == Instruction::And || opCode == Instruction::Or))
        ) {
            newValue = LHS;
        } else if ((LHS == RHS) && (opCode == Instruction::SDiv || opCode == Instruction::UDiv)) {
                newValue = ConstantInt::get(inst.getType(), 1);
        } else {
            return false;
        }

        if (LocalOptsVerbose) {
            std::map<int, std::string> constantIdentities {
                {Instruction::Add, "x + 0 = x"},
                {Instruction::Sub, "x - 0 = x"},
                {Instruction::Mul, "x * 0 = 0"},
                {Instruction::Shl, "x << 0 = x"},
                {Instruction::LShr, "x >> 0 = x"},
                {Instruction::Xor, "x ^ 0 = x"},
                {Instruction::And, "x & 0 = 0"},
                {Instruction::SDiv, "x / 1 = x"},
                {Instruction::UDiv, "x / 1 = x"}
            };

            std::map<int, std::string> nonConstantIdentities {
                {Instruction::Sub, "x - x = 0"},
                {Instruction::And, "x & x = x"},
                {Instruction::Or, "x | x = x"},
                {Instruction::Xor, "x ^ x = 0"},
                {Instruction::SDiv, "x / 1 = x"},
                {Instruction::UDiv, "x / 1 = x"}
            };

            if (C && C->getSExtValue() == 0 && opCode == Instruction::Mul) {
                identity = "x * 0 = 0";
            } else if (C) {
                identity = constantIdentities[opCode];
            } else {
                identity = nonConstantIdentities[opCode];
            }
        }

        // If we found an optimization, apply it
        if (newValue) {
            if (LocalOptsVerbose) {
                outs() << "Applying Algebraic identity optimization on instruction: " << inst << "\n";
                outs() << "The identity found was of the type: " << identity << "\n\n";
            }

            // Replace all uses of the original instruction with the new optimized value
            inst.replaceAllUsesWith(newValue);

            return true;
        }
    }

    return false;
}

/**
 * Apply strength reduction optimizations to convert expensive operations to cheaper ones.
 *
 * Strength reduction replaces computationally expensive operations with equivalent but
 * more efficient sequences. This optimization targets:
 *
 * 1. Multiplication by power of 2:
 *    - x * 2^n becomes x << n (left shift is faster than multiplication)
 *
 * 2. Other multiplications by constants:
 *    - Approximates multiplication using a combination of shift and subtraction
 *    - x * c becomes (x << ceil(log2(c))) - x for certain constants
 *
 * 3. Division by power of 2:
 *    - x / 2^n becomes x >> n (right shift is faster than division)
 *
 * These optimizations improve runtime performance by replacing
 * high-latency operations with simpler, faster alternatives.
 *
 * @param inst The instruction to be optimized
 * @return true if optimization was applied, false otherwise
 */
bool strengthReduction(Instruction &inst) {
    Value *V = nullptr;
    ConstantInt *C = nullptr;

    if (
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(V), PatternMatch::m_ConstantInt(C))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(C), PatternMatch::m_Value(V)))
    ) {
        unsigned int opCode = inst.getOpcode();

        Instruction *newInst = nullptr;
        int64_t constantValue = C->getSExtValue();

        std::string type = "";  // Stores the description of the transformation for verbose output

        // Handle multiplication operations
        if (opCode == Instruction::Mul) {
            int constantLog = ceil(log2(constantValue));

            if ((pow(2, constantLog) - constantValue) > 1) return false;

            type = "x * 2^n ==> x << n";
            Instruction *newInstShift = BinaryOperator::Create(
                Instruction::Shl, V, ConstantInt::get(inst.getType(), constantLog));

            newInstShift->insertAfter(&inst);

            if ((constantValue & (constantValue - 1)) != 0)  {
                type = "x * c ==> x << ceil(log2(c)) - x";

                newInst = BinaryOperator::Create(
                    Instruction::Sub, newInstShift, V);
                newInst->insertAfter(newInstShift);
            } else {
                newInst = newInstShift;
            }
        }
        // Handle division operations
        else if (opCode == Instruction::SDiv || opCode == Instruction::UDiv) {
            if ((constantValue & (constantValue - 1)) == 0) {
                // If divisor is a power of 2, replace division with right shift
                // x / 2^n => x >> n
                type = "x / 2^n ==> x >> n";
                newInst = BinaryOperator::Create(
                    Instruction::LShr, V, ConstantInt::get(inst.getType(), log2(constantValue)));
                newInst->insertAfter(&inst);
            }
        }

        // If optimization was applied, update all uses of the original instruction
        if (newInst) {
            if (LocalOptsVerbose) {
                outs() << "Applying Strength Reduction optimization on instruction: " << inst << "\n";
                outs() << "The transformation applied was: " << type << "\n\n";
            }

            inst.replaceAllUsesWith(newInst);

            return true;
        }
    }

    return false;
}

/**
 * Optimizes across multiple instructions by recognizing and eliminating inverse operations.
 *
 * This optimization identifies sequences of instructions where operations cancel each other out,
 * particularly focusing on patterns like:
 * - (x + c1) - c1 = x  (addition followed by subtraction of same constant)
 * - (x - c1) + c1 = x  (subtraction followed by addition of same constant)
 * - (x << c1) >> c1 = x  (left shift followed by right shift of same constant)
 * - (x >> c1) << c1 = x  (right shift followed by left shift of same constant)
 *
 * It is also able to the same patterns with negative constants, such as:
 * - (x + (-c1)) + c1 = x
 *
 * The optimization traverses the instruction graph looking for these patterns,
 * and when found, replaces the entire sequence (if it has no other uses) with the original value.
 *
 * More complex patterns involving multiple operations are also detected when possible,
 * such as ((x + 5) + 3) - 8 = x.
 *
 * Note: This optimization can detect inverse shift operations which correspond to multiplication and
 * division by powers of two. However, it cannot match cases where the value is multiplied and divided
 * by numbers that are not powers of 2, since these operations are typically transformed by strength
 * reduction optimization into different instruction sequences.
 *
 * @param inst The instruction to be optimized (usually the final operation in a sequence)
 * @param instructionsToRemove Vector containing the instruction to remove from the IR after optimizations
 * @return true if optimization was applied, false otherwise
 */
bool multiInstructionOptimization(Instruction &inst, std::vector<Instruction*> &instructionsToRemove) {
    Value *V = nullptr;
    ConstantInt *C = nullptr;

    if (
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(V), PatternMatch::m_ConstantInt(C))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(C), PatternMatch::m_Value(V)))
    ) {
        unsigned int opCode = inst.getOpcode();
        int64_t constantValue = C->getSExtValue();

        Instruction *varInst = dyn_cast<Instruction>(V);
        // Ensure the variable operand is also a binary instruction
        if (!varInst || !varInst->isBinaryOp()) {
            return false;
        }

        // Define pairs of inverse operations
        std::map<int, int> opMap {
            {Instruction::Add, Instruction::Sub},
            {Instruction::Sub, Instruction::Add},
            {Instruction::Shl, Instruction::LShr},
            {Instruction::LShr, Instruction::Shl},
            {Instruction::Mul, Instruction::SDiv},
            {Instruction::UDiv, Instruction::Mul},
            {Instruction::SDiv, Instruction::Mul}
        };

        std::queue<Instruction*> worklist;
        worklist.push(varInst);

        std::vector<Instruction*> specular_inst;
        specular_inst.push_back(varInst);

        Value* varV = nullptr;
        ConstantInt* varC = nullptr;
        bool canOptimize = false;

        while (!worklist.empty()) {
            varInst = worklist.front();
            worklist.pop();
            int varOpcode = varInst->getOpcode();

            if (
                (PatternMatch::match(varInst, PatternMatch::m_BinOp(PatternMatch::m_Value(varV), PatternMatch::m_ConstantInt(varC))) ||
                PatternMatch::match(varInst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(varC), PatternMatch::m_Value(varV)))) &&
                (opMap[opCode] == varOpcode || opCode == varOpcode)
            ) {
                bool areDiscordant = false;

                // This if checks whether the operations are of the same type but with negative constants
                // A further check is done to make sure that the operation is not a logical shift becuase
                // it is not allowed to have shifts with negative values
                if (varC->getSExtValue() * constantValue < 0 &&
                    (opCode == Instruction::Shl || opCode == Instruction::LShr)
                ) {
                    return false;
                } else if (varC->getSExtValue() * constantValue < 0 && opCode == varOpcode) {
                    areDiscordant = true;
                }

                if (LocalOptsVerbose) {
                    outs() << "Found potential inverse operation with constant: " << varC->getSExtValue() << "\n";
                }

                int64_t temp = 0;

                if (areDiscordant) {
                    temp = constantValue + varC->getSExtValue();
                } else {
                    temp = constantValue - varC->getSExtValue();
                }

                if (temp > 0) {
                    if (Instruction *vInst = dyn_cast<Instruction>(varV)) {
                        worklist.push(vInst);
                        specular_inst.push_back(vInst);
                        constantValue = temp;
                    }
                } else if (temp == 0) {
                    canOptimize = true;
                } else {
                    return false;
                }
            }
        }

        // Check if the current operation and the nested operation are inverse operations
        if (canOptimize) {
            if (LocalOptsVerbose) {
                outs() << "Applying Multi Instruction optimization on instruction: " << inst << "\n";
                outs() << "This is because, these instructions:\n";

                for (Instruction *inst : specular_inst) {
                    outs() << "\t";
                    inst->print(outs());
                    outs() << "\n";
                }

                outs() << "are inverse operations that cancel out with the current instruction\n\n";
            }

            for (Instruction *inst : specular_inst) {
                if (inst->hasNUses(1)) instructionsToRemove.push_back(inst);
            }

            // If so, we can simplify to just the original variable
            // For example: (x + 5) - 5 = x or (x * 2) / 2 = x
            inst.replaceAllUsesWith(varV);

            return true;
        }
    }

    return false;
}

/**
 * Apply all available optimizations to every instruction in a basic block.
 *
 * This function iterates through each instruction in the provided basic block and
 * tries to apply the following optimizations in sequence:
 *
 * 1. Algebraic Identity Optimizations - simplify based on mathematical rules
 * 2. Strength Reduction - replace expensive operations with cheaper ones
 * 3. Multi-Instruction Optimization - eliminate sequences of inverse operations
 *
 * When an optimization is successfully applied, the instruction is marked for removal
 * and will be deleted after all optimizations have been attempted.
 *
 * Note: This approach is more targeted than general Dead Code Elimination,
 * as it only removes instructions that our specific optimizations have replaced.
 *
 * @param BB The basic block to optimize
 * @return true if any optimization was applied to any instruction
 */
bool runOnBBOptimizations(BasicBlock &BB) {
    std::vector<Instruction*> instructionsToRemove;

    bool isChanged = false;

    for (Instruction &inst : BB) {
        if (
            algebraicIdentityOptimization(inst) ||
            strengthReduction(inst) ||
            multiInstructionOptimization(inst, instructionsToRemove)
        ) {
            instructionsToRemove.push_back(&inst);
            isChanged = true;
        }
    }

    /*
    Note on alternative approach:
        A full Dead Code Elimination (DCE) pass could be implemented as follows,
        but it's too aggressive for our targeted optimization purposes.
        We prefer to only remove instructions that our optimizations have explicitly handled.

    for (Instruction &inst : BB) {
        if (inst.isSafeToRemove()) {
            if (LocalOptsVerbose) {
                outs() << "Removing instruction: " << inst << "\n";
            }

            inst->eraseFromParent();
        };
    }
    */

    for (Instruction *inst : instructionsToRemove) {
        if (LocalOptsVerbose) {
            outs() << "Removing instruction: ";
            inst->print(outs());
            outs() << "\n";
        }

        inst->eraseFromParent();
    }

    return isChanged;
}

/**
 * Run all optimization passes on every basic block in a function.
 *
 * This function coordinates the optimization process at the function level,
 * iterating through each basic block in the function and applying the
 * optimization routines. It provides verbose output about the optimization
 * process when enabled.
 *
 * The function serves as an intermediate layer between module-level optimization
 * and basic block-level optimizations, allowing for function-specific processing
 * if needed in the future.
 *
 * @param F The LLVM Function to optimize
 * @return true if any basic block was transformed
 */
bool runOnFunction(Function &F) {
    bool Transformed = false;

    if (LocalOptsVerbose) {
        outs() << "--- " << "Function " << F.getName() << " OPTIMIZATIONS ---\n\n";
    }

    // Iterate over all basic blocks in the function
    for (auto Iter = F.begin(); Iter != F.end(); ++Iter) {
        // Apply optimizations to each basic block
        if (runOnBBOptimizations(*Iter)) {
            Transformed = true;
        }
    }

    if (LocalOptsVerbose) {
        outs() << "---------------------\n\n";
    }

    return Transformed;
}

/**
 * The main pass entry point - runs optimizations on the entire module.
 *
 * This method is called by the LLVM pass manager for each module being processed.
 * It orchestrates the optimization process by:
 * 1. Iterating through each function in the module
 * 2. Running all available optimizations on each function
 * 3. Tracking whether any changes were made to determine which analyses to preserve
 *
 *
 * @param M The LLVM Module to optimize
 * @param AM The module analysis manager providing analysis results
 * @return PreservedAnalyses indicating which analyses are preserved after optimization
 */
PreservedAnalyses LocalOpts::run(Module &M, ModuleAnalysisManager &AM) {
    bool transformed = false;

    // Run optimizations on each function in the module
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter)
        if (runOnFunction(*Fiter)) {
            transformed = true;
        }

    if (transformed)
        // If any function was modified, invalidate all analyses
        return PreservedAnalyses::none();

    // If no changes were made, all analyses are preserved
    return PreservedAnalyses::all();
}

/**
 * This function creates the necessary information structure for the LLVM pass manager
 * to recognize and register our local optimization pass.
 *
 * The registration callback allows the pass to be invoked via the command line
 * using the `-passes=local-opts` option with the LLVM opt tool.
 *
 * @return PassPluginLibraryInfo struct with complete plugin registration details
 */
PassPluginLibraryInfo getLocalOptsPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LocalOpts", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    // Allow the pass to be invoked via -passes=local-opts
                    if (Name == "local-opts") {
                        MPM.addPass(LocalOpts());
                        return true;
                    }
                    return false;
                });
        }};
}

/**
 * Plugin API entry point - Required by LLVM's plugin architecture.
 *
 * This function is the main entry point used by the LLVM opt tool when loading
 * this optimization pass as a plugin. It exposes the pass registration information
 * to the LLVM pass manager.
 *
 * The LLVM_ATTRIBUTE_WEAK annotation ensures proper symbol handling across
 * different build systems and platforms.
 *
 * @return PassPluginLibraryInfo containing all details needed to register this pass
 */
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLocalOptsPluginInfo();
}
