#include "LocalOpts.hpp"

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
 * Converts an integer to a set of bit positions where '1's appear in its binary representation.
 *
 * For example, for n=10 (binary: 1010), the function returns {1,3} because bits at
 * positions 1 and 3 are set.
 *
 * This helper function is used during strength reduction to decompose constant
 * multipliers into their power-of-2 components.
 *
 * @param n The integer to analyze
 * @return A set containing the bit positions of all set bits in n
 */
std::set<int> getExpSet(int n) {
    std::set<int> expSet;

    int nBits = floor(log2(n)) + 1;

    int res = n;

    for (int i = 0; i < nBits; i++) {
        if (res % 2 == 1) {
            expSet.insert(i);
        }

        res /= 2;
    }

    return expSet;
};

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
 * Implementation details:
 * - Uses LLVM's pattern matchers to identify binary operations with constants
 * - Handles both cases where constant is on left or right side
 * - Provides verbose output showing the identity that was applied
 * - Replaces the original instruction with the simplified value
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

        static const std::vector<int> zeroConstantOps = {
            Instruction::Add,
            Instruction::Sub,
            Instruction::Shl,
            Instruction::LShr,
            Instruction::And,
            Instruction::Xor,
            Instruction::Or,
        };

        static const std::vector<int> oneConstantOps = {
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
            !(C && opCode == Instruction::Sub && C == inst.getOperand(0)) && // if the instruction is of the type y = 0 - x it should not be applied
            ((C && (constantValue == 0 || constantValue == -1) && std::find(zeroConstantOps.begin(), zeroConstantOps.end(), opCode) != zeroConstantOps.end()) ||
            (C && constantValue == 1 && std::find(oneConstantOps.begin(), oneConstantOps.end(), opCode) != oneConstantOps.end()) ||
            ((LHS == RHS) && (opCode == Instruction::And || opCode == Instruction::Or)))
        ) {
            newValue = LHS;
        } else if ((LHS == RHS) && (opCode == Instruction::SDiv || opCode == Instruction::UDiv)) {
                newValue = ConstantInt::get(inst.getType(), 1);
        } else {
            return false;
        }

        if (LocalOptsVerbose) {
            static const std::map<int, std::string> constantIdentities {
                {Instruction::Add, "x + 0 = x"},
                {Instruction::Sub, "x - 0 = x"},
                {Instruction::Mul, "x * 1 = x"},
                {Instruction::Shl, "x << 0 = x"},
                {Instruction::LShr, "x >> 0 = x"},
                {Instruction::Xor, "x ^ 0 = x"},
                {Instruction::And, "x & 0 = 0"},
                {Instruction::SDiv, "x / 1 = x"},
                {Instruction::UDiv, "x / 1 = x"}
            };

            static const std::map<int, std::string> nonConstantIdentities {
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
                identity = constantIdentities.at(opCode);
            } else {
                identity = nonConstantIdentities.at(opCode);
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
 * 2. Multiplication by specific patterns:
 *    - For constants like 2^n-1 (e.g., 7, 15): x * c becomes (x << n) - x
 *    - For constants with few set bits: decomposes multiplication into a series of shifts and adds
 *      (e.g., x * 10 becomes (x << 3) + (x << 1) since 10 = 2^3 + 2^1)
 *
 * 3. Division by power of 2:
 *    - x / 2^n becomes x >> n (right shift is faster than division)
 *
 * Implementation details:
 * - Uses getExpSet() to decompose constants into their binary components
 * - For constants with all bits set (2^n-1), applies the (x << n) - x optimization
 * - For constants with few set bits (<=3), creates a sequence of shift and add operations
 * - Handles negative constants by negating the final result
 * - Each new instruction is inserted in the proper position in the IR
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

        bool isNegative = constantValue < 0;

        if (isNegative) {
            constantValue *= -1;
        }

        std::string type = "";  // Stores the description of the transformation for verbose output

        // Handle multiplication operations
        // The number is converted to binary and a set with the exponents to use is created.
        // If the constant has a binary is equal to 2^(n-1)
        // the optimization x << c - x is applied
        // Otherwise the strength reduction is applied by converting the multiplication into a sum of different power of two.
        if (opCode == Instruction::Mul) {
            std::set<int> expSet = getExpSet(constantValue);
            int nBits = floor(log2(constantValue)) + 1;

            Instruction *prevInst = &inst;

            if (expSet.size() == nBits) {
                type = "(x << c) - x";
                Instruction *newInstShift = BinaryOperator::Create(
                    Instruction::Shl, V, ConstantInt::get(Type::getInt32Ty(inst.getParent()->getContext()), nBits));

                newInstShift->insertAfter(prevInst);

                Instruction *newInstSub = BinaryOperator::CreateNSWSub(
                    newInstShift, V);

                newInstSub->insertAfter(newInstShift);

                newInst = newInstSub;
            } else if (expSet.size() < 4) {
                // I've decided to not apply strength reduction optimization if the number of shifts to add is more than 3.
                // Even though the mul instruction is a multi-cycle instruction, replacing it with multiple shifts and adds can
                // become more expensive than the mul itself.
                // My choice is arbitrary and it is based on simple web researchs, therefore
                // I don't consider it as the perfect solution and it, perharps, could be improved; however I consider it a good trade-off

                type = "x * c = x * (2^c1 + 2^c2 ...) ==> x << c1 + x << c2 ...";
                for (int exp : expSet) {
                    Instruction *newInstShift = BinaryOperator::Create(
                        Instruction::Shl, V, ConstantInt::get(Type::getInt32Ty(inst.getParent()->getContext()), exp));

                    newInstShift->insertAfter(prevInst);

                    if (expSet.size() > 1 && prevInst != &inst) {
                        Instruction *newInstAdd = nullptr;

                        newInstAdd = BinaryOperator::CreateNSWAdd(
                            newInstShift, prevInst);

                        newInstAdd->insertAfter(newInstShift);
                        prevInst = newInstAdd;
                    } else {
                        prevInst = newInstShift;
                    }
                }

                newInst = prevInst;
            } else {
                return false;
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

        if (isNegative) {
            Instruction *negInst = BinaryOperator::CreateNSWSub(ConstantInt::get(inst.getType(), 0), newInst, "neg");
            negInst->insertAfter(newInst);
            newInst = negInst;
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
 * with a focus on patterns like:
 * - (x + c1) - c1 = x  (addition followed by subtraction of same constant)
 * - (x - c1) + c1 = x  (subtraction followed by addition of same constant)
 * - (x << c1) >> c1 = x  (left shift followed by right shift of same constant)
 * - (x >> c1) << c1 = x  (right shift followed by left shift of same constant)
 * - (x * c1) / c1 = x  (multiplication followed by division with same constant)
 * - (x / c1) * c1 = x  (division followed by multiplication with same constant)
 *
 * Implementation algorithm:
 * 1. Start with a binary operation (e.g., sub, add) with a constant operand
 * 2. Check if its variable operand is also a binary operation
 * 3. Use a worklist to traverse a chain of operations, building a list of specular instructions
 * 4. Track accumulating constant values to detect when operations cancel out
 * 5. For add/sub: check if constants sum to zero
 * 6. For mul/div: check if constants divide to 1
 * 7. For shift operations: check if shift amounts cancel out
 *
 * Complex patterns like ((x + 5) + 3) - 8 = x are handled by recursively analyzing the
 * instruction chain.
 *
 * When a cancellation pattern is found, the entire chain is replaced with the original
 * variable, and unused instructions are marked for removal.
 *
 * @param inst The instruction to be optimized (usually the final operation in a sequence)
 * @param instructionsToRemove Vector storing instructions to be removed after optimization
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
        static const std::map<int, int> opMap {
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
                (opMap.at(opCode) == varOpcode || opCode == varOpcode)
            ) {
                bool areDiscordant = false;

                // This if checks whether the operations are of the same type but with negative constants
                // A further check is done to make sure that the operation is not a logical shift because
                // it is not allowed to have shifts with negative values
                if (
                    constantValue * varC->getSExtValue() < 0 &&
                    opCode == varOpcode &&
                    (opCode != Instruction::Add || opCode != Instruction::Sub)
                ) {
                    areDiscordant = true;
                } else if (opCode == varOpcode) {
                    return false;
                }

                if (LocalOptsVerbose) {
                    outs() << "Found potential inverse operation with constant: " << varC->getSExtValue() << "\n";
                }

                int64_t temp = 0;

                if (areDiscordant) {
                    temp = constantValue + varC->getSExtValue();
                } else {
                    if (
                        opCode == Instruction::Mul ||
                        opCode == Instruction::UDiv ||
                        opCode == Instruction::SDiv
                    ) {
                        temp = constantValue / varC->getSExtValue();
                    } else {
                        temp = constantValue - varC->getSExtValue();
                    }
                }

                if (
                    temp == 0 ||
                    (temp == 1 && (opCode == Instruction::Mul || opCode == Instruction::UDiv || opCode == Instruction::SDiv))
                ) {
                    canOptimize = true;
                } else if (temp > 0) {
                    if (Instruction *vInst = dyn_cast<Instruction>(varV)) {
                        worklist.push(vInst);
                        specular_inst.push_back(vInst);
                        constantValue = temp;
                    }
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
 * Implementation details:
 * - Skips floating point operations (only optimizes integer operations)
 * - Collects instructions that have been replaced by optimizations
 * - Removes the optimized instructions after all transformations are complete
 * - Provides verbose output about removed instructions when enabled
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
            !inst.getType()->isFloatingPointTy() && // Optimizations are for integer operations only
            (algebraicIdentityOptimization(inst) ||
            strengthReduction(inst) ||
            multiInstructionOptimization(inst, instructionsToRemove))
        ) {
            instructionsToRemove.push_back(&inst);
            isChanged = true;
        }
    }

    /*
    Note on alternative approach:
        A full Dead Code Elimination (DCE) pass could be implemented as follows,
        but it's too aggressive for our targeted optimization purposes.
        I prefer to only remove instructions that our optimizations have explicitly handled.

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
 * Following LLVM's pass manager requirements, it returns information about which
 * analysis results remain valid after the transformations.
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
