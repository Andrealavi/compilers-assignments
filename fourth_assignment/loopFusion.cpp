#include "loopFusion.hpp"

using namespace llvm;

static cl::opt<bool> LoopFusionVerbose(
    "lf-verbose",
    cl::desc(
        "Enables verbose output for loop fusion optimization"
    ),
    cl::init(false)
);

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

    if (LoopFusionVerbose) {
        errs() << "Checking if loops are adjacent:\n";
        errs() << "  L1 exit block: ";
        if (L1Exit)
            L1Exit->printAsOperand(errs(), false);
        else
            errs() << "nullptr";
        errs() << "\n  L2 entry block: ";
        if (L2Entry)
            L2Entry->printAsOperand(errs(), false);
        else
            errs() << "nullptr";
        errs() << "\n  Adjacent: " << (L2Entry == L1Exit ? "Yes" : "No") << "\n";
    }

    return L2Entry == L1Exit;
}

bool areCFE(Loop &L1, Loop &L2, DominatorTree &DT, PostDominatorTree &PDT) {
    bool result = DT.dominates(L1.getHeader(), L2.getHeader()) &&
        PDT.dominates(L2.getHeader(), L1.getHeader());

    if (LoopFusionVerbose) {
        errs() << "Checking control flow equivalence:\n";
        errs() << "  L1 header dominates L2 header: "
               << (DT.dominates(L1.getHeader(), L2.getHeader()) ? "Yes" : "No") << "\n";
        errs() << "  L2 header post-dominates L1 header: "
               << (PDT.dominates(L2.getHeader(), L1.getHeader()) ? "Yes" : "No") << "\n";
        errs() << "  CFE result: " << (result ? "Yes" : "No") << "\n";
    }

    return result;
}

bool haveSameItNum(Loop &L1, Loop &L2, ScalarEvolution &SE) {
    unsigned L1TripCount = SE.getSmallConstantTripCount(&L1);
    unsigned L2TripCount = SE.getSmallConstantTripCount(&L2);
    bool result = (L1TripCount == L2TripCount) && (L1TripCount != 0);

    if (LoopFusionVerbose) {
        errs() << "Checking trip counts:\n";
        errs() << "  L1 trip count: " << L1TripCount << "\n";
        errs() << "  L2 trip count: " << L2TripCount << "\n";
        errs() << "  Same trip count: " << (result ? "Yes" : "No") << "\n";
    }

    return result;
}

bool isDependent(LoadInst &inst, std::vector<StoreInst*> storeInsts, DependenceInfo &DI) {
    for (StoreInst *storeInst : storeInsts) {
        if (DI.depends(&inst, storeInst, true)) {
            if (LoopFusionVerbose) {
                errs() << "  Found dependency between:\n";
                errs() << "    Load: ";
                inst.print(errs());
                errs() << "\n    Store: ";
                storeInst->print(errs());
                errs() << "\n";
            }
            return true;
        }
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

    if (LoopFusionVerbose) {
        errs() << "Collected " << loopStores.size() << " store instructions from loop\n";
    }

   return loopStores;
}

bool areDependent(Loop &L1, Loop &L2, DependenceInfo &DI) {
    if (LoopFusionVerbose) {
        errs() << "Checking for dependencies between loops...\n";
    }

    std::vector<StoreInst*> l1Stores = getLoopStores(L1);

    for (BasicBlock *BB : L2.getBlocks()) {
        for (Instruction &inst : *BB) {
            if (LoadInst *LI = dyn_cast<LoadInst>(&inst)) {
                if (isDependent(*LI, l1Stores, DI)) {
                    if (LoopFusionVerbose) {
                        errs() << "Loop dependency found - fusion not possible\n";
                    }
                    return true;
                }
            }
        }
    }

    if (LoopFusionVerbose) {
        errs() << "No loop dependencies found\n";
    }

    return false;
}

bool isLoopFusionApplicable(Loop &L1, Loop &L2, ScalarEvolution &SE,
    DominatorTree &DT, PostDominatorTree &PDT, DependenceInfo &DI) {
        if (LoopFusionVerbose) {
            errs() << "\n===== Checking if loop fusion is applicable =====\n";
            errs() << "Loop 1 header: ";
            L1.getHeader()->printAsOperand(errs(), false);
            errs() << "\nLoop 2 header: ";
            L2.getHeader()->printAsOperand(errs(), false);
            errs() << "\n";
        }

        bool adjacent = areAdjacents(L1, L2);
        if (!adjacent) {
            if (LoopFusionVerbose) errs() << "Loops are not adjacent - fusion not possible\n";
            return false;
        }

        bool cfe = areCFE(L1, L2, DT, PDT);
        if (!cfe) {
            if (LoopFusionVerbose) errs() << "Loops are not control flow equivalent - fusion not possible\n";
            return false;
        }

        bool sameIter = haveSameItNum(L1, L2, SE);
        if (!sameIter) {
            if (LoopFusionVerbose) errs() << "Loops don't have same iteration count - fusion not possible\n";
            return false;
        }

        bool dependent = areDependent(L1, L2, DI);
        if (dependent) {
            if (LoopFusionVerbose) errs() << "Loops have dependencies - fusion not possible\n";
            return false;
        }

        if (LoopFusionVerbose) {
            errs() << "All checks passed - loop fusion is applicable!\n";
        }
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
        if (LoopFusionVerbose) {
            errs() << "Removing unused compare instruction: ";
            inst->print(errs());
            errs() << "\n";
        }

        if (LoadInst *LI = dyn_cast<LoadInst>(inst->getOperand(0))) {
            if (LoopFusionVerbose) {
                errs() << "Removing associated load instruction: ";
                LI->print(errs());
                errs() << "\n";
            }
            LI->eraseFromParent();
        }

        inst->eraseFromParent();
    }
}

