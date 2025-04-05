#include "constantPropagation.hpp"
#include <llvm-19/llvm/Analysis/LoopInfo.h>
#include <llvm-19/llvm/IR/BasicBlock.h>
#include <llvm-19/llvm/IR/CFG.h>
#include <llvm-19/llvm/IR/Constant.h>
#include <llvm-19/llvm/IR/Constants.h>
#include <llvm-19/llvm/IR/Instruction.h>
#include <llvm-19/llvm/IR/Instructions.h>
#include <llvm-19/llvm/Support/Casting.h>

#define INFINITY 2147483647

using namespace llvm;

std::map<Value*, int> computeIntersection(BasicBlock &BB, std::map<BasicBlock*, std::map<Value*, int>> &blocksConstants) {
    bool isFirst = true;
    std::map<Value*, int> res;

    for (BasicBlock *pred : predecessors(&BB)) {
        if (blocksConstants.find(pred) == blocksConstants.end()) continue;

        if (BB.getName() == "while") outs() << pred->getName() << "\n";

        if (isFirst) {
            res = blocksConstants[pred];
            isFirst = false;
        } else {
            std::map<Value*, int> temp;

            for (auto &rPair : res) {
                for (auto &pPair : blocksConstants[pred]) {
                    if (rPair == pPair) temp[rPair.first] = rPair.second;
                }
            }

            res = temp;
        }
    }

    return res;
}

LoadInst* getLoad(Value *inst1, Value *inst2) {
    LoadInst *load1 = dyn_cast<LoadInst>(inst1);
    LoadInst *load2 = dyn_cast<LoadInst>(inst2);

    return load1 ? load1 : load2;
}

bool bothLoad(Value *lhs, Value *rhs) {
    if (dyn_cast<LoadInst>(lhs) && dyn_cast<LoadInst>(rhs)) return true;

    return false;
}

int computeConstant(Instruction &inst, std::map<Value*, int> &blockConstants) {
    Value* LHS = nullptr;
    Value* RHS = nullptr;
    ConstantInt *C = nullptr;

    if (
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_ConstantInt(C))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(C), PatternMatch::m_Value(LHS))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_Value(RHS)))
    ) {
        if (C) {
            if (LoadInst *LI = dyn_cast<LoadInst>(LHS)) {
                Value* ptr = LI->getPointerOperand();

                if (blockConstants.find(ptr) != blockConstants.end()) {
                    return C->getSExtValue() + blockConstants[ptr];
                }
            } else if (Instruction *opInst = dyn_cast<Instruction>(LHS)) {
                return computeConstant(*opInst, blockConstants) + C->getSExtValue();
            }
        } else if (bothLoad(LHS, RHS)) {
            LoadInst *lhsLoad = cast<LoadInst>(LHS);
            LoadInst *rhsLoad = cast<LoadInst>(RHS);

            Value *lhsPtr = lhsLoad->getPointerOperand();
            Value *rhsPtr = rhsLoad->getPointerOperand();

            if (
                blockConstants.find(lhsPtr) != blockConstants.end() &&
                blockConstants.find(rhsPtr) != blockConstants.end()
            ) {
                return blockConstants[lhsPtr] + blockConstants[rhsPtr];
            }
        } else if (LoadInst *LI = getLoad(LHS, RHS)) {
            Value *nonLoad = LI == LHS ? RHS : LHS;
            Instruction *nonLoadInst = dyn_cast<Instruction>(nonLoad);
            Value *ptr = LI->getPointerOperand();

            if (nonLoadInst && blockConstants.find(ptr) != blockConstants.end()) {
                return computeConstant(*nonLoadInst, blockConstants) + blockConstants[ptr];
            }
        }
    }

    return INFINITY;
}

bool blockConstants(BasicBlock &BB, std::map<BasicBlock*, std::map<Value*, int>> &blocksConstants) {
    bool isChanged = false;
    std::map<Value*, int> blockConstants = computeIntersection(BB, blocksConstants);

    for (Instruction &inst : BB) {
        if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) {
            Value* ptr = SI->getPointerOperand();
            Value* V = SI->getValueOperand();

            if (ConstantInt *C = dyn_cast<ConstantInt>(V)) {
                blockConstants[ptr] = C->getSExtValue();
            } else if (LoadInst *LI = dyn_cast<LoadInst>(V)) {
                if (blockConstants.find(LI->getPointerOperand()) != blockConstants.end()) {
                    blockConstants[ptr] = blockConstants[LI->getPointerOperand()];
                }
            } else if (Instruction *vInst = dyn_cast<Instruction>(V)) {
                int constValue = computeConstant(*vInst, blockConstants);

                if (constValue != INFINITY) blockConstants[ptr] = constValue;
            }
        } else {
            continue;
        }
    }

    if (blockConstants != blocksConstants[&BB]) {
        blocksConstants[&BB] = blockConstants;
        isChanged = true;
    }

    return isChanged;
}

bool constantPropagation(Function &F, std::map<BasicBlock*, std::map<Value*, int>> &blocksConstants) {
    for (BasicBlock &BB : F) {
        if (blockConstants(BB, blocksConstants)) return true;
    }

    return false;
}

PreservedAnalyses ConstantPropagation::run(Module &M, ModuleAnalysisManager &AM) {
    // Run optimizations on each function in the module
    for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter) {
        std::map<BasicBlock*, std::map<Value*, int>> blocksConstants;
        while (constantPropagation(*Fiter, blocksConstants)) {};

        outs() << "Constants for function: " << Fiter->getName();
        outs() << "\n\n";

        for (auto &pair : blocksConstants) {
            outs() << "Constant propagation for basic block: " << pair.first->getName();
            outs() << "\n";

            for (auto &constPair : pair.second) {
                constPair.first->print(outs());
                outs() << ": " << constPair.second << "\n";
            }

            outs() << "\n";
        }

        outs() << "------------------\n\n";
    }

    return PreservedAnalyses::all();
}


PassPluginLibraryInfo getConstantPropagationPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "Constant Propagation", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    if (Name == "constant-propagation") {
                        MPM.addPass(ConstantPropagation());
                        return true;
                    }
                    return false;
                });
        }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getConstantPropagationPluginInfo();
}
