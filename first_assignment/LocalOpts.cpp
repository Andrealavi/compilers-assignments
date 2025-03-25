#include "LocalOpts.hpp"

using namespace llvm;

/**
 * Command-line option that enables verbose output for the optimizer.
 * When enabled, the pass will print detailed information about
 * each optimization that is applied.
 *
 * Use with `-local-opts-verbose` flag when running opt.
 */
static cl::opt<bool> LocalOptsVerbose(
    "local-opts-verbose",
    cl::desc(
        "Enables verbose output for the different optimizations"
    ),
    cl::init(false)
);

/**
 * Optimize instructions based on algebraic identities
 * Examples: x-x=0, x/x=1, x&x=x, x|x=x, x+0=x, x*0=0, etc.
 *
 * @param inst The instruction to be optimized
 * @return true if the instruction was optimized, false otherwise
 */
Instruction* algebraicIdentityOptimization(Instruction &inst) {
    Value *LHS = nullptr;
    Value *RHS = nullptr;
    ConstantInt *C = nullptr;

    // Only process binary operators
    if (
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_ConstantInt(C))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(C), PatternMatch::m_Value(LHS))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_Value(RHS)))
    ) {
        if (isa<Constant>(LHS)) {
            return nullptr;
        }

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
            return nullptr;
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
                {Instruction::SDiv - Instruction::UDiv, "x / 1 = x"}
            };

            std::map<int, std::string> nonConstantIdentities {
                {Instruction::Sub, "x - x = 0"},
                {Instruction::And, "x & x = x"},
                {Instruction::Or, "x | x = x"},
                {Instruction::Xor, "x ^ x = 0"},
                {Instruction::SDiv - Instruction::UDiv, "x / 1 = x"}
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

            return dyn_cast<Instruction>(newValue);
        }
    }

    return nullptr;
}

/**
 * Apply strength reduction optimizations to convert expensive operations to cheaper ones.
 * This transformation replaces costly operations with equivalent but more efficient
 * sequences of instructions.
 *
 * Examples:
 * - multiplication by power of 2 becomes left shift (x * 2^n => x << n)
 * - division by power of 2 becomes right shift (x / 2^n => x >> n)
 * - multiplication by constant approximated by shifts and subtraction
 *
 * @param inst The instruction to be optimized
 * @return true if optimization was applied, false otherwise
 */
