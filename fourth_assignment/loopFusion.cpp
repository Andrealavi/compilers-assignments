#include "loopFusion.hpp"

using namespace llvm;

BasicBlock* getBlockToCheck(Loop &L) {
    return L.isGuarded() ? L.getLoopGuardBranch()->getParent() : L.getLoopPreheader() ;
}

bool areAdjacents(Loop &L1, Loop &L2) {
    BasicBlock *L2Entry = getBlockToCheck(L2);
    BasicBlock *L1Exit = nullptr;

    if (L1.isGuarded()) {
        L1Exit = dyn_cast<BasicBlock>(L1.getLoopGuardBranch()->getOperand(1));
    } else {
        L1Exit = L1.getExitBlock();
    }

    return L2Entry == L1Exit;
}

bool areCFE(Loop &L1, Loop &L2, DominatorTree &DT, PostDominatorTree &PDT) {
    return DT.dominates(L1.getHeader(), L2.getHeader()) &&
        PDT.dominates(L2.getHeader(), L1.getHeader());
}

bool haveSameItNum(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    return SE.getSmallConstantTripCount(&L1) == SE.getSmallConstantTripCount(&L2) != 0;
}

bool isDependent(LoadInst &inst, std::vector<StoreInst*> storeInsts, DependenceInfo &DI) {
    for (StoreInst *storeInst : storeInsts) {
        if (DI.depends(&inst, storeInst, true)) return true;
    }

    return false;
}

std::vector<StoreInst*> getLoopStores(Loop &L) {
    std::vector<StoreInst*> loopStores;

    for (Instruction &inst : *L.getHeader()) {
        if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) loopStores.push_back(SI);
    }

    for (BasicBlock *BB : L.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (StoreInst *SI = dyn_cast<StoreInst>(&inst)) loopStores.push_back(SI);
        }
    }

   return loopStores;
}

bool areDependent(Loop &L1, Loop &L2, DependenceInfo &DI) {
    std::vector<StoreInst*> l1Stores = getLoopStores(L1);

    for (BasicBlock *BB : L2.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&inst)) {
                if (isDependent(*LI, l1Stores, DI)) return true;
            }
        }
    }

    return false;
}

bool isLoopFusionApplicable(Loop &L1, Loop &L2, ScalarEvolution &SE,
    DominatorTree &DT, PostDominatorTree &PDT, DependenceInfo &DI) {
        if (
            !areAdjacents(L1, L2) ||
            !areCFE(L1, L2, DT, PDT) ||
            !haveSameItNum(L1, L2, SE) ||
            areDependent(L1, L2, DI)
        ) return false;

        return true;
}

BasicBlock *getFirstBodyBlock(Loop &L) {
    BasicBlock *BB = L.getHeader();

    for (BasicBlock *succ : successors(BB)) {
        if (L.contains(succ) && succ != BB) return succ;
    }

    return nullptr;
}

BasicBlock *getLastBodyBlock(Loop &L) {
    BasicBlock *BB = L.getLoopLatch();

    for (BasicBlock *pred : predecessors(BB)) {
        if (L.contains(pred) && pred != BB) return pred;
    }

    return nullptr;
}

PHINode* getInductionVariable(Loop &L) {
    BasicBlock *header = L.getHeader();

    for (Instruction &inst : *header) {
        if (PHINode *PN = dyn_cast<PHINode>(&inst)) {
            for (User *user : PN->users()) {
                if (CmpInst *CI = dyn_cast<CmpInst>(user)) {
                    for (User *cmpUser : CI->users()) {
                        if (BranchInst *BI = dyn_cast<BranchInst>(cmpUser)) {
                            if (BI->isConditional()) return PN;
                        }
                    }
                }
            }
        }
    }

    return nullptr;
}

bool hasPreheader(Loop &L) {
    return L.getLoopPreheader() != nullptr;
}

void removeCompareAndLoad(CmpInst *inst) {
    if (inst->hasNUses(0)) {
        inst->print(outs());
        if (LoadInst *LI = dyn_cast<LoadInst>(inst->getOperand(0))) {
            LI->eraseFromParent();
        }

        inst->eraseFromParent();
    }
}

