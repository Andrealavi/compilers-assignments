#include "veryBusyExpressions.hpp"

using namespace llvm;

/**
    Given an operand it returns the pointer to the memory location
    where the operand took its value
    If the operand is not related to any pointer, nullptr is returned
*/
Value *getLoadPointer(Value *operand) {
    if (LoadInst *LI = dyn_cast<LoadInst>(operand)) return LI->getPointerOperand();

    return nullptr;
}

/**
    Checks whether the value is changed before the load instruction
*/
bool isChanged(LoadInst *loadInst) {
    Function *F = loadInst->getFunction();
    Value *ptr = loadInst->getPointerOperand();

    MemorySSAWrapperPass MSSAWP;
    MSSAWP.runOnFunction(*F);
    MemorySSA &MSSA = MSSAWP.getMSSA();

    MemoryAccess *loadAccess = MSSA.getMemoryAccess(loadInst);
    if (!loadAccess) return false;

    MemoryAccess *defAccess = MSSA.getWalker()->getClobberingMemoryAccess(loadAccess);

    if (MemoryDef *memDef = dyn_cast<MemoryDef>(defAccess)) {
        Instruction *defInst = memDef->getMemoryInst();

        if (StoreInst *SI = dyn_cast<StoreInst>(defInst)) {
            if (SI->getPointerOperand() == ptr) return true;
        }
    }


    return false;
}

/**
    Checks whether the pointers used in the instructions are the same
*/
bool checkPointers(Instruction &inst1, Instruction &inst2) {
    Value *LHS1 = inst1.getOperand(0);
    Value *LHS2 = inst2.getOperand(0);

    Value *RHS1 = inst1.getOperand(1);
    Value *RHS2 = inst2.getOperand(1);

    Value *lhsPtr1 = getLoadPointer(LHS1);
    Value *rhsPtr1 = getLoadPointer(RHS1);

    Value *lhsPtr2 = getLoadPointer(LHS2);
    Value *rhsPtr2 = getLoadPointer(RHS2);

    if (lhsPtr1 && lhsPtr2 && !rhsPtr1 && !rhsPtr2) {
        if (lhsPtr1 == lhsPtr2) return true;
    } else if (!lhsPtr1 && !lhsPtr2 && rhsPtr1 && rhsPtr2) {
        if (rhsPtr1 == rhsPtr2) return true;
    } else if (lhsPtr1 && lhsPtr2 && rhsPtr1 && rhsPtr2) {
        if (lhsPtr1 == lhsPtr2 && rhsPtr1 == rhsPtr2) return true;
        else if (inst1.isCommutative() && lhsPtr1 == rhsPtr2 && lhsPtr2 == rhsPtr1) return true;
    }

    return false;
}

/**
    Checks whether the operands in the instructions are the same
*/
bool checkOperands(Instruction &inst1, Instruction &inst2) {
    Value *LHS1 = inst1.getOperand(0);
    Value *LHS2 = inst2.getOperand(0);

    Value *RHS1 = inst1.getOperand(1);
    Value *RHS2 = inst2.getOperand(1);

    if (LHS1 == LHS2 && RHS1 == RHS2) return true;
    else if (inst1.isCommutative() && LHS1 == RHS2 && LHS2 == RHS1) return true;

    return false;
}

/**
    Checks whether the instructions are equal
*/
bool areEqual(Instruction &inst1, Instruction &inst2) {
    if (!inst1.isBinaryOp() || !inst2.isBinaryOp()) return false;
    if (inst1.getOpcode() != inst2.getOpcode()) return false;

    if (checkOperands(inst1, inst2)) return true;
    if (checkPointers(inst1, inst2)) return true;


    return false;
}

/**
    Computes the intersection between the successors sets
*/
std::set<Instruction*> intersectSets(BasicBlock &BB, std::map<BasicBlock*, std::set<Instruction*>> &busyInsts) {
    std::set<Instruction*> res;
    bool isFirst = true;

    for (BasicBlock *succ : successors(&BB)) {
        if (busyInsts.find(succ) == busyInsts.end()) continue;

        if (isFirst) {
            res = busyInsts[succ];
            isFirst = false;
        } else {
            std::set<Instruction*> temp;

            for (Instruction *rInst : res) {
                for (Instruction *sInst : busyInsts[succ]) {
                    if (areEqual(*rInst, *sInst)) temp.insert(rInst);
                }
            }

            res = temp;
        }
    }

    return res;
}

/**
    Removes instructions killed by the given instruction
*/
void removeKilled(Instruction &inst, std::set<Instruction*> &blockBusyInsts) {
    std::vector<Instruction*> instsToRemove;

    for (Instruction *bInst : blockBusyInsts) {
        if (bInst->isBinaryOp() && areEqual(inst, *bInst)) instsToRemove.push_back(bInst);
    }

    for (Instruction *rInst : instsToRemove) {
        blockBusyInsts.erase(rInst);
    }
}

/**
    Computes the very busy instructions for the given basic block
*/
bool getVeryBusyInsts(BasicBlock &BB, std::map<BasicBlock*, std::set<Instruction*>> &busyInsts) {
    std::set<Instruction*> blockBusyInsts = intersectSets(BB, busyInsts);

    for (Instruction &inst : BB) {
        removeKilled(inst, blockBusyInsts);
        if (inst.isBinaryOp()) blockBusyInsts.insert(&inst);
    }

    if (blockBusyInsts != busyInsts[&BB]) {
        busyInsts[&BB] = blockBusyInsts;

        return true;
    }

    return false;
}

/**
    Computes very busy expressions for the given function
*/
bool veryBusyExpressions(Function &F,  std::map<BasicBlock*, std::set<Instruction*>> &busyInsts) {
    bool isChanged = false;

    for (BasicBlock &BB : reverse(F)) {
        //if (BB == F.)
        if (getVeryBusyInsts(BB, busyInsts)) isChanged = true;
    }

    return isChanged;
}

PreservedAnalyses VeryBusyExpressions::run(Module &M, ModuleAnalysisManager &AM) {
    // Run optimizations on each function in the module
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter) {
        std::map<BasicBlock*, std::set<Instruction*>> busyInsts;
        while (veryBusyExpressions(*Fiter, busyInsts)) {};

        outs() << "Dominators for function: " << Fiter->getName();
        outs() << "\n\n";

        for (auto &pair : busyInsts) {
            outs() << "Very Busy Expressions for basic block: " << pair.first->getName();
            outs() << "\n";

            for (auto &busy : pair.second) {
                busy->print(outs());
                outs() << "\n";
            }
        }

        outs() << "------------------\n\n";
    }

    return PreservedAnalyses::all();
}


PassPluginLibraryInfo getVeryBusyExpressionsPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Dominator Analysis", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    if (Name == "very-busy") {
                        MPM.addPass(VeryBusyExpressions());
                        return true;
                    }
                    return false;
                });
        }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getVeryBusyExpressionsPluginInfo();
}