Instruction* strengthReduction(Instruction &inst) {
    if (inst.isBinaryOp()) {
        Value *LHS = inst.getOperand(0);
        Value *RHS = inst.getOperand(1);

        unsigned int opCode = inst.getOpcode();

        // Get the constant operand, if any
        ConstantInt *constant = isa<ConstantInt>(LHS) ? dyn_cast<ConstantInt>(LHS) : dyn_cast<ConstantInt>(RHS);

        if (!constant) {
            return nullptr;  // No constant operand, can't apply strength reduction
        }

        // Get the variable operand
        Value *variable = (constant == LHS) ? RHS : LHS;

        Instruction *newInst = nullptr;

        int64_t constantValue = constant->getSExtValue();

        std::string type;  // Stores the description of the transformation for verbose output

        // Handle multiplication operations
        if (opCode == Instruction::Mul) {
            if ((constantValue & (constantValue - 1)) == 0) {
                // If constant is a power of 2, replace multiplication with left shift
                // x * 2^n => x << n
                type = "x * 2^n ==> x << n";  // Corrected from 2^2 to 2^n
                newInst = BinaryOperator::Create(
                    Instruction::Shl, variable, ConstantInt::get(inst.getType(), log2(constantValue)));

                newInst->insertAfter(&inst);
            } else {
                // For other constants, try to approximate using shifts and subtractions
                // x * c => (x << ceil(log2(c))) - x
                type = "x * c ==> x << ceil(log2(c)) - x";
                Instruction *newInstShift = BinaryOperator::Create(
                    Instruction::Shl, variable, ConstantInt::get(inst.getType(), ceil(log2(constantValue))));

                newInstShift->insertAfter(&inst);

                newInst = BinaryOperator::Create(
                    Instruction::Sub, newInstShift, variable);

                newInst->insertAfter(newInstShift);
            }
        }
        // Handle division operations
        else if (opCode == Instruction::SDiv || opCode == Instruction::UDiv) {
            if ((constantValue & (constantValue - 1)) == 0) {
                // If divisor is a power of 2, replace division with right shift
                // x / 2^n => x >> n
                type = "x / 2^n ==> x >> n";  // Added description string
                newInst = BinaryOperator::Create(
                    Instruction::LShr, variable, ConstantInt::get(inst.getType(), log2(constantValue)));
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
            // Note: The instruction is not actually removed
            // This would require: inst.eraseFromParent();

            return newInst;
        }
    }

    return nullptr;
}

/**
 * Optimizes across multiple instructions by recognizing patterns like:
 * (x op1 c) op2 c where op1 and op2 are inverse operations
 * For example: (x + 5) - 5 = x or (x * 2) / 2 = x
 *
 * This optimization identifies and eliminates pairs of operations that cancel each other out
 * when they involve the same constant value.
 *
 * @param inst The instruction to be optimized
 * @return true if optimization was applied, false otherwise
 */
Instruction* multiInstructionOptimization(Instruction &inst) {
    if (inst.isBinaryOp()) {
        Value* LHS = inst.getOperand(0);
        Value* RHS = inst.getOperand(1);
        unsigned int opCode = inst.getOpcode();

        // Find the constant operand, if any
        ConstantInt *constant = isa<ConstantInt>(LHS) ? dyn_cast<ConstantInt>(LHS) : dyn_cast<ConstantInt>(RHS);
        if (!constant) {
            return nullptr;  // No constant, can't proceed with this optimization
        }

        int64_t constantValue = constant->getZExtValue();

        // Identify the variable operand
        Value *variable = (constant == LHS) ? RHS : LHS;
        Instruction *varInst = dyn_cast<Instruction>(variable);

        // Ensure the variable operand is also a binary instruction
        if (!varInst || !varInst->isBinaryOp()) {
            return nullptr;
        }

        // Examine the operands of the nested instruction
        Value *varLHS = varInst->getOperand(0);
        Value *varRHS = varInst->getOperand(1);
        unsigned int varOpCode = varInst->getOpcode();

        // Find the constant operand in the nested instruction, if any
        ConstantInt *varConstant = isa<ConstantInt>(varLHS) ? dyn_cast<ConstantInt>(varLHS) : dyn_cast<ConstantInt>(varRHS);
        if (!varConstant) {
            return nullptr;  // No constant in nested instruction
        }

        int64_t varConstantValue = varConstant->getZExtValue();

        // Get the variable from the nested instruction
        Value *varVariable = (varConstant == varLHS) ? varRHS : varLHS;

        // Constants must be the same value for this optimization
        if (varConstantValue != constantValue) {
            return nullptr;
        }

        // Define pairs of inverse operations
        std::map<unsigned int, unsigned int> opMap;
        opMap[Instruction::Add] = Instruction::Sub;      // Addition and subtraction are inverses
        opMap[Instruction::Sub] = Instruction::Add;      // Subtraction and addition are inverses
        opMap[Instruction::Mul] = Instruction::SDiv;     // Multiplication and division are inverses
        opMap[Instruction::SDiv] = Instruction::Mul;     // Division and multiplication are inverses

        // Check if the current operation and the nested operation are inverse operations
        if (opMap[opCode] == varOpCode) {
            if (LocalOptsVerbose) {
                outs() << "Applying Multi Instruction optimization on instruction: " << inst << "\n";
                outs() << "This is because, this instruction '";
                varInst->print(outs());
                outs() << "' is specular to the modified instruction\n\n";
                // Fixed "specular" to "inverse" or similar term would be more accurate
            }

            // If so, we can simplify to just the original variable
            // For example: (x + 5) - 5 = x or (x * 2) / 2 = x
            inst.replaceAllUsesWith(varVariable);
            // Note: Instruction is not actually removed
            // This would require: inst.eraseFromParent();

            return dyn_cast<Instruction>(varVariable);
        }
    }

    return nullptr;
}

/**
 * Apply all optimizations to every instruction in a basic block.
 * Attempts to apply algebraic identity, strength reduction, and multi-instruction
 * optimizations to each instruction. When an optimization is applied, metadata
 * is attached to mark which optimization was used.
 *
 * @param BB The basic block to optimize
 * @return true if any optimization was applied
 */
bool runOnBBOptimizations(BasicBlock &BB) {
    MDNode *MD;
    LLVMContext &context = BB.getContext();

    std::vector<Instruction*> instructionToRemove;

    bool isChanged = false;

    for (Instruction &inst : BB) {
        if (Instruction* newInst = algebraicIdentityOptimization(inst)) { // Try to apply algebraic identity optimization
            // Add metadata to mark this instruction as optimized with algebraic identity
            MD = MDNode::get(
                context,
                MDString::get(
                    context,
                    "Applied an algebraic identity optimization")
            );

            newInst->setMetadata("algebraic", MD);

            instructionToRemove.push_back(&inst);
            isChanged = true;
        } else if (Instruction* newInst = strengthReduction(inst)) { // Try to apply strength reduction optimization
            // Add metadata to mark this instruction as optimized with strength reduction
            MD = MDNode::get(
                context,
                MDString::get(
                    context,
                    "Applied a strength reduction optimization")
            );

            newInst->setMetadata("strength", MD);

            instructionToRemove.push_back(&inst);
            isChanged = true;
        } else if (Instruction* newInst = multiInstructionOptimization(inst)) { // Try to apply multi-instruction optimization
            // Add metadata to mark this instruction as optimized with multi-instruction opt
            MD = MDNode::get(
                context,
                MDString::get(
                    context,
                    "Applied a multi instruction optimization")
            );

            newInst->setMetadata("multi-instruction", MD);

            instructionToRemove.push_back(&inst);
            isChanged = true;
        }
    }

    for (Instruction *inst : instructionToRemove) {
        inst->eraseFromParent();
    }

    return isChanged;
}

/**
 * Run all optimizations on every basic block in a function.
 * Iterates through each basic block and applies the optimization
 * functions, with verbose output if enabled.
 *
 * @param F The function to optimize
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
 * This is called by the LLVM pass manager for each module being processed.
 * It orchestrates running the optimizations on each function in the module.
 *
 * @param M The module to optimize
 * @param AM The module analysis manager
 * @return PreservedAnalyses indicating which analyses are preserved
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
 * Get information about this pass plugin.
 * This creates the necessary information structure for the LLVM pass manager
 * to recognize and register our optimization pass.
 *
 * @return PassPluginLibraryInfo struct with plugin details
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
 * Plugin API entry point - allows opt to recognize the pass
 * when used with -passes=local-opts command line option.
 *
 * This is called by LLVM when loading the pass to get information
 * about it and register it in the pass pipeline.
 *
 * @return PassPluginLibraryInfo for this pass
 */
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLocalOptsPluginInfo();
}