void fuseHeader(BasicBlock *l1Header, BasicBlock *l2Header, BasicBlock *l1First) {
    if (LoopFusionVerbose) {
        errs() << "Fusing headers:\n";
        errs() << "  L1 header: ";
        l1Header->printAsOperand(errs(), false);
        errs() << "\n  L2 header: ";
        l2Header->printAsOperand(errs(), false);
        errs() << "\n  L1 first body block: ";
        l1First->printAsOperand(errs(), false);
        errs() << "\n";
    }

    Instruction *l1termInst = l1Header->getTerminator();
    Instruction *l2termInst = l2Header->getTerminator();

    std::vector<Instruction*> instsToMove;

    for (Instruction &inst : *l2Header) {
        if (&inst != l2termInst) instsToMove.push_back(&inst);
    }

    if (LoopFusionVerbose) {
        errs() << "Moving " << instsToMove.size() << " instructions from L2 header to L1 header\n";
    }

    for (Instruction *inst : instsToMove) {
        if (PHINode *PN = dyn_cast<PHINode>(inst)) {
            if (!PN->hasNUses(0)) {
                if (LoopFusionVerbose) {
                    errs() << "  Moving PHI node: ";
                    PN->print(errs());
                    errs() << "\n";
                }
                PN->moveBefore(l1Header->getFirstNonPHI());
            }
        } else if (inst != l2termInst) {
            if (LoopFusionVerbose) {
                errs() << "  Moving instruction: ";
                inst->print(errs());
                errs() << "\n";
            }
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

    if (LoopFusionVerbose && !bbToErase.empty()) {
        errs() << "Removing " << bbToErase.size() << " unreachable predecessor blocks\n";
    }

    for (BasicBlock *BB : bbToErase) BB->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Erasing L2 header\n";
    }
    l2Header->eraseFromParent();
}

void applyLoopFusion(Loop &L1, Loop &L2) {
    if (LoopFusionVerbose) {
        errs() << "\n===== Applying loop fusion =====\n";
        errs() << "L1 header: ";
        L1.getHeader()->printAsOperand(errs(), false);
        errs() << "\nL2 header: ";
        L2.getHeader()->printAsOperand(errs(), false);
        errs() << "\n";
    }

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
            if (LoopFusionVerbose) {
                errs() << "Replacing L2 induction variable with L1 induction variable\n";
                errs() << "  L1 IV: ";
                l1InductionVar->print(errs());
                errs() << "\n  L2 IV: ";
                inductionVariable->print(errs());
                errs() << "\n";
            }
            inductionVariable->replaceAllUsesWith(l1InductionVar);
        }
    }

    if (LoopFusionVerbose) {
        errs() << "Replacing L2 preheader with L1 preheader\n";
        errs() << "Replacing L2 latch with L1 latch\n";
    }

    L2.getLoopPreheader()->replaceAllUsesWith(L1.getLoopPreheader());
    L2.getLoopLatch()->replaceAllUsesWith(L1.getLoopLatch());

    fuseHeader(l1Header, l2Header, l1First);

    if (LoopFusionVerbose) {
        errs() << "Connecting L1 last block to L2 first block\n";
    }
    inst = BranchInst::Create(l2First, l1Last->getTerminator());
    l1Last->getTerminator()->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Connecting L2 last block to L1 latch\n";
    }
    inst = BranchInst::Create(L1.getLoopLatch(), l2Last->getTerminator());
    l2Last->getTerminator()->eraseFromParent();

    if (LoopFusionVerbose) {
        errs() << "Loop fusion completed successfully\n";
    }
}