void fuseHeader(BasicBlock *l1Header, BasicBlock *l2Header, BasicBlock *l1First) {
    Instruction *l1termInst = l1Header->getTerminator();
    Instruction *l2termInst = l2Header->getTerminator();

    std::vector<Instruction*> instsToMove;

    for (Instruction &inst : *l2Header) {
        if (&inst != l2termInst) instsToMove.push_back(&inst);
    }

    for (Instruction *inst : instsToMove) {
        if (PHINode *PN = dyn_cast<PHINode>(inst)) {
            if (!PN->hasNUses(0)) PN->moveBefore(l1Header->getFirstNonPHI());
        } else if (inst != l2termInst) {
            inst->moveBefore(l1termInst);
        }
    }

    l2termInst->moveAfter(l1termInst);
    l1termInst->eraseFromParent();
    l2termInst->setSuccessor(0, l1First);

    std::vector<BasicBlock*> bbToErase;

    for (BasicBlock *BB : predecessors(l2Header)) {
        if (BB->hasNPredecessors(0)) bbToErase.push_back(BB);
    }

    for (BasicBlock *BB : bbToErase) BB->eraseFromParent();

    l2Header->eraseFromParent();
}

void applyLoopFusion(Loop &L1, Loop &L2) {
    BasicBlock *l1First = getFirstBodyBlock(L1);
    BasicBlock *l1Last = getLastBodyBlock(L1);

    BasicBlock *l2First = getFirstBodyBlock(L2);
    BasicBlock *l2Last = getLastBodyBlock(L2);

    BasicBlock *l1Header = L1.getHeader();

    BasicBlock *l2Preheader = L2.getLoopPreheader();
    BasicBlock *l2Header = L2.getHeader();
    BasicBlock *l2Exit = L2.getExitBlock();

    BranchInst *inst = nullptr;

    PHINode *inductionVariable = getInductionVariable(L2);
    if (inductionVariable) {
        PHINode *l1InductionVar = getInductionVariable(L1);

        if (l1InductionVar) {
            inductionVariable->replaceAllUsesWith(l1InductionVar);
        }
    }

    L2.getLoopPreheader()->replaceAllUsesWith(L1.getLoopPreheader());
    L2.getLoopLatch()->replaceAllUsesWith(L1.getLoopLatch());

    fuseHeader(l1Header, l2Header, l1First);

    inst = BranchInst::Create(l2First, l1Last->getTerminator());
    l1Last->getTerminator()->eraseFromParent();

    inst = BranchInst::Create(L1.getLoopLatch(), l2Last->getTerminator());
    l2Last->getTerminator()->eraseFromParent();
}

void updateLoopInfo(Function &F, FunctionAnalysisManager &AM, DominatorTree *&DT,
    PostDominatorTree *&PDT, ScalarEvolution *&SE, DependenceInfo *&DI, LoopInfo *&LI) {
        DT = &AM.getResult<DominatorTreeAnalysis>(F);
        PDT = &AM.getResult<PostDominatorTreeAnalysis>(F);
        SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
        DI = &AM.getResult<DependenceAnalysis>(F);
        LI = &AM.getResult<LoopAnalysis>(F);
}

PreservedAnalyses LoopFusion::run(Function &F, FunctionAnalysisManager &AM) {
    bool transformed = false;

    DominatorTree *DT = nullptr;
    PostDominatorTree *PDT = nullptr;
    ScalarEvolution *SE = nullptr;
    DependenceInfo *DI = nullptr;
    LoopInfo *LI = nullptr;

    bool isLoopFusionApplied;
    do {
        isLoopFusionApplied = false;

        updateLoopInfo(F, AM, DT, PDT, SE, DI, LI);

        SmallVector<Loop*, 4> loops = LI->getLoopsInPreorder();

        for (auto first_it = loops.begin(); first_it != loops.end(); ++first_it) {
            Loop *L1 = *first_it;

            for (auto second_it = std::next(first_it); second_it != loops.end(); ++second_it) {
                Loop *L2 = *second_it;

                if (isLoopFusionApplicable(*L1, *L2, *SE, *DT, *PDT, *DI)) {
                    applyLoopFusion(*L1, *L2);

                    isLoopFusionApplied = true;
                    break;
                }
            }

            if (isLoopFusionApplied) break;
        }

        if (isLoopFusionApplied) {
            AM.invalidate(F, PreservedAnalyses::none());
        }
    } while(isLoopFusionApplied);

    return PreservedAnalyses::none();
}

PassPluginLibraryInfo getLoopFusionPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopFusion", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // Register the pass with the pass builder
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    // Allow the pass to be invoked via -passes=local-opts
                    if (Name == "lf") {
                        FPM.addPass(LoopSimplifyPass());
                        FPM.addPass(LoopFusion());
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
    return getLoopFusionPluginInfo();
}
