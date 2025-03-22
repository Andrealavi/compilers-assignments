#include "LocalOpts.h"

using namespace llvm;

/**
 * Runs a demonstration on a basic block to show how to access and manipulate LLVM IR instructions
 * @param B Reference to the basic block to operate on
 * @return true if the basic block was modified
 */
bool runOnBasicBlock(BasicBlock &B) {

    // Get the first two instructions of the basic block
    Instruction &Inst1st = *B.begin(), &Inst2nd = *(++B.begin());

    // Verify that the first instruction is the same as the first operand of the second instruction
    // This is a specific assumption for this example
    assert(&Inst1st == Inst2nd.getOperand(0));

    // Print the first instruction to the standard output
    outs() << "PRIMA ISTRUZIONE: " << Inst1st << "\n";
    // Print the first instruction as an operand format
    outs() << "COME OPERANDO: ";
    Inst1st.printAsOperand(outs(), false);
    outs() << "\n";

    // Demonstrate the User-Use-Value relationship by examining the operands
    outs() << "I MIEI OPERANDI SONO:\n";
    for (auto *Iter = Inst1st.op_begin(); Iter != Inst1st.op_end(); ++Iter) {
        Value *Operand = *Iter;

        // Check if the operand is a function argument and print its details
        if (Argument *Arg = dyn_cast<Argument>(Operand)) {
            outs() << "\t" << *Arg << ": SONO L'ARGOMENTO N. " << Arg->getArgNo()
             <<" DELLA FUNZIONE " << Arg->getParent()->getName()
                            << "\n";
        }
        // Check if the operand is a constant integer and print its value
        if (ConstantInt *C = dyn_cast<ConstantInt>(Operand)) {
            outs() << "\t" << *C << ": SONO UNA COSTANTE INTERA DI VALORE " << C->getValue()
                            << "\n";
        }
    }

    // Print all instructions that use the first instruction as an operand (users)
    outs() << "LA LISTA DEI MIEI USERS:\n";
    for (auto Iter = Inst1st.user_begin(); Iter != Inst1st.user_end(); ++Iter) {
        outs() << "\t" << *(dyn_cast<Instruction>(*Iter)) << "\n";
    }

    // Print all uses of the first instruction (which gives the same result as users in this context)
    outs() << "E DEI MIEI USI (CHE E' LA STESSA):\n";
    for (auto Iter = Inst1st.use_begin(); Iter != Inst1st.use_end(); ++Iter) {
        outs() << "\t" << *(dyn_cast<Instruction>(Iter->getUser())) << "\n";
    }

    // Demonstrate instruction creation and modification
    // Create a new Add instruction that adds the first operand of Inst1st to itself
    Instruction *NewInst = BinaryOperator::Create(
            Instruction::Add, Inst1st.getOperand(0), Inst1st.getOperand(0));

    // Insert the new instruction after Inst1st
    NewInst->insertAfter(&Inst1st);
    // Replace all uses of Inst1st with NewInst
    // This updates all instructions that used Inst1st to now use NewInst
    Inst1st.replaceAllUsesWith(NewInst);

    return true;
}

/**
 * Optimize instructions based on algebraic identities
 * Examples: x-x=0, x/x=1, x&x=x, x|x=x, x+0=x, x*0=0, etc.
 *
 * @param Inst The instruction to be optimized
 * @return true if the instruction was optimized, false otherwise
 */