void updateLoopInfo(Function &F, FunctionAnalysisManager &AM, DominatorTree *&DT,
    PostDominatorTree *&PDT, ScalarEvolution *&SE, DependenceInfo *&DI, LoopInfo *&LI) {
        if (LoopFusionVerbose) {
            errs() << "Updating loop analysis information\n";
        }
        DT = &AM.getResult<DominatorTreeAnalysis>(F);
        PDT = &AM.getResult<PostDominatorTreeAnalysis>(F);
        SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
        DI = &AM.getResult<DependenceAnalysis>(F);
        LI = &AM.getResult<LoopAnalysis>(F);
}

PreservedAnalyses LoopFusion::run(Function &F, FunctionAnalysisManager &AM) {
    if (LoopFusionVerbose) {
        errs() << "Running LoopFusion pass on function: " << F.getName() << "\n";
    }

    bool transformed = false;

    DominatorTree *DT = nullptr;
    PostDominatorTree *PDT = nullptr;
    ScalarEvolution *SE = nullptr;
    DependenceInfo *DI = nullptr;
    LoopInfo *LI = nullptr;

    bool isLoopFusionApplied;
    int fusionCount = 0;

    do {
        isLoopFusionApplied = false;

        updateLoopInfo(F, AM, DT, PDT, SE, DI, LI);

        SmallVector<Loop*, 4> loops = LI->getLoopsInPreorder();

        if (LoopFusionVerbose) {
            errs() << "Found " << loops.size() << " loops in function\n";
        }

        for (auto first_it = loops.begin(); first_it != loops.end(); ++first_it) {
            Loop *L1 = *first_it;

            for (auto second_it = std::next(first_it); second_it != loops.end(); ++second_it) {
                Loop *L2 = *second_it;

                if (LoopFusionVerbose) {
                    errs() << "\nAttempting to fuse loops:\n";
                    errs() << "  Loop 1 header: ";
                    L1->getHeader()->printAsOperand(errs(), false);
                    errs() << "\n  Loop 2 header: ";
                    L2->getHeader()->printAsOperand(errs(), false);
                    errs() << "\n";
                }

                if (isLoopFusionApplicable(*L1, *L2, *SE, *DT, *PDT, *DI)) {
                    applyLoopFusion(*L1, *L2);
                    fusionCount++;

                    if (LoopFusionVerbose) {
                        errs() << "Successfully applied fusion #" << fusionCount << "\n";
                    }

                    isLoopFusionApplied = true;
                    transformed = true;
                    break;
                }
            }

            if (isLoopFusionApplied) break;
        }

        if (isLoopFusionApplied) {
            if (LoopFusionVerbose) {
                errs() << "Invalidating analysis after fusion\n";
            }
            AM.invalidate(F, PreservedAnalyses::none());
        }
    } while(isLoopFusionApplied);

    if (LoopFusionVerbose) {
        errs() << "\nLoop Fusion pass complete - applied " << fusionCount
               << " fusion" << (fusionCount != 1 ? "s" : "") << "\n";
    }

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
