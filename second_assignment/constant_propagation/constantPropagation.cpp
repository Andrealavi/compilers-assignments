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

/**
    Computes the intersetction between two maps

    It is used in blockConstants function in order to obtain the input map
    from predecessors
*/
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

/**
    Returns the first instruction to be a load.
    If both instruction are not LoadInst, nullptr is returned
*/
LoadInst* getLoad(Value *inst1, Value *inst2) {
    LoadInst *load1 = dyn_cast<LoadInst>(inst1);
    LoadInst *load2 = dyn_cast<LoadInst>(inst2);

    return load1 ? load1 : load2;
}

/**
    Returns true if both instruction are LoadInst, false otherwise
*/
bool bothLoad(Value *lhs, Value *rhs) {
    if (dyn_cast<LoadInst>(lhs) && dyn_cast<LoadInst>(rhs)) return true;

    return false;
}

/**
    Helper function used to perform the right algebric operation
    based on the instruction opcode.
*/
int performOp(int val1, int val2, uint64_t opCode) {
    int res = INFINITY;

    switch (opCode) {
        case Instruction::Add:
            res = val1 + val2;
        break;

        case Instruction::Sub:
            res = val1 - val2;
        break;

        case Instruction::Mul:
            res = val1 * val2;
        break;

        case Instruction::SDiv:
        case Instruction::UDiv:
            res = val1 / val2;
        break;

        default:
        break;
    }

    return res;
}

/**
    Returns true if the given argument is the first
    in the instruction
*/
bool isFirst(Instruction &inst, Value *V) {
    return inst.getOperand(0) == V;
}

/**
    Recursive function that computes the constant value to add to the constants map

    Makes use of llvm PatternMatch module in order to find binary operations of the type:
    - Value ⊕ Value
    - Value ⊕ Constant

    Base case:
        - val1 ⊕ val2 ==> both operands in the instructions are known constants.
            The result of the operation is returned
        - the instruction does not respect the criteria ==> returns INFINITY

    Recursive case:
        - One of the two operand is an instruction different from load ==>
            calls the function with the operand instruction as the inst argument
*/
int computeConstant(Instruction &inst, std::map<Value*, int> &blockConstants) {
    Value* LHS = nullptr;
    Value* RHS = nullptr;
    ConstantInt *C = nullptr;

    int val1 = INFINITY;
    int val2 = INFINITY;
    uint64_t opCode = inst.getOpcode();
    bool isVal1First = false;

    if (
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_ConstantInt(C))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_ConstantInt(C), PatternMatch::m_Value(LHS))) ||
        PatternMatch::match(&inst, PatternMatch::m_BinOp(PatternMatch::m_Value(LHS), PatternMatch::m_Value(RHS)))
    ) {

        if (C) {
            if (LoadInst *LI = dyn_cast<LoadInst>(LHS)) {
                Value* ptr = LI->getPointerOperand();

                if (blockConstants.find(ptr) != blockConstants.end()) {
                    val1 = blockConstants[ptr];
                }
            } else if (Instruction *opInst = dyn_cast<Instruction>(LHS)) {
                val1 = computeConstant(*opInst, blockConstants);
            }

            val2 = C->getSExtValue();

            if (isFirst(inst, LHS)) isVal1First = true;

        } else if (bothLoad(LHS, RHS)) {
            LoadInst *lhsLoad = cast<LoadInst>(LHS);
            LoadInst *rhsLoad = cast<LoadInst>(RHS);

            Value *lhsPtr = lhsLoad->getPointerOperand();
            Value *rhsPtr = rhsLoad->getPointerOperand();

            if (
                blockConstants.find(lhsPtr) != blockConstants.end() &&
                blockConstants.find(rhsPtr) != blockConstants.end()
            ) {
                val1 = blockConstants[lhsPtr];
                val2 = blockConstants[rhsPtr];

                isVal1First = true;
            }
        } else if (LoadInst *LI = getLoad(LHS, RHS)) {
            Value *nonLoad = LI == LHS ? RHS : LHS;
            Instruction *nonLoadInst = dyn_cast<Instruction>(nonLoad);
            Value *ptr = LI->getPointerOperand();

            if (nonLoadInst && blockConstants.find(ptr) != blockConstants.end()) {
                val1 = blockConstants[ptr];
                val2 = computeConstant(*nonLoadInst, blockConstants);

                if (LI == LHS) isVal1First = true;
            }
        }
    }

    if (val1 != INFINITY && val2 != INFINITY) {
        if (isVal1First) return performOp(val1, val2, opCode);
        else return performOp(val2, val1, opCode);
    }

    return INFINITY;
}

/**
    Computes the constants for the given block.

    Returns true if the block's constants were changed, false otherwise
*/
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

/**
    Computes constant propagation information for the given function

    Returns true if at least one block's constant have been changed
*/
bool constantPropagation(Function &F, std::map<BasicBlock*, std::map<Value*, int>> &blocksConstants) {
    bool transformed = false;

    for (BasicBlock &BB : F) {
        if (blockConstants(BB, blocksConstants)) transformed = true;
    }

    return transformed;
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