bool algebraicIdentityOptimization(Instruction &Inst) {
    // Only process binary operators
    if (Inst.isBinaryOp()) {
        Value *LHS = Inst.getOperand(0);  // Left-hand side operand
        Value *RHS = Inst.getOperand(1);  // Right-hand side operand

        unsigned int opCode = Inst.getOpcode();  // Operation code (Add, Sub, etc.)

        Value *newValue = nullptr;  // Will hold the simplified value if optimization applies

        // Case 1: Operands are the same (x op x)
        if (LHS == RHS) {
            switch (opCode) {
                case Instruction::Sub:  // x - x = 0
                    newValue = Constant::getNullValue(Inst.getType());
                break;

                case Instruction::SDiv:  // x / x = 1 (signed division)
                    newValue = ConstantInt::get(Inst.getType(), 1);
                break;

                case Instruction::UDiv:  // x / x = 1 (unsigned division)
                    newValue = ConstantInt::get(Inst.getType(), 1);
                break;

                case Instruction::And:  // x & x = x
                    newValue = LHS;
                break;

                case Instruction::Or:  // x | x = x
                    newValue = LHS;
                break;

                default:
                break;
            }
        }
        // Case 2: One operand is a constant
        else if (ConstantInt *constant = isa<ConstantInt>(LHS) ? dyn_cast<ConstantInt>(LHS) : isa<ConstantInt>(RHS) ? dyn_cast<ConstantInt>(RHS) : nullptr) {
            if (!constant) {
                return false;
            }

            // Determine which operand is the variable
            Value *variable = constant == LHS ? RHS : LHS;

            int64_t constantValue = constant->getSExtValue();

            // Special case for constant value 0
            if (constantValue == 0) {
                switch (opCode) {
                    case Instruction::Add:  // x + 0 = x
                        newValue = variable;
                    break;

                    case Instruction::Sub:  // x - 0 = x or 0 - x = -x
                        newValue = variable;  // Note: this is correct only if constant is RHS
                    break;

                    case Instruction::Mul:  // x * 0 = 0
                        newValue = Constant::getNullValue(Inst.getType());
                    break;

                    case Instruction::Shl:  // x << 0 = x
                        newValue = variable;
                    break;

                    case Instruction::LShr:  // x >> 0 = x
                        newValue = variable;
                    break;

                    default:
                    break;
                }
            }
            // Special case for constant value 1
            else if (constantValue == 1) {
                switch (opCode) {
                    case Instruction::Mul:  // x * 1 = x
                        newValue = variable;
                    break;

                    case Instruction::UDiv:  // x / 1 = x (unsigned)
                        newValue = variable;
                    break;

                    case Instruction::SDiv:  // x / 1 = x (signed)
                        newValue = variable;
                    break;

                    default:
                    break;
                }
            }
        }

        // If we found an optimization, apply it
        if (newValue) {
            // Replace all uses of the original instruction with the new optimized value
            Inst.replaceAllUsesWith(newValue);

            // Note: The instruction is not actually removed from the parent
            // This would require: Inst.eraseFromParent();

            return true;
        }
    }

    return false;
}

/**
 * Apply strength reduction optimizations to convert expensive operations to cheaper ones
 * Examples: multiplication by power of 2 becomes left shift, division by power of 2 becomes right shift
 *
 * @param inst The instruction to be optimized
 * @return true if optimization was applied, false otherwise
 */
bool strengthReduction(Instruction &inst) {
    if (inst.isBinaryOp()) {
        Value *LHS = inst.getOperand(0);
        Value *RHS = inst.getOperand(1);

        unsigned int opCode = inst.getOpcode();

        // Get the constant operand, if any
        ConstantInt *constant = isa<ConstantInt>(LHS) ? dyn_cast<ConstantInt>(LHS) : dyn_cast<ConstantInt>(RHS);

        if (!constant) {
            return false;  // No constant operand, can't apply strength reduction
        }

        // Get the variable operand
        Value *variable = (constant == LHS) ? RHS : LHS;

        Instruction *newInst = nullptr;

        int64_t constantValue = constant->getSExtValue();

        // Handle multiplication operations
        if (opCode == Instruction::Mul) {
            if ((constantValue & (constantValue - 1)) == 0) {
                // If constant is a power of 2, replace multiplication with left shift
                // x * 2^n => x << n
                newInst = BinaryOperator::Create(
                    Instruction::Shl, variable, ConstantInt::get(inst.getType(), log2(constantValue)));

                newInst->insertAfter(&inst);
            } else {
                // For other constants, try to approximate using shifts and subtractions
                // x * c => (x << ceil(log2(c))) - x
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
                newInst = BinaryOperator::Create(
                    Instruction::LShr, variable, ConstantInt::get(inst.getType(), log2(constantValue)));
                newInst->insertAfter(&inst);
            }
        }

        // If optimization was applied, update all uses of the original instruction
        if (newInst) {
            inst.replaceAllUsesWith(newInst);
            // Note: The instruction is not actually removed
            // This would require: inst.eraseFromParent();

            return true;
        }
    }

    return false;
}

/**
 * Optimizes across multiple instructions by recognizing patterns like:
 * (x op1 c) op2 c where op1 and op2 are inverse operations
 * For example: (x + 5) - 5 = x
 *
 * @param Inst The instruction to be optimized
 * @return true if optimization was applied, false otherwise
 */
bool multiInstructionOptimization(Instruction &Inst) {
    if (Inst.isBinaryOp()) {
        Value* LHS = Inst.getOperand(0);
        Value* RHS = Inst.getOperand(1);
        unsigned int opCode = Inst.getOpcode();

        // Find the constant operand, if any
        ConstantInt *constant = isa<ConstantInt>(LHS) ? dyn_cast<ConstantInt>(LHS) : dyn_cast<ConstantInt>(RHS);
        if (!constant) {
            return false;  // No constant, can't proceed with this optimization
        }

        int64_t constantValue = constant->getZExtValue();

        // Identify the variable operand
        Value *variable = (constant == LHS) ? RHS : LHS;
        Instruction *varInst = dyn_cast<Instruction>(variable);

        // Ensure the variable operand is also a binary instruction
        if (!varInst || !varInst->isBinaryOp()) {
            return false;
        }

        // Examine the operands of the nested instruction
        Value *varLHS = varInst->getOperand(0);
        Value *varRHS = varInst->getOperand(1);
        unsigned int varOpCode = varInst->getOpcode();

        // Find the constant operand in the nested instruction, if any
        ConstantInt *varConstant = isa<ConstantInt>(varLHS) ? dyn_cast<ConstantInt>(varLHS) : dyn_cast<ConstantInt>(varRHS);
        if (!varConstant) {
            return false;  // No constant in nested instruction
        }

        int64_t varConstantValue = varConstant->getZExtValue();

        // Get the variable from the nested instruction
        Value *varVariable = (varConstant == varLHS) ? varRHS : varLHS;

        // Constants must be the same value for this optimization
        if (varConstantValue != constantValue) {
            return false;
        }

        // Define pairs of inverse operations
        std::map<unsigned int, unsigned int> opMap;
        opMap[Instruction::Add] = Instruction::Sub;      // Addition and subtraction are inverses
        opMap[Instruction::Sub] = Instruction::Add;      // Subtraction and addition are inverses
        opMap[Instruction::Mul] = Instruction::SDiv;     // Multiplication and division are inverses
        opMap[Instruction::SDiv] = Instruction::Mul;     // Division and multiplication are inverses

        // Check if the current operation and the nested operation are inverse operations
        if (opMap[opCode] == varOpCode) {
            // If so, we can simplify to just the original variable
            // For example: (x + 5) - 5 = x or (x * 2) / 2 = x
            Inst.replaceAllUsesWith(varVariable);
            // Note: Instruction is not actually removed
            // This would require: Inst.eraseFromParent();

            return true;
        }
    }

    return false;
}

/**
 * Apply all optimizations to every instruction in a basic block
 * Also adds metadata to mark which optimizations were applied
 *
 * @param BB The basic block to optimize
 * @return true if any optimization was applied
 */
bool runOnBBOptimizations(BasicBlock &BB) {
    MDNode *MD;
    LLVMContext &context = BB.getContext();

    for (Instruction &Inst : BB) {
        // Try to apply algebraic identity optimization
        if (algebraicIdentityOptimization(Inst)) {
            // Add metadata to mark this instruction as optimized with algebraic identity
            MD = MDNode::get(
                context,
                MDString::get(
                    context,
                    "Applied an algebraic identity optimization")
            );

            Inst.setMetadata("algebraic", MD);
        }

        // Try to apply strength reduction optimization
        if (strengthReduction(Inst)) {
            // Add metadata to mark this instruction as optimized with strength reduction
            MD = MDNode::get(
                context,
                MDString::get(
                    context,
                    "Applied a strength reduction optimization")
            );

            Inst.setMetadata("strength", MD);
        }

        // Try to apply multi-instruction optimization
        if (multiInstructionOptimization(Inst)) {
            // Add metadata to mark this instruction as optimized with multi-instruction opt
            MD = MDNode::get(
                context,
                MDString::get(
                    context,
                    "Applied a multi instruction optimization")
            );

            Inst.setMetadata("strength", MD);
        }
    }

    return true;
}

/**
 * Run all optimizations on every basic block in a function
 *
 * @param F The function to optimize
 * @return true if any basic block was transformed
 */
bool runOnFunction(Function &F) {
    bool Transformed = false;

    // Iterate over all basic blocks in the function
    for (auto Iter = F.begin(); Iter != F.end(); ++Iter) {
        // Apply optimizations to each basic block
        if (runOnBBOptimizations(*Iter)) {
            Transformed = true;
        }
    }

    return Transformed;
}

/**
 * The main pass entry point - runs optimizations on the entire module
 *
 * @param M The module to optimize
 * @param AM The module analysis manager
 * @return PreservedAnalyses indicating which analyses are preserved
 */
PreservedAnalyses LocalOpts::run(Module &M, ModuleAnalysisManager &AM) {
    // Run optimizations on each function in the module
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter)
        if (runOnFunction(*Fiter))
            // If any function was modified, invalidate all analyses
            return PreservedAnalyses::none();

    // If no changes were made, all analyses are preserved
    return PreservedAnalyses::all();
}

/**
 * Get information about this pass plugin
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
 * when used with -passes=local-opts command line option
 *
 * @return PassPluginLibraryInfo for this pass
 */
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLocalOptsPluginInfo();
}
